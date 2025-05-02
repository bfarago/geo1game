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
#include "global.h"
#include "plugin.h"
#include "http.h"
#include "config.h"
#include "cache.h"
#include "handlers.h"

typedef struct {
    int interval;
} plugin_timer_t;

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


#define MAX_SERVER_SOCKETS 4
ServerSocket g_server_sockets[MAX_SERVER_SOCKETS];
int g_server_socket_count = 0;
// void register_http_routes(PluginContext *ctx, int count, const char *routes[]);

ContextServerData* onAcceptHttp(ServerSocket* ss, int client_fd, struct sockaddr_in *addr);
ContextServerData* onAcceptWs(ServerSocket* ss, int client_fd, struct sockaddr_in *addr);
ContextServerData* onAcceptControl(ServerSocket* ss, int client_fd, struct sockaddr_in *addr);
//void* onProcessHttp(ClientContext *ctx);
void* onProcessWs(void *arg);
void* onProcessControl(void *arg);

int http_routes_count = 4;
const HttpRouteRule http_routes[] = {
    {"/testimage", test_image},
    {"/status.html", handle_status_html},
    {"/status.json", handle_status_json},
    {"/", infopage},
};


/** unix signal handlers and process id save */
volatile sig_atomic_t keep_running = 1;
volatile sig_atomic_t reload_plugins = 0;

MapContext map_context;

pthread_mutex_t plugin_housekeeper_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    pthread_t thread_id;
    unsigned char running;
    unsigned char mapgen_loaded;
    time_t last_mapgen_use;
} housekeeper_t;

housekeeper_t g_housekeeper;


int g_PluginCount = 0;
PluginContext g_Plugins[MAX_PLUGIN];

int g_port_http;
int g_port_ws;
int g_port_control;
int g_debug_msg_enabled=0;

