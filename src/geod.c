/*
 * File:    geod.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * GEO daemon main source file
 * Key features:
 *  Daemon startup
 *      config load
 *      process id handling (checks)
 *      listen , bind sockets
 *  Manage cyclic tasks
 *  Load and unload .so plugins
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <dirent.h>
#include <stdarg.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <assert.h>

#define DATA_TABLE_LINKED
#define HTTP_STATIC_LINKED
#define PLUGINHST_STATIC_LINKED
#define CMD_STATIC_LINKED

#include "global.h"
#include "sync.h"
#include "plugin.h"
#include "pluginhst.h"
#include "http.h"
#include "config.h"
#include "cache.h"
#include "handlers.h"
#include "hashmap.h"

#define MAX_SERVER_SOCKETS 4

extern void http_route_register(const char *route, PluginHttpRequestHandler handler, PluginContext* pc);
extern int http_route_search(const char *route, PluginHttpRequestHandler *prh, PluginContext **ppc);
 
typedef enum{
    PROTOCOLID_HTTP,
    PROTOCOLID_WS,
    PROTOCOLID_CONTROL
} ProtocolId_t;

struct ServerSocket;
struct ContextServerData;

typedef struct ContextServerData* (*on_accept_fn_t)(struct ServerSocket *ss, int client_fd, struct sockaddr_in *addr);
typedef void* (*process_fn_t)(void *arg);

typedef struct ServerProtocol{
    ProtocolId_t id;
    const char *label;
    on_accept_fn_t on_accept;
    const process_fn_t on_process;
} ServerProtocol;

typedef struct ContextServerData {
    struct ContextServerData *next;
    pthread_t thread_id;
    ClientContext cc;
}ContextServerData;

typedef struct StatData{
    unsigned short actual5s;  //jumping
    unsigned short a_min5s;
    unsigned short a_max5s;
    unsigned int a_sum5s;     //aggregated

    unsigned short min5sm;
    unsigned short max5sm;
    unsigned int avg5sm;    
    unsigned int value_m;
} StatData;

typedef struct ServerSocket{
    int fd;
    int port;
    int backlog;
    const char *label;
    const ServerProtocol *protocol;
    struct sockaddr_in addr;
    char server_ip[32];
    int opt;
    StatData stat_alive;
    StatData stat_finished;
    StatData stat_failed;
    StatData stat_exectime;
    double execution_time_5s;
    ContextServerData *first_ctx_data;
} ServerSocket;


ServerSocket g_server_sockets[MAX_SERVER_SOCKETS];
size_t g_server_socket_count = 0;

ContextServerData* onAcceptHttp(ServerSocket* ss, int client_fd, struct sockaddr_in *addr);
ContextServerData* onAcceptWs(ServerSocket* ss, int client_fd, struct sockaddr_in *addr);
ContextServerData* onAcceptControl(ServerSocket* ss, int client_fd, struct sockaddr_in *addr);

//void* onProcessHttp(ClientContext *ctx);
void* onProcessWs(void *arg);
void* onProcessControl(void *arg);

/** unix signal handlers and process id save */
volatile sig_atomic_t keep_running = 1;
volatile sig_atomic_t reload_plugins = 0;
volatile sig_atomic_t release_plugins = 0;

MapContext map_context;

int g_port_http;
int g_port_ws;
int g_port_control;
int g_debug_msg_enabled=0;

char g_geod_pidfile[MAX_PATH] = GEOD_PIDFILE;
int g_force_start_kill =0;
char g_geod_logfile[MAX_PATH] = GEOD_LOGFILE;


void sigusr1_handler(int signum) {
    (void)signum;
    reload_plugins = 1;
    g_debug_msg_enabled= config_get_int("LOG", "debug_msg_enabled", 0);
}
void sigusr2_handler(int signum){
    (void)signum;
    release_plugins = 1;
}
void sigint_handler(int signum) {
    (void)signum;
    keep_running = 0;
}
void sigterm_handler(int signum) {
    (void)signum;
    keep_running = 0;
}

void sigchld_handler(int sig) {
    (void)sig;
    // tries to help the kernel, to free up zombie processes, the number of zombies are
    // a practical number, this much sys calls will trigger the kernel to lett a z process
    // to be freed up. We expect not more than this amout of kept zombie processes.
    // the timeout is one secount, so after max count number of calls, the function exits
    // anyway. Non blocking waitpid() is used to avoid blocking the whole program.
    int count = 32; // theoreticaly this is the expected zombie count max.
    time_t start = time(NULL);
    while (count-- > 0 && waitpid(-1, NULL, WNOHANG) > 0)
    {
        if (time(NULL) - start > 1) break; // max 1 másodperc
    };
}

void setup_sigchld_handler() {
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
}

/**
 * wait until a pid identified process exit
 */
int wait_for_process_exit(pid_t pid, int timeout_sec) {
    for (int i = 0; i < timeout_sec * 10; i++) {
        if (kill(pid, 0) != 0) {
            return 0; // Success
        }
        usleep(100000); // 100 ms
    }
    return -1; // Was not successfully stopped yet
}

/**
 * Writes the configured PID file, if it is possible.
 * Depending on -k switch, it may kill the previous process.
 */
void write_pidfile_or_exit() {
    config_get_string("GEOD", "pidfile", g_geod_pidfile, sizeof(g_geod_pidfile), GEOD_PIDFILE);
    FILE *fp = fopen(g_geod_pidfile, "r");
    if (fp) {
        pid_t old_pid;
        if (fscanf(fp, "%d", &old_pid) == 1) {
            if (kill(old_pid, 0) == 0) {
                fprintf(stderr, "GeoD already running with PID %d\n", old_pid);
                if (g_force_start_kill){
                    kill(old_pid, SIGTERM);
                    wait_for_process_exit(old_pid, 6);
                    if (kill(old_pid, 0) == 0) {
                        fprintf(stderr, "GeoD still running after SIGTERM, trying SIGKILL...\n");
                        kill(old_pid, SIGKILL);
                        wait_for_process_exit(old_pid, 6);
                        if (kill(old_pid, 0) == 0) {
                            fprintf(stderr, "GeoD could not be terminated (PID %d)\n", old_pid);
                            fclose(fp);
                            exit(1);
                        }
                    }
                }else{
                    fclose(fp);
                    exit(1);
                }
            }
        }
        fclose(fp);
    }

    fp = fopen(g_geod_pidfile, "w");
    if (!fp) {
        perror("Cannot write PID file");
        exit(1);
    }
    fprintf(fp, "%d\n", getpid());
    fclose(fp);
}
void remove_pidfile() {
    unlink(g_geod_pidfile);
}

/** Log a message to the log file. */
// Shared timestamp caching for log messages
static time_t g_log_last_sec = 0;
static char g_log_last_timestamp[32] = "";

static const char* get_cached_timestamp(void) {
    time_t now = time(NULL);
    if (now != g_log_last_sec) {
        struct tm tmbuf;
        struct tm *t = localtime_r(&now, &tmbuf);
        if (t) {
            snprintf(g_log_last_timestamp, sizeof(g_log_last_timestamp),
                     "%04d-%02d-%02d %02d:%02d:%02d",
                     t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                     t->tm_hour, t->tm_min, t->tm_sec);
            g_log_last_sec = now;
        } else {
            snprintf(g_log_last_timestamp, sizeof(g_log_last_timestamp), "0000-00-00 00:00:00");
        }
    }
    return g_log_last_timestamp;
}

void logmsg(const char *fmt, ...) {
    FILE *log = fopen(g_geod_logfile, "a");
    if (!log) return;
    const char *timestamp = get_cached_timestamp();
    fprintf(log, "%s ", timestamp);
    va_list args;
    va_start(args, fmt);
    vfprintf(log, fmt, args);
    va_end(args);
    fprintf(log, "\n");
    fclose(log);
}
void errormsg(const char *fmt, ...) {
    FILE *log = fopen(g_geod_logfile, "a");
    if (!log) return;
    const char *timestamp = get_cached_timestamp();
    fprintf(log, "%s ERROR: ", timestamp);
    va_list args;
    va_start(args, fmt);
    vfprintf(log, fmt, args);
    va_end(args);
    fprintf(log, "\n");
    fclose(log);
}

void debugmsg(const char *fmt, ...) {
    if (g_debug_msg_enabled){
        FILE *log = fopen(g_geod_logfile, "a");
        if (!log) return;
        const char *timestamp = get_cached_timestamp();
        fprintf(log, "%s DEBUG: ", timestamp);
        va_list args;
        va_start(args, fmt);
        vfprintf(log, fmt, args);
        va_end(args);
        fprintf(log, "\n");
        fclose(log);
    }
}

int loadSo(){
    map_context.lib_handle = dlopen("./libmapgen_c.so", RTLD_LAZY);
    if (!map_context.lib_handle) {
        const char *error = dlerror();
        logmsg("dlopen: %s", error?error : "Unknown error");
        return (1);
    }
    map_context.get_info = (get_terrain_info_t)dlsym(map_context.lib_handle, "mapgen_get_terrain_info");
    if (!map_context.get_info) {
        const char *error = dlerror();
        logmsg("dlopen: %s", error?error : "Unknown error");
        return (1);
    }
    map_context.mapgen_finish = (mapgen_finish_t)dlsym(map_context.lib_handle, "mapgen_finish");
    if (!map_context.mapgen_finish) {
        const char *error = dlerror();
        logmsg("dlopen: %s", error?error : "Unknown error");
        return (1);
    }
    map_context.mapgen_init = (mapgen_init_t)dlsym(map_context.lib_handle, "mapgen_init");
    if (!map_context.mapgen_init) {
        const char *error = dlerror();
        logmsg("dlopen: %s", error?error : "Unknown error");
        return (1);
    }
    g_housekeeper.mapgen_loaded = 1U;
    g_housekeeper.last_mapgen_use = time(NULL);
    return (0);
}
void closeSo(){
    if (map_context.lib_handle) {
        dlclose(map_context.lib_handle);
        map_context.lib_handle = NULL;
        map_context.get_info = NULL;
        map_context.mapgen_finish = NULL;
        map_context.mapgen_init = NULL;
        g_housekeeper.mapgen_loaded = 0U;
    }
}
int start_map_context(void){
    if (!g_housekeeper.running) {
        logmsg("Housekeeper is not running");
        return 1;
    }
    g_housekeeper.last_mapgen_use = time(NULL);
    if (g_housekeeper.mapgen_loaded) {
        // logmsg("Mapgen already loaded");
        return 0;
    }
    loadSo();
    if (!g_housekeeper.mapgen_loaded) {
        logmsg("Failed to load mapgen");
        return 1;
    }
    return 0;
}
int stop_map_context(void){
    if (!g_housekeeper.running) {
        logmsg("Housekeeper is not running");
        return 1;
    }
    if (!g_housekeeper.mapgen_loaded) {
        logmsg("Mapgen not loaded");
        return 0;
    }
    g_housekeeper.last_mapgen_use = time(NULL);
    return 0;
}