void sigusr1_handler(int signum) {
    (void)signum;
    reload_plugins = 1;
    g_debug_msg_enabled= config_get_int("LOG", "debug_msg_enabled", 0);
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
void write_pidfile_or_exit() {
    FILE *fp = fopen(GEOD_PIDFILE, "r");
    if (fp) {
        pid_t old_pid;
        if (fscanf(fp, "%d", &old_pid) == 1) {
            if (kill(old_pid, 0) == 0) {
                fprintf(stderr, "GeoD already running with PID %d\n", old_pid);
                fclose(fp);
                exit(1);
            }
        }
        fclose(fp);
    }

    fp = fopen(GEOD_PIDFILE, "w");
    if (!fp) {
        perror("Cannot write PID file");
        exit(1);
    }
    fprintf(fp, "%d\n", getpid());
    fclose(fp);
}
void remove_pidfile() {
    unlink(GEOD_PIDFILE);
}

/** Log a message to the log file. */
void logmsg(const char *fmt, ...) {
    FILE *log = fopen(GEOD_LOGFILE, "a");
    if (!log) return;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(log, "%04d-%02d-%02d %02d:%02d:%02d ",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);
    va_list args;
    va_start(args, fmt);
    vfprintf(log, fmt, args);
    va_end(args);
    fprintf(log, "\n");
    fclose(log);
}
void errormsg(const char *fmt, ...) {
    FILE *log = fopen(GEOD_LOGFILE, "a");
    if (!log) return;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(log, "%04d-%02d-%02d %02d:%02d:%02d ERROR: ",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);
    va_list args;
    va_start(args, fmt);
    vfprintf(log, fmt, args);
    va_end(args);
    fprintf(log, "\n");
    fclose(log);
}

void debugmsg(const char *fmt, ...) {
    if (g_debug_msg_enabled){
        FILE *log = fopen(GEOD_LOGFILE, "a");
        if (!log) return;
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        fprintf(log, "%04d-%02d-%02d %02d:%02d:%02d DEBUG: ",
                t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                t->tm_hour, t->tm_min, t->tm_sec);
        va_list args;
        va_start(args, fmt);
        vfprintf(log, fmt, args);
        va_end(args);
        fprintf(log, "\n");
        fclose(log);
    }
}

int geod_mutex_timedlock(pthread_mutex_t *lock, const unsigned long timeout_ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    int res= pthread_mutex_timedlock(lock, &ts);
    if (res == ETIMEDOUT) {
        errormsg("Mutex lock timed out");
    } else if (res != 0) {
        errormsg("Mutex lock failed: %d", res);
    }   
    return res;
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

void plugin_start_timer(PCHANDLER pc, int interval, void (*callback)(PCHANDLER)){
    (void)pc;
    (void)interval;
    (void)callback;

}
void plugin_stop_timer(PCHANDLER pc){
    (void)pc;
    // Stop the timer for the plugin
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
void register_http_routes(PluginContext *pc, int count, const char *routes[]) {
    HttpCapabilities *cap = &pc->http_caps;
    cap->http_route_count = count;
    cap->http_routes = malloc(count * sizeof(HttpRouteRule));
    if (!cap->http_routes) {
        logmsg("Failed to allocate memory for HTTP routes");
        return;
    }
    for (int i = 0; i < count; i++) {
        int len = strlen(routes[i]);
        cap->http_routes[i] = malloc(len + 1);
        snprintf(cap->http_routes[i], len + 1, "%s", routes[i]); // it could be also /plugin/pluginnname, etc...
    }
}
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
void register_control_routes(PluginContext *ctx, int count, const char *routes[]) {
    ControlCapabilities *cap = &ctx->control_caps;
    cap->route_count = count;
    cap->routes = malloc(count * sizeof(ControlRouteRule));
    if (!cap->routes) {
        logmsg("Failed to allocate memory for CONTROL routes");
        return;
    }
    for (int i = 0; i < count; i++) {
        int len = strlen(routes[i]);
        cap->routes[i] = malloc(len + 1);
        snprintf(cap->routes[i], len + 1, "%s", routes[i]); // it could be also /plugin/pluginnname, etc...
    }
}
void register_db_queue(PluginContext *ctx, const char *db){
    //placeholder function
    (void)ctx;
    (void)db;
}

PluginContext* get_plugin_context(const char *name){
    if (!name) return NULL;
    char pname[MAX_PATH];
    snprintf(pname, sizeof(pname), "%s.so", name);
    for (int i=0; i<g_PluginCount; i++){
        if (!strcmp(g_Plugins[i].name, pname)){
            return &g_Plugins[i];
        }
    }
    return NULL;
}
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
PluginHostInterface g_plugin_host = {
    .get_plugin_context = get_plugin_context,
    .start_timer = (void*)plugin_start_timer,
    .stop_timer = (void*)plugin_stop_timer,
    .logmsg = logmsg,
    .errormsg = errormsg,
    .debugmsg = debugmsg,
    .file_exists = file_exists,
    .file_exists_recent = file_exists_recent,
    .config_get_string = config_get_string,
    .config_get_int = config_get_int,
    .register_db_queue = register_db_queue,
    .server = {
        .register_http_route = register_http_routes,
        .register_control_route = register_control_routes
    },
    .http = {
        .send_response = send_response,
        .send_file = send_file,
        .send_chunk_head = send_chunk_head,
        .send_chunk_end = send_chunk_end,
        .send_chunks = send_chunks
    },
    .ws = {
        .handshake = ws_hostside_handshake,
    },
    .map = {
        .start_map_context= start_map_context,
        .stop_map_context = stop_map_context,
        .get_map_info = get_map_info
    },
    .image = {
        .context_start = image_context_start,
        .context_stop = image_context_stop,
        .create = image_create,
        .destroy = image_destroy,
        .get_buffer = image_get_buffer,
        .write_row = image_write_row
    },
    .cache = {
        .get_dir = cache_get_dir,
        .file_init = cache_file_init,
        .file_create = cache_file_create,
        .file_close = cache_file_close,
        .file_remove = cache_file_remove,
        .file_rename = cache_file_rename,
        .file_exists_recent = cache_file_exists_recent,
        .file_write = cache_file_write,
    }
};

int plugin_load(const char *name, int id){
    PluginContext *pc=&g_Plugins[id];
    
    char filename[MAX_PLUGIN_PATH];
    snprintf(filename, sizeof(filename), "%s/%s", PLUGIN_DIR, name);

    struct stat st;
    if (stat(filename, &st) != 0) {
        logmsg("Failed to stat plugin file: %s", filename);
        return -1;
    }
    pc->file_mtime = (unsigned long)st.st_mtime;
    
    pc->handle = dlopen(filename, RTLD_LAZY);
    if (!pc->handle) {
        logmsg("Failed to load plugin: %s", filename);
        return -1;
    }
    pc->plugin_init = (plugin_init_t)dlsym(pc->handle, "plugin_init");
    if (!pc->plugin_init) {
        logmsg("Failed to find plugin_init in %s", filename);
        dlclose(pc->handle);
        return -1;
    }
    pc->plugin_finish = (plugin_finish_t)dlsym(pc->handle, "plugin_finish");
    if (!pc->plugin_finish) {
        logmsg("Failed to find plugin_finish in %s", filename);
        dlclose(pc->handle);
        return -1;
    }
    //thread related functions are optional
    pc->thread_init = (plugin_thread_init_t)dlsym(pc->handle, "plugin_thread_init");
    pc->thread_finish = (plugin_thread_finish_t)dlsym(pc->handle, "plugin_thread_finish");
    
    pc->plugin_register = (plugin_register_t)dlsym(pc->handle, "plugin_register"); 
    if (!pc->plugin_register) {
        logmsg("Failed to find plugin_register in %s", filename);
        dlclose(pc->handle);
        return -1;
    }
    // Resolve generic plugin event handler (optional)
    pc->plugin_event = (plugin_event_handler_t)dlsym(pc->handle, "plugin_event");
    if (pc->plugin_init(pc, &g_plugin_host)) {
        logmsg("Failed to initialize plugin: %s", filename);
        dlclose(pc->handle);
        return -1;
    }
    pc->last_used = time(NULL);
    return 0;
}

int plugin_unload(int id){
    PluginContext *pc = &g_Plugins[id];
    if (pc->handle) {
        // Call plugin_event(PLUGIN_EVENT_STANDBY) if present before finish
        if (pc->plugin_event) {
            PluginEventContext evctx = {0};
            int ev = pc->plugin_event(pc, PLUGIN_EVENT_STANDBY, &evctx);
            if (ev != 0) {
                debugmsg("Plugin %s requested delay for standby", pc->name);
                return -1;
            }
        }
        if (pc->plugin_finish) {
            pc->plugin_finish(pc);
        }
        dlclose(pc->handle);
        pc->handle = NULL;
        pc->plugin_init = NULL;
        pc->plugin_finish = NULL;
        pc->plugin_register = NULL;
        pc->http.request_handler = NULL;
        pc->plugin_event = NULL;
    } else {
        logmsg("Plugin not loaded");
        return -1;
    }
    return 0;
}
int plugin_start(int id) {
    PluginContext *pc = &g_Plugins[id];
    if (!pc->handle) {
        if (plugin_load(pc->name, id)){
            logmsg("Plugin not loaded: %s", pc->name);
            return -1;
        }
    }
    int res = geod_mutex_timedlock(&plugin_housekeeper_mutex, 20000UL);
    if (res == 0) {
        pc->last_used = time(NULL);
        pc->used_count++;
        pthread_mutex_unlock(&plugin_housekeeper_mutex);
    }
    if (pc->thread_init){
        if (pc->thread_init(pc)) {
            logmsg("Failed to initialize plugin thread: %s", pc->name);
            return -1;
        }
    }
    return 0;
}
void plugin_stop(int id) {
    PluginContext *pc = &g_Plugins[id];
    if (pc->handle) {
        if (pc->thread_finish){
            if (pc->thread_finish(pc)) {
                logmsg("Failed to finish plugin thread: %s", pc->name);
                //return;
            }
        }
    
        int res = geod_mutex_timedlock(&plugin_housekeeper_mutex, 20000UL);
        if (res == 0) {
            pc->last_used = time(NULL);
            pc->used_count--;
            pthread_mutex_unlock(&plugin_housekeeper_mutex);
        }
    }
}

void plugin_scan_and_register() {
    DIR *dir = opendir(PLUGIN_DIR);
    if (!dir) {
        errormsg("Cannot open plugin directory: %s", PLUGIN_DIR);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strstr(entry->d_name, ".so")) continue;
        // Ellenőrizzük, hogy már ismerjük-e
        int known = 0;
        for (int i = 0; i < g_PluginCount; i++) {
            if (strcmp(g_Plugins[i].name, entry->d_name) == 0) {
                known = 1;
                break;
            }
        }

        if (!known && g_PluginCount < MAX_PLUGIN) {
            PluginContext *pc = &g_Plugins[g_PluginCount];
            pc->http_caps.http_route_count=0;
            int res = plugin_load(entry->d_name, g_PluginCount);
            strncpy(pc->name, entry->d_name, sizeof(pc->name) - 1);
            pc->id = g_PluginCount;
            if (res == 0) {
                int rr = pc->plugin_register(pc, &g_plugin_host);
                if (rr == 0) {
                    debugmsg("Plugin registered: %s", entry->d_name);
                } else {
                    errormsg("Plugin registration failed: %s", entry->d_name);
                }
            } else {
                errormsg("Plugin load failed: %s", entry->d_name);
            }
            g_PluginCount++;
        }
    }
    closedir(dir);
}
int housekeeper_is_running(void){
    return g_housekeeper.running;
}
int housekeeper_is_mapgen_loaded(void){
    return g_housekeeper.mapgen_loaded;
}
void housekeeper_plugins(time_t now ){
    for (int i = 0; i < g_PluginCount; i++) {
        PluginContext *pc = &g_Plugins[i];
        if (pc->handle) {
            if (pc->used_count <= 0) {
                time_t last = (time_t)pc->last_used;
                if (difftime(now, last) > PLUGIN_IDLE_TIMEOUT) {
                    plugin_unload(i);
                    debugmsg("Plugin unloaded: %s", pc->name);
                }
            }
        }
    }
    if (reload_plugins) {
        logmsg("Received SIGUSR1 — rescanning plugins...");
        plugin_scan_and_register();
        reload_plugins = 0;
    }
}
void housekeeper_mapgen(time_t now ){
    if (g_housekeeper.mapgen_loaded) {
        if (g_housekeeper.last_mapgen_use + MAPGEN_IDLE_TIMEOUT < now) {
            debugmsg("Mapgen idle timeout. Unloading mapgen");
            if (map_context.mapgen_finish) {
                map_context.mapgen_finish();
                closeSo();
            }
        }
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

void housekeeper_server_clients(time_t now ){
    (void)now; // suppress unused parameter warning (now is passed to housekeeper_server_clients 
    #if (0)
    if (g_debug_msg_enabled){
        for(int i=0; i<g_server_socket_count; i++) {
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
    for(int i=0; i<g_server_socket_count; i++) {
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
            logmsg("%s Server %s:%d Clients stats: (alive, ok, failed, exec, cpu) min (%d, %d, %d, %dms, %0.2f%%) max (%d, %d, %d, %dms, %0.2f%%)",
            ss->protocol->label, ss->server_ip, ss->port,
            ss->stat_alive.min5sm, ss->stat_finished.min5sm, ss->stat_failed.min5sm, ss->stat_exectime.min5sm, ss->stat_exectime.min5sm/50.0,
            ss->stat_alive.max5sm, ss->stat_finished.max5sm, ss->stat_failed.max5sm, ss->stat_exectime.max5sm, ss->stat_exectime.max5sm/50.0);
            ss->execution_time_5s = 0.0;
        }
    }
    if (g_counter5s >= SD_MAX_COUNT_5S){
        g_counter5s = 0;
    }else{
        g_counter5s++;
    }
    
}

void *housekeeper_thread(void *arg) {
    (void)arg; // suppress unused parameter warning
    while (g_housekeeper.running) {
        time_t now = time(NULL);
        housekeeper_plugins(now);
        housekeeper_mapgen(now);
        housekeeper_server_clients(now);
        for (int i = 0; i<5; i++) {
            if (g_housekeeper.running) {
                sleep(1);
            } else {
                break;
            }
        }
    }
    return NULL;
}

void start_housekeeper() {
    g_housekeeper.running = 1U;
    g_housekeeper.mapgen_loaded = 0U;
    if (pthread_create(&g_housekeeper.thread_id, NULL, housekeeper_thread, NULL) != 0) {
        perror("Failed to create housekeeper thread");
        exit(1);
    }
}

void stop_housekeeper() {
    g_housekeeper.running = 0U;
    pthread_join(g_housekeeper.thread_id, NULL);
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

void *onProcessControl(void *arg) {
    ClientContext *ctx = (ClientContext *)arg;
    
    int keep_processing = 1;
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
    close_ClientContext(ctx);
    return NULL;
}
void *onProcessWs(void *arg){
    ClientContext *ctx = (ClientContext *)arg;
    close_ClientContext(ctx);
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
    // Handle specific paths
    for (int i = 0; i < http_routes_count; i++) {
        if (strcmp(http_routes[i].path, params.path) == 0) {
            http_routes[i].handler(ctx, &params);
            if (ctx->result_status == CTX_RUNNING) ctx->result_status = CTX_FINISHED_OK;
            close_ClientContext(ctx);
            return NULL;
        }
    }
    for (int i=0; i<g_PluginCount; i++) {
        PluginContext *pc=&g_Plugins[i];
        if (pc->http_caps.http_route_count == 0) continue;
        for (int j=0; j<pc->http_caps.http_route_count; j++) {
            if (strcmp(pc->http_caps.http_routes[j], params.path) == 0) {
                if (!plugin_start(i)){
                    pc->http.request_handler(pc, ctx, &params);
                    plugin_stop(i);
                    if (ctx->result_status == CTX_RUNNING) ctx->result_status = CTX_FINISHED_OK;
                    close_ClientContext(ctx);
                    // logmsg("Plugin %s closed the TCP socket", pc->name);
                    return NULL;
                } else {
                    errormsg("Plugin %s is busy", pc->name);
                    dprintf(ctx->socket_fd, "HTTP/1.1 503 Service Unavailable\r\n\r\n");
                    ctx->result_status = CTX_ERROR;
                    close_ClientContext(ctx);
                    return NULL;
                }
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

int main() {
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigterm_handler);
    signal(SIGUSR1, sigusr1_handler);
    setup_sigchld_handler();

    g_debug_msg_enabled= config_get_int("LOG", "debug_msg_enabled", 0);

    logstartup();
    cachesystem_init();
    
    atexit(remove_pidfile);
    write_pidfile_or_exit();
    

    plugin_scan_and_register();
    start_housekeeper();
    init_startup_server_sockets();

    for (int i = 0; i < g_server_socket_count; i++) {
        if (server_socket_bind_listen(&g_server_sockets[i]) < 0) {
            exit(1);
        }
    }

    while (keep_running) {
        fd_set fds;
        FD_ZERO(&fds);
        int max_fd = 0;
        for (int i = 0; i < g_server_socket_count; i++) {
            FD_SET(g_server_sockets[i].fd, &fds);
            if (g_server_sockets[i].fd > max_fd) max_fd = g_server_sockets[i].fd;
        }

        struct timeval timeout = {1, 0}; // 1 sec
        int ret = select(max_fd + 1, &fds, NULL, NULL, &timeout);

        if (ret > 0){
            for (int i = 0; i < g_server_socket_count; i++) {
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
                    
                    ContextServerData *csd = sp->on_accept(ss, client_fd, &client_addr);
                    if (csd){
                        ClientContext *ctx = &csd->cc;
                        clock_gettime(CLOCK_MONOTONIC, &ctx->start_time);
                        pthread_create(&csd->thread_id, NULL, sp->on_process, ctx);
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
    for (int i = 0; i < g_server_socket_count; i++) {
        ServerSocket *ss = &g_server_sockets[i];
        if (ss->fd != -1) {
            close(ss->fd);
        }
    }
    closeSo();
    stop_housekeeper();
    logmsg("GeoD shutdown.");
    return 0;
}