int get_map_info(TerrainInfo *info, float lat, float lon) {
    *info = map_context.get_info(lat,lon);
    return 0;
}

int image_context_start(PluginContext *pc){
    if (!pc) pc=get_plugin_context("image");
    if (pc){
        plugin_start(pc->id);
        //pc->image.start_image_context(pc);
        return 0;
    }
    return 1;
}
int image_context_stop(PluginContext *pc){
    if (!pc) pc=get_plugin_context("image");
    if (pc){
        //pc->image.stop_image_context(pc);
        plugin_stop(pc->id);
    }
    return 0;
}
int image_create(PluginContext *pc, Image *image, const char *filename, unsigned int width, unsigned int height, ImageBackendType backend, ImageFormat format, ImageBufferFormat buffer_type){
    if (pc){
        pc->image.create(pc, image, filename, width, height, backend, format, buffer_type);
        return 0;
    }else{
        logmsg("No image plugin");
    }
    return 1;
}
int image_destroy(PluginContext *pc, Image *image){
    if (pc) {
        pc->image.destroy(pc, image);
        return 0;
    }
    return 1;
}
void image_get_buffer(PluginContext *pc, Image *image, void** buffer){
    if (pc) {
        pc->image.get_buffer(pc, image, buffer);
    }
}
void image_write_row(PluginContext *pc, Image *image, void *row){
    if (pc) {
        pc->image.write_row(pc, image, row);
    }
}

/**
 * Add a HTTP route 
 */
void register_http_routes(PluginContext *pc, int count, const char *routes[]) {
    // add to hashmap, new way
    for (int i=0; i < count; i++){
        http_route_register(routes[i], NULL, pc);
    }
}

/**
 * Add a route to the WS protocol
 */
void register_ws_routes(PluginContext *pc, int count, const char *routes[]) {
    WsCapabilities *cap = &pc->ws_caps;
    cap->ws_route_count = count;
    cap->ws_routes = malloc(count * sizeof(WsRouteRule));
    if (!cap->ws_routes) {
        logmsg("Failed to allocate memory for HTTP routes");
        return;
    }
    for (int i = 0; i < count; i++) {
        int len = strlen(routes[i]);
        cap->ws_routes[i] = malloc(len + 1);
        snprintf(cap->ws_routes[i], len + 1, "%s", routes[i]); // it could be also /plugin/pluginnname, etc...
    }
}

/**
 * Future implementation, placeholder
 */
void register_db_queue(PluginContext *ctx, const char *db){
    //placeholder function
    (void)ctx;
    (void)db;
}

/**
 * Actually only one plugin is implementing ws, so this is not yet
 * used, but placeholder...
 */
void ws_hostside_handshake(PluginContext *pc, WsRequestParams* wsp, const char *msg){
    //placeholder function, just an exaple for host interface
    (void)msg;
    (void)wsp;
    int wspluginid = pc->id;
    if (0==plugin_start(wspluginid)){
            // plugin dinamyc functions are available...
        plugin_stop(wspluginid);
    }
}


void logClientContext(ClientContext *cc){
    logmsg("Client: ip:%s fd:%d", cc->client_ip, cc->socket_fd);
}

#define SD_MAX_COUNT_5S (12) // 60 div 5, one minute elapsed
unsigned short g_counter5s=0;
void stat_data_add(StatData *sd, int value){
    sd->actual5s = value;
    if (sd->a_max5s < value) sd->a_max5s = value;
    if (sd->a_min5s > value) sd->a_min5s = value;
    sd->a_sum5s += value;
    if (g_counter5s >= SD_MAX_COUNT_5S){
        sd->value_m = sd->a_sum5s;
        sd->min5sm = sd->a_min5s;
        sd->max5sm = sd->a_max5s;
        sd->avg5sm = sd->a_sum5s / g_counter5s;
        sd->a_max5s = 0;
        sd->a_min5s = 0xffff;
        sd->a_sum5s = 0;
    }
}
void server_stat_clear(void){
    sync_det_clear();
    pluginhst_stat_clear();
}

typedef enum{
    FID_SS_Protocol,
    FID_SS_ServerIp,
    FID_SS_ServerPort,
    FID_SS_Alive,
    FID_SS_Ok,
    FID_SS_Failed,
    FID_SS_ExecutionTime,
    FID_SS_Cpu,
    FID_SS_MAXNUMBER
}TableServerStatsFieldId;

const FieldDescr g_table_ServerStatFields[FID_SS_MAXNUMBER] ={
    [FID_SS_Protocol]       = { .name = "Protocol",    .fmt = "%9s",     .width = 9,  .align_right = 1, .type = FIELD_TYPE_STRING, .precision = -1 },
    [FID_SS_ServerIp]       = { .name = "Server IP",   .fmt = "%16s",    .width = 16, .align_right = 1, .type = FIELD_TYPE_STRING, .precision = -1 },
    [FID_SS_ServerPort]     = { .name = "Port",        .fmt = "%-5d",    .width = 7,  .align_right = 0, .type = FIELD_TYPE_INT,    .precision = -1 },
    [FID_SS_Alive]          = { .name = "Alive",       .fmt = "%7s",     .width = 7,  .align_right = 0, .type = FIELD_TYPE_STRING, .precision = -1 },
    [FID_SS_Ok]             = { .name = "Ok",          .fmt = "%7s",     .width = 7,  .align_right = 0, .type = FIELD_TYPE_STRING, .precision = -1 },
    [FID_SS_Failed]         = { .name = "Failed",      .fmt = "%7s",     .width = 7,  .align_right = 0, .type = FIELD_TYPE_STRING, .precision = -1 },
    [FID_SS_ExecutionTime]  = { .name = "Exe [ms]",    .fmt = "%9s",     .width = 9,  .align_right = 0, .type = FIELD_TYPE_STRING, .precision = -1 },
    [FID_SS_Cpu]            = { .name = "CPU %",       .fmt = "%16s",    .width = 16, .align_right = 0, .type = FIELD_TYPE_STRING, .precision = -1 }
};
const TableDescr g_table_ServerStat ={
    .fields_count = FID_SS_MAXNUMBER,
    .fields = g_table_ServerStatFields
};
TableResults g_result_ServerStat = { .rows_count = 0, .fields = NULL };

void server_stat_table(const TableDescr** tdout, TableResults ** trout){
    if (g_result_ServerStat.rows_count != g_server_socket_count){
        table_results_free(&g_result_ServerStat);
        table_results_alloc(&g_table_ServerStat, &g_result_ServerStat, g_server_socket_count);
    }
    for(size_t i=0; i < g_server_socket_count; i++) {
        ServerSocket *ss = &g_server_sockets[i];
        FieldValue *row = table_row_get(&g_table_ServerStat, &g_result_ServerStat, i);
        table_field_set_str( &row[FID_SS_Protocol], ss->protocol->label);
        table_field_set_str( &row[FID_SS_ServerIp], ss->server_ip);
        row[FID_SS_ServerPort].i = ss->port;
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "%2d - %2d", ss->stat_alive.min5sm,          ss->stat_alive.max5sm);
        table_field_set_str( &row[FID_SS_Alive], tmp);
        snprintf(tmp, sizeof(tmp), "%2d - %2d", ss->stat_finished.min5sm,       ss->stat_finished.max5sm);
        table_field_set_str( &row[FID_SS_Ok], tmp);
        snprintf(tmp, sizeof(tmp), "%2d - %2d", ss->stat_failed.min5sm,         ss->stat_failed.max5sm);
        table_field_set_str( &row[FID_SS_Failed], tmp);
        snprintf(tmp, sizeof(tmp), "%3d - %3d", ss->stat_exectime.min5sm,       ss->stat_exectime.max5sm);
        table_field_set_str( &row[FID_SS_ExecutionTime], tmp);
        snprintf(tmp, sizeof(tmp), "%5.2f - %5.2f", ss->stat_exectime.min5sm/50.0,  ss->stat_exectime.max5sm/50.0);
        table_field_set_str( &row[FID_SS_Cpu], tmp);
    }
    *tdout = &g_table_ServerStat;
    *trout = &g_result_ServerStat;
}

void housekeeper_server_clients(time_t now ){
    (void)now; // suppress unused parameter warning (now is passed to housekeeper_server_clients 
    #if (0)
    if (g_debug_msg_enabled){
        for(size_t i=0; i<g_server_socket_count; i++) {
            ServerSocket *ss = &g_server_sockets[i];
            ContextServerData *csd = ss->first_ctx_data;
            logmsg("Server: %s ip:%s:%d fd:%d",ss->protocol->label, ss->server_ip, ss->port, ss->fd);
            int limit =10;
            while (csd){
                ClientContext *cc = &csd->cc;
                logClientContext(cc);
                if (--limit < 0) break;
                csd = csd->next;
            }
        }
    }
    #endif
    //
    for(size_t i=0; i<g_server_socket_count; i++) {
        ServerSocket *ss = &g_server_sockets[i];
        int limit =300;
        int count_alive = 0;
        int count_finished = 0;
        int count_failed = 0;
        ContextServerData **csd_prev = &ss->first_ctx_data;
        while (*csd_prev){
            ContextServerData *csd = *csd_prev;
            if (csd->cc.result_status == CTX_ERROR){
                count_failed++;
            }

            if (csd->cc.socket_fd<0){
                ss->execution_time_5s+= csd->cc.elapsed_time;
                *csd_prev = csd->next; // decouple
                free(csd);
                count_finished++;
                continue;
            }
            count_alive++;
            if (--limit < 0) break;
            csd_prev= &csd->next;
            if (csd_prev == NULL) break;

        }
        stat_data_add(&ss->stat_alive, count_alive);
        stat_data_add(&ss->stat_finished, count_finished);
        stat_data_add(&ss->stat_failed, count_failed);
        stat_data_add(&ss->stat_exectime, ss->execution_time_5s*1000.0);
        if (g_counter5s >= SD_MAX_COUNT_5S){
            /* logmsg("%s Server %s:%d Clients stats: (alive, ok, failed, exec, cpu) min (%d, %d, %d, %dms, %0.2f%%) max (%d, %d, %d, %dms, %0.2f%%)",
                ss->protocol->label, ss->server_ip, ss->port,
                ss->stat_alive.min5sm, ss->stat_finished.min5sm, ss->stat_failed.min5sm, ss->stat_exectime.min5sm, ss->stat_exectime.min5sm/50.0,
                ss->stat_alive.max5sm, ss->stat_finished.max5sm, ss->stat_failed.max5sm, ss->stat_exectime.max5sm, ss->stat_exectime.max5sm/50.0);
            */
            ss->execution_time_5s = 0.0;
        }
    }
    if (g_counter5s >= SD_MAX_COUNT_5S){
        g_counter5s = 0;
    }else{
        g_counter5s++;
    }
}

int file_exists(const char *filename) {
    struct stat st;
    if (stat(filename, &st) != 0) return 0; // nem létezik
    return 1;
}
int file_exists_recent(const char *filename, int max_age_seconds) {
    struct stat st;
    if (stat(filename, &st) != 0) return 0; // nem létezik

    time_t now = time(NULL);
    return (now - st.st_mtime) < max_age_seconds;
}

void close_ClientContext(ClientContext *ctx) {
    close(ctx->socket_fd);
    ctx->socket_fd = -1;
    clock_gettime(CLOCK_MONOTONIC, &ctx->end_time);
    ctx->elapsed_time = (ctx->end_time.tv_sec - ctx->start_time.tv_sec) +
                          (ctx->end_time.tv_nsec - ctx->start_time.tv_nsec) / 1e9;

}

typedef enum{
    CMD_S_Stop,
    CMD_S_Reload,
    CMD_S_Release,
    CMD_S_DebugToggleGeneral,
    CMD_S_MAXNUMBER
}ServerCommandId;

CommandEntry g_ServerCommands[CMD_S_MAXNUMBER]={
    [CMD_S_Release]             = {.path="release",   .help="Release all the plugins",           .arg_hint=""},
    [CMD_S_Reload]              = {.path="reload",    .help="Reload the plugins",                .arg_hint=""},
    [CMD_S_Stop]                = {.path="stop",      .help="Stops the server",                  .arg_hint=""},
    [CMD_S_DebugToggleGeneral]  = {.path="debug", .help="toggle global debug log level",        .arg_hint="On / OFF"},
//    [CMD_CLEAR]     = {.path="clear",     .help="Clear Statistics",                  .arg_hint=""},
};

int server_execute_commands(ClientContext *ctx, CommandEntry *pe, char* cmd){
    (void)ctx;
    (void)cmd;
    int ret = -1;
    if (pe)
    switch(pe->handlerid){
        case CMD_S_Stop: keep_running = 0; ret = 0; break;
        case CMD_S_Reload: reload_plugins = 1; ret = 0; break;
        case CMD_S_DebugToggleGeneral: g_debug_msg_enabled = (g_debug_msg_enabled)?0:1; ret = 0; break;
        case CMD_S_Release: release_plugins = 1; ret =0; break;
    }
    return ret;
}

void server_init(){
    cmd_register_commands(NULL, g_ServerCommands, CMD_S_MAXNUMBER);
}
void server_destroy(){

}
void *onProcessControl(void *arg) {
    ClientContext *ctx = (ClientContext *)arg;
    PluginContext *pctx = get_plugin_context("control");
    if (pctx)
    if (!plugin_start(pctx->id)){   
        pctx->control.request_handler(pctx, ctx, "", 0, NULL);
        plugin_stop(pctx->id);
    }
/*
    // pthread_setname_np("control");
    int keep_processing = 0;
    while (keep_processing && keep_running) {
        char line[BUF_SIZE];
        // read lines from client
        ssize_t n = read(ctx->socket_fd, line, sizeof(line)-1);
        if (n > 0) {
            line[n] = '\0';
            dprintf(ctx->socket_fd, "You said: %s", line);  // <- visszaküldés
            char *cmd = strtok(line, " \r\n");
            if (cmd) {
                if (strcasecmp(line, "quit") == 0) {
                    dprintf(ctx->socket_fd, "Goodbye!\n");
                    keep_processing = 0;
                }else if (strcasecmp(line, "reload") == 0) {
                    dprintf(ctx->socket_fd, "Plugins will be reloaded\n");
                    reload_plugins=1;
                }else if (strcasecmp(line, "stop") == 0) {
                    dprintf(ctx->socket_fd, "Server will be stopped\n");
                    keep_running = 0;
                }else if (strcasecmp(line, "stat") == 0) {
                    char *arg = strtok(NULL, " \r\n");
                    if (arg) {
                        dprintf(ctx->socket_fd, "%s staistics\n", arg);
                    }else{
                        dprintf(ctx->socket_fd, "staistics\n");
                    }
                    char dump[BUF_SIZE];
                    sync_det_str_dump( dump, BUF_SIZE);
                    dprintf(ctx->socket_fd, "sync: %s\n", dump);
                    pluginhst_det_str_dump(dump, BUF_SIZE);
                    dprintf(ctx->socket_fd, "host: %s\n", dump);
                    for(int i=0; i<g_PluginCount; i++){
                        PluginContext *p= &g_Plugins[i];
                        if (p->stat.det_str_dump){
                            p->stat.det_str_dump(dump, BUF_SIZE);
                            dprintf(ctx->socket_fd, "plugin %d (%s): %s\n", p->id, p->name, dump);
                        }
                    }
                }else if (strcasecmp(line, "debug") == 0) {
                    g_debug_msg_enabled = (g_debug_msg_enabled)?0:1;
                    dprintf(ctx->socket_fd, "debug: %s\n", g_debug_msg_enabled?"on":"off");
                }else{
                    for (int id = 0; id < g_PluginCount; id++) {
                        PluginContext *pctx = &g_Plugins[id];
                        int n = pctx->control_caps.route_count;
                        if (n){
                            for (int j = 0; j < n; j++) {
                                ControlCapabilities *cap = &pctx->control_caps;
                                char *route = cap->routes[j];
                                if (strcasecmp(route, cmd) == 0) {
                                    char *arg = strtok(NULL, " \r\n");
                                    int argc =1;
                                    char * argv[3]={arg, NULL,NULL};
                                    if (!plugin_start(id)){
                                        pctx->control.request_handler(pctx, ctx, cmd, argc, argv);
                                    }else{
                                        logmsg("itt");
                                    }
                                    plugin_stop(id);
                                    
                                    break;
                                }else{
                                    logmsg("ott");
                                }
                            }
                        }
                    }
                }
            }
        }
        sleep(1);
    }
*/
    close_ClientContext(ctx);
    return NULL;
}

void *onProcessWs(void *arg){
    ClientContext *ctx = (ClientContext *)arg;
    PluginContext *pctx= get_plugin_context("ws");
    int wsid = pctx->id;
    if (!plugin_start(wsid)){

        ctx->result_status = CTX_RUNNING;
        int total_len = 0;
        while (total_len < BUF_SIZE - 1) {
            int bytes = read(ctx->socket_fd, ctx->request_buffer + total_len, BUF_SIZE - 1 - total_len);
            if (bytes <= 0) {
                errormsg("Error reading request from client");
                ctx->result_status = CTX_ERROR;
                close_ClientContext(ctx);
                return NULL;
            }
            total_len += bytes;
            ctx->request_buffer[total_len] = '\0';
            if (strstr(ctx->request_buffer, "\r\n\r\n")) {
                break;
            }
        }
        ctx->request_buffer_len = total_len;
        // parse the HTTP protocol side of the request
        // later, the session handling could be done here
        http_parse_request(ctx, &ctx->request);
    
        // App specific query, if needed for any reason (?)
        //RequestParams params;
        //parse_request_path_and_params(ctx->request_buffer, &params);
        
        // Handle specific paths

        WsRequestParams wsp;
        strncpy( wsp.session_id, ctx->request.session_id, sizeof(wsp.session_id));

        debugmsg("before the plugin ws.request_handler");
        pctx->ws.request_handler(pctx, ctx, &wsp);
        debugmsg("after the plugin ws.request_handler");
        close_ClientContext(ctx);
        plugin_stop(wsid);
    }else{
        errormsg("ws plugin not found?");
    }
    return NULL;
}

void *http_handle_client(void *arg) {
    ClientContext *ctx = (ClientContext *)arg;
    ctx->result_status = CTX_RUNNING;
    int total_len = 0;
    while (total_len < BUF_SIZE - 1) {
        int bytes = read(ctx->socket_fd, ctx->request_buffer + total_len, BUF_SIZE - 1 - total_len);
        if (bytes <= 0) {
            errormsg("Error reading request from client");
            ctx->result_status = CTX_ERROR;
            close_ClientContext(ctx);
            return NULL;
        }
        total_len += bytes;
        ctx->request_buffer[total_len] = '\0';
        if (strstr(ctx->request_buffer, "\r\n\r\n")) {
            break;
        }
    }
    ctx->request_buffer_len = total_len;
    http_parse_request(ctx, &ctx->request);

    RequestParams params;
    parse_request_path_and_params(ctx->request_buffer, &params);

    PluginContext *pc = NULL;
    PluginHttpRequestHandler rh = NULL;
    int rsearch= http_route_search(params.path, &rh, &pc);
    if (0 == rsearch){
        if (rh && !pc){
            rh(NULL, ctx, &params); // host implementation
        }else if (!rh && pc){
            if (!plugin_start(pc->id)){
                pc->http.request_handler(pc, ctx, &params);
                plugin_stop(pc->id);
                if (ctx->result_status == CTX_RUNNING) ctx->result_status = CTX_FINISHED_OK;
                close_ClientContext(ctx);
                return NULL;
            }else{
                errormsg("Plugin %s is busy", pc->name);
                dprintf(ctx->socket_fd, "HTTP/1.1 503 Service Unavailable\r\n\r\n");
                ctx->result_status = CTX_ERROR;
                close_ClientContext(ctx);
                return NULL;
            }
        }
    }
    // Handle unknown paths
    dprintf(ctx->socket_fd, "HTTP/1.1 404 Not Found\r\n\r\n");
    ctx->result_status = CTX_ERROR;
    close_ClientContext(ctx);
    return NULL;
}

void logstartup(){
    char cwd[MAX_PATH];
    getcwd(cwd, sizeof(cwd));
    logmsg("GeoD starting in working dir %s", cwd);
}

const ServerProtocol g_server_protocols[3] = {
   [PROTOCOLID_HTTP] =  {
        .id = PROTOCOLID_HTTP,
        .label = "HTTP",
        .on_accept = onAcceptHttp,
        .on_process = http_handle_client
    },
    [PROTOCOLID_WS] = {
        .id = PROTOCOLID_WS,
        .label = "WS",
        .on_accept = onAcceptWs,
        .on_process = onProcessWs
    },
    [PROTOCOLID_CONTROL] = {
        .id = PROTOCOLID_CONTROL,
        .label = "CONTROL",
        .on_accept = onAcceptControl,
        .on_process = onProcessControl
    }
};

int wait_for_bindable_port(struct sockaddr_in *addr, int retries, int delay_ms) {
    int sock;
    for (int i = 0; i < retries; i++) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            usleep(delay_ms * 1000);
            continue;
        }

        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (bind(sock, (struct sockaddr *)addr, sizeof(*addr)) == 0) {
            close(sock);
            return 1; // success
        }

        close(sock);
        usleep(delay_ms * 1000);
    }
    return 0; // not bindable
}

int register_server_socket(int port, const ServerProtocol const *proto, int backlog) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ServerSocket *ss = &g_server_sockets[g_server_socket_count];
    *ss = (ServerSocket){
        .fd = fd, .port = port, .protocol = proto, .addr = {0},
        .opt = 1, .backlog = backlog
    };
    ss->addr.sin_family = AF_INET;
    ss->addr.sin_port = htons(port);
    ss->addr.sin_addr.s_addr = INADDR_ANY;
    config_get_string(proto->label, "server_ip",ss->server_ip,sizeof(ss->server_ip), "127.0.0.1");
    if (!inet_aton(ss->server_ip, &ss->addr.sin_addr)) {
        errormsg("Invalid IP address in config: %s", ss->server_ip);
    }
    return g_server_socket_count++;
}

int server_socket_bind_listen(ServerSocket *ss){
    if (!wait_for_bindable_port(&ss->addr, 10, 200)) {
        errormsg("Port %d is not available after retries", ss->port);
        return -1;
    }
    if (bind(ss->fd, (struct sockaddr *)&ss->addr, sizeof(ss->addr)) < 0) {
        perror("bind");
        errormsg("Failed to bind the socket on port %d", ss->port);
        return -1;
    }
    if (listen(ss->fd, ss->backlog) < 0) {
        perror("listen");
        errormsg("Failed to bind the socket on port %d", ss->port);
        return -2;
    }
    debugmsg("Listening on port %d at %s by protocol %s", ss->port, inet_ntoa(ss->addr.sin_addr), ss->protocol->label);
    return 0;
}

ContextServerData* onAcceptBasic(ServerSocket *ss, int fd, struct sockaddr_in *addr) {
    if (!ss) return NULL;
    debugmsg("Accepted connection on port %d from %s by protocol %s", ss->port, inet_ntoa(addr->sin_addr), ss->protocol->label);
    // context may depends on the protocol...
    ContextServerData *csd= malloc(sizeof(ContextServerData));
    if (!csd) return NULL;
    csd->next = NULL;
    ClientContext *ctx = &csd->cc;
    ctx->socket_fd = fd;
    inet_ntop(AF_INET,  &(addr->sin_addr), ctx->client_ip, sizeof(ctx->client_ip));

    ContextServerData **prev = &ss->first_ctx_data;
    while (*prev) {
        prev = &(*prev)->next;
    }
    *prev = csd; // this is the last one now.
    return csd;
}

ContextServerData* onAcceptHttp(ServerSocket *ss, int fd, struct sockaddr_in *addr) {
    return onAcceptBasic(ss, fd, addr);
}
ContextServerData* onAcceptWs(ServerSocket *ss,int fd, struct sockaddr_in *addr) {
    return onAcceptBasic(ss, fd, addr);
}
ContextServerData* onAcceptControl(ServerSocket *ss, int fd, struct sockaddr_in *addr) {
    return onAcceptBasic(ss, fd, addr);
}

void init_startup_server_sockets() {
    g_port_http = config_get_int("HTTP", "port", 8008);
    g_port_ws = config_get_int("WS", "port", 8010);
    g_port_control = config_get_int("CONTROL", "port", 8011);
    register_server_socket(g_port_http, &g_server_protocols[PROTOCOLID_HTTP], 16);
    register_server_socket(g_port_ws, &g_server_protocols[PROTOCOLID_WS], 16);
    register_server_socket(g_port_control, &g_server_protocols[PROTOCOLID_CONTROL], 4);
}

int main(int argc, char** argv)
{
    //quick check of the platform.
    assert(sizeof(pthread_t) <= sizeof(void*) );

    int opt;
    while ((opt = getopt(argc, argv, "hc:k")) != -1) {
        switch (opt) {
            case 'k':
                g_force_start_kill = 1;
                break;
            case 'h':
                printf("Usage: %s [-c config_file] [-h] [-k]\n"
                    "  -c config.ini : use this ini file\n"
                    "  -k : force start, kill the previous pid\n"
                    , argv[0]
                );
                exit(0);
            case 'c':
                if (optarg) {
                    strncpy(g_config_file, optarg, sizeof(g_config_file) - 1);
                    g_config_file[sizeof(g_config_file) - 1] = '\0';
                } else {
                    fprintf(stderr, "Missing argument for -c\n");
                    exit(1);
                }
                break;
            default:
                fprintf(stderr, "Usage: %s [-c config_file] [-h]\n", argv[0]);
                exit(1);
        }
    }

    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigterm_handler);
    signal(SIGUSR1, sigusr1_handler);
    signal(SIGUSR2, sigusr2_handler);
    setup_sigchld_handler();
    config_get_string("GEOD", "logfile", g_geod_logfile, sizeof(g_geod_logfile), GEOD_LOGFILE);
    config_get_string("GEOD", "plugin_dir", g_geod_plugin_dir, sizeof(g_geod_plugin_dir), PLUGIN_DIR);
    g_debug_msg_enabled= config_get_int("LOG", "debug_msg_enabled", 0);

    logstartup();
    cachesystem_init();
    
    atexit(remove_pidfile);
    write_pidfile_or_exit();
    
    http_init();
    cmd_init();
    server_init();

    plugin_scan_and_register();
    start_housekeeper();
    init_startup_server_sockets();

    for (size_t i = 0; i < g_server_socket_count; i++) {
        if (server_socket_bind_listen(&g_server_sockets[i]) < 0) {
            exit(1);
        }
    }

    while (keep_running) {
        fd_set fds;
        FD_ZERO(&fds);
        int max_fd = 0;
        for (size_t i = 0; i < g_server_socket_count; i++) {
            FD_SET(g_server_sockets[i].fd, &fds);
            if (g_server_sockets[i].fd > max_fd) max_fd = g_server_sockets[i].fd;
        }

        struct timeval timeout = {1, 0}; // 1 sec
        int ret = select(max_fd + 1, &fds, NULL, NULL, &timeout);

        if (ret > 0){
            for (size_t i = 0; i < g_server_socket_count; i++) {
                ServerSocket *ss = &g_server_sockets[i];
                const ServerProtocol *sp = ss->protocol;
                if (FD_ISSET(ss->fd, &fds)) {
                    struct sockaddr_in client_addr;
                    socklen_t addrlen = sizeof(client_addr);
                    int client_fd = accept(ss->fd, (struct sockaddr *)&client_addr, &addrlen);
                    if (client_fd < 0) {
                        logmsg("accept error: %s", strerror(errno));
                        continue;
                    }
                    int flags = fcntl(client_fd, F_GETFL, 0);
                    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
                    ContextServerData *csd = sp->on_accept(ss, client_fd, &client_addr);
                    if (csd){
                        ClientContext *ctx = &csd->cc;
                        char tname[16];
                        snprintf(tname, 16, "C%zu_%s", i, ctx->client_ip);
                        clock_gettime(CLOCK_MONOTONIC, &ctx->start_time);
                        pthread_create(&csd->thread_id, NULL, sp->on_process, ctx);
                        pthread_setname_np(csd->thread_id, tname);
                        pthread_detach(csd->thread_id);
                    }else{
                        // rejected by the protocol specific on_accept function based on rules..,
                        close(client_fd);
                    }
                }
            }
        } else if (ret == 0) {
            // timeout, restart loop, meanwhile we can check if keep_running is set to 0
        }
    }
    for (size_t i = 0; i < g_server_socket_count; i++) {
        ServerSocket *ss = &g_server_sockets[i];
        if (ss->fd != -1) {
            close(ss->fd);
        }
    }
    closeSo();
    stop_housekeeper();
    server_destroy();
    cmd_destroy();
    http_destroy();
    logmsg("GeoD shutdown.");
    return 0;
}
