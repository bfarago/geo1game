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
#include <dlfcn.h>
#include <png.h>
#include <fcntl.h>
#include <png.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include "global.h"
#include "plugin.h"
#include "http.h"
#include "config.h"


typedef struct {
    int interval;
} plugin_timer_t;

int file_exists(const char *path);
int file_exists_recent(const char *path, int interval);
void register_http_routes(PluginContext *ctx, int count, const char *routes[]);

void test_image(ClientContext *ctx, RequestParams *params);
void handle_status_json(ClientContext *ctx, RequestParams *params);
void handle_status_html(ClientContext *ctx, RequestParams *params);
void infopage(ClientContext *ctx, RequestParams *params);

int http_routes_count = 8;
const HttpRouteRule http_routes[] = {
    {"/test", test_image},
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
char g_cache_dir[MAX_PATH];
int g_http_port;
int g_debug_msg_enabled=0;

void sigusr1_handler(int signum) {
    (void)signum;
    reload_plugins = 1;
}
void sigint_handler(int signum) {
    (void)signum;
    keep_running = 0;
}
void sigterm_handler(int signum) {
    (void)signum;
    keep_running = 0;
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
void register_http_routes(PluginContext *ctx, int count, const char *routes[]) {
    ctx->http_route_count = count;
    ctx->http_routes = malloc(count * sizeof(HttpRouteRule));
    if (!ctx->http_routes) {
        logmsg("Failed to allocate memory for HTTP routes");
        return;
    }
    for (int i = 0; i < count; i++) {
        int len = strlen(routes[i]);
        ctx->http_routes[i] = malloc(len + 1);
        snprintf(ctx->http_routes[i], len + 1, "%s", routes[i]); // it could be also /plugin/pluginnname, etc...
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
    .http = {
        .register_http_route = register_http_routes,
        .send_response = send_response,
        .send_file = send_file,
        .send_chunk_head = send_chunk_head,
        .send_chunk_end = send_chunk_end,
        .send_chunks = send_chunks
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
    if (pc->plugin_init(pc, &g_plugin_host)) {
        logmsg("Failed to initialize plugin: %s", filename);
        dlclose(pc->handle);
        return -1;
    }
    pc->last_used = time(NULL);
    return 0;
}

int plugin_unload(int id){
    PluginContext *pc=&g_Plugins[id];
    if (pc->handle) {
        if (pc->plugin_finish) {
            pc->plugin_finish(pc);
        }
        dlclose(pc->handle);
        pc->handle = NULL;
        pc->plugin_init = NULL;
        pc->plugin_finish = NULL;
        pc->plugin_register = NULL;
        pc->http.request_handler = NULL;
    }else{
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
            pc->http_route_count=0;
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
void *housekeeper_thread(void *arg) {
    (void)arg; // suppress unused parameter warning
    while (g_housekeeper.running) {
        time_t now = time(NULL);
        housekeeper_plugins(now);
        housekeeper_mapgen(now);
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

void handle_status_html(ClientContext *ctx, RequestParams *params) {
    const char *html_head = "<html><body><h1>GeoD Status</h1><p>Server is running.</p>";
    const char *html_end =  "</body></html>";
    char html[4096];
    int offset = 0;
    offset += snprintf(html + offset, sizeof(html) - offset, "%s", html_head);
    offset += snprintf(html + offset, sizeof(html) - offset, "<p>Client IP: %s</p>", ctx->client_ip);
    offset += snprintf(html + offset, sizeof(html) - offset, "<p>Request Path: %s</p>", params->path);
    offset += snprintf(html + offset, sizeof(html) - offset, "<p>Request Params:</p><ul>");
    for (int i = 0; i < ctx->request.query_count; i++) {
        offset += snprintf(html + offset, sizeof(html) - offset, "<li>%s: %s</li>", ctx->request.query[i].key, ctx->request.query[i].value);
    }
    offset += snprintf(html + offset, sizeof(html) - offset, "</ul>");
    offset += snprintf(html + offset, sizeof(html) - offset, "<p>Request Headers:</p><ul>");
    for (int i = 0; i < ctx->request.header_count; i++) {
        offset += snprintf(html + offset, sizeof(html) - offset, "<li>%s: %s</li>", ctx->request.headers[i].key, ctx->request.headers[i].value);
    }
    offset += snprintf(html + offset, sizeof(html) - offset, "</ul>");
    offset += snprintf(html + offset, sizeof(html) - offset, "<p>Request Method: %s</p>", ctx->request.method);
    offset += snprintf(html + offset, sizeof(html) - offset, "<p>Plugins Loaded:</p><ul>");
    for (int i = 0; i < g_PluginCount; i++) {
        PluginContext *pc = &g_Plugins[i];
        if (pc->handle) {
            offset += snprintf(html + offset, sizeof(html) - offset, "<li>%d: %s  (used:%d) ", pc->id, pc->name, pc->used_count );
            for (int j = 0; j < pc->http_route_count; j++) {
                offset += snprintf(html + offset, sizeof(html) - offset, "[<a href=\"%s\">%s</a>] ", pc->http_routes[j], pc->http_routes[j] );
            }
            offset += snprintf(html + offset, sizeof(html) - offset, "</li>");
        }
    }
    offset += snprintf(html + offset, sizeof(html) - offset, "</ul>");
    offset += snprintf(html + offset, sizeof(html) - offset, "<p>Plugins not loaded:</p><ul>");
    for (int i = 0; i < g_PluginCount; i++) {
        PluginContext *pc = &g_Plugins[i];
        if (!pc->handle) {
            offset += snprintf(html + offset, sizeof(html) - offset, "<li>%d: %s ", pc->id, pc->name );
            for (int j = 0; j < pc->http_route_count; j++) {
                offset += snprintf(html + offset, sizeof(html) - offset, "[<a href=\"%s\">%s</a>] ", pc->http_routes[j], pc->http_routes[j] );
            }
            offset += snprintf(html + offset, sizeof(html) - offset, "</li>");
        }
    }
    offset += snprintf(html + offset, sizeof(html) - offset, "</ul>");
    offset += snprintf(html + offset, sizeof(html) - offset, "<p>Mapgen Loaded: %s</p>", g_housekeeper.mapgen_loaded ? "Yes" : "No");
    
    offset += snprintf(html + offset, sizeof(html) - offset, "%s", html_end);
    send_response(ctx->socket_fd, 200, "text/html", html);
    logmsg("%s status_html request", ctx->client_ip);
}

void handle_status_json(ClientContext *ctx, RequestParams *params) {
    (void)params; // suppress unused parameter warning
    const char *json = "{\"status\":\"running\"}";
    send_response(ctx->socket_fd, 200, "application/json", json);
    //logmsg("%s status_json request", ctx->client_ip);
}

void infopage(ClientContext *ctx, RequestParams *params) {
    (void)params; // suppress unused parameter warning
    dprintf(ctx->socket_fd,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<!DOCTYPE html>"
        "<html><head><title>GeoD</title>"
        "<style>body { background-color: black; color: white; text-align: center; }</style>"
        "</head><body>"
        "<h1>GeoD Preview</h1>"
        "<p><img src=\"clouds?width=720&height=360\" alt=\"Clouds\"></p>"
        "<p><img src=\"biome?width=720&height=360\" alt=\"Biome\"></p>"
        "<p><img src=\"elevation?width=720&height=360\" alt=\"Elevation\"></p>"
        "</body></html>"
    );
}
void test_image(ClientContext *ctx, RequestParams *params) {
    char filename[MAX_PATH];
    snprintf(filename, sizeof(filename), "%s/test_lat%.2f_lon%.2f_%dx%d.png",
        g_cache_dir,
        params->lat_min, params->lon_min, 
        params->width, params->height);
    if (!file_exists_recent(filename, CACHE_TIME)) {
        logmsg("Generating new biome PNG: %s", filename);
        if (start_map_context()) {
            logmsg("Failed to start map context");
            send_response(ctx->socket_fd, 500, "text/plain", "Failed to start map context\n");
            return;
        }
        PluginContext *pcimg= get_plugin_context("image");
        if (pcimg){
            image_context_start(pcimg);
            Image img;
            if (image_create(pcimg, &img, "test.png", 320, 200, ImageBackend_Png, ImageFormat_RGB, ImageBuffer_AoS)){
                for (unsigned int y = 0; y < img.height; y++) {
                    png_bytep row = malloc(3 * img.width);
                    float lat = params->lat_max - ((params->lat_max - params->lat_min) / img.height) * y;
                    for (unsigned int x = 0; x < img.width; x++) {
                        float lon = params->lon_min + ((params->lon_max - params->lon_min) / img.width) * x;
                        TerrainInfo info = map_context.get_info(lat, lon);
                        row[x*3 + 0] = info.r;
                        row[x*3 + 1] = info.g;
                        row[x*3 + 2] = info.b;
                    }
                    image_write_row(pcimg, &img, row); //png_write_row(img.png_ptr, row);
                    free(row);
                }
                image_destroy(pcimg, &img);  // PngImage_finish(&img);
                logmsg("Test PNG generated: %s", filename);
            }
            image_context_stop(pcimg);
        }
        stop_map_context();
    } else {
        logmsg("Using cached test PNG: %s", filename);
    }
    send_file(ctx->socket_fd, "image/png", filename);
}

void *http_handle_client(void *arg) {
    ClientContext *ctx = (ClientContext *)arg;
    int len = read(ctx->socket_fd, ctx->request_buffer, BUF_SIZE - 1);
    if (len <= 0) {
        close(ctx->socket_fd);
        free(ctx);
        return NULL;
    }
    ctx->request_buffer[len] = '\0';
    http_parse_request(ctx, &ctx->request);
    char *xfor = strcasestr(ctx->request_buffer, "x-forwarded-for:");
    if (xfor) {
        xfor += strlen("x-forwarded-for:");
        while (*xfor == ' ') xfor++;
        char *end = strchr(xfor, '\r');
        if (end) *end = '\0';
        strncpy(ctx->client_ip, xfor, sizeof(ctx->client_ip) - 1);
    }

    RequestParams params;
    parse_request_path_and_params(ctx->request_buffer, &params);
    // Handle specific paths
    for (int i = 0; i < http_routes_count; i++) {
        if (strcmp(http_routes[i].path, params.path) == 0) {
            http_routes[i].handler(ctx, &params);
            close(ctx->socket_fd);
            free(ctx);
            return NULL;
        }
    }
    for (int i=0; i<g_PluginCount; i++) {
        PluginContext *pc=&g_Plugins[i];
        if (pc->http_route_count == 0) continue;
        for (int j=0; j<pc->http_route_count; j++) {
            if (strcmp(pc->http_routes[j], params.path) == 0) {
                if (!plugin_start(i)){
                    pc->http.request_handler(pc, ctx, &params);
                    plugin_stop(i);
                    close(ctx->socket_fd);
                    free(ctx);
                    return NULL;
                } else {
                    errormsg("Plugin %s is busy", pc->name);
                    dprintf(ctx->socket_fd, "HTTP/1.1 503 Service Unavailable\r\n\r\n");
                    close(ctx->socket_fd);
                    free(ctx);
                    return NULL;
                }
            }
        }
    }
    // Handle unknown paths
    dprintf(ctx->socket_fd, "HTTP/1.1 404 Not Found\r\n\r\n");
    close(ctx->socket_fd);
    free(ctx);
    return NULL;
}

// Initialize cache directory: create if missing, cleanup old cache files
void cache_init(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        if (mkdir(path, 0755) != 0) {
            errormsg("Failed to create cache directory: %s", path);
            return;
        } else {
            logmsg("Created cache directory: %s", path);
            return;
        }
    }

    DIR *dir = opendir(path);
    if (!dir) {
        errormsg("Cannot open cache directory: %s", path);
        return;
    }
    if (config_get_int("CACHE", "cleanup_on_start",1)){
        struct dirent *entry;
        char filepath[MAX_PATH];
        int count_deleted = 0;
        int count_failed = 0;
        int count_others = 0;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type != DT_REG) continue;
            const char *name = entry->d_name;
            if (strstr(name, ".png") || strstr(name, ".png_") || strstr(name, ".json") || strstr(name, ".html")) {
                snprintf(filepath, sizeof(filepath), "%s/%s", path, name);
                if (unlink(filepath) == 0) {
                    count_deleted++;
                    // logmsg("Deleted cache file: %s", filepath);
                } else {
                    count_failed++;
                    debugmsg("Failed to delete file: %s", filepath);
                }
            }else {
                count_others++;
            }
        }
        if (count_deleted + count_failed + count_others > 0) {
            logmsg("Cleanup on start: Deleted %d cache files, failed to delete %d cache files, and %d other files", count_deleted, count_failed, count_others);
        }
    }
    closedir(dir);
}

int main() {
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigterm_handler);
    signal(SIGUSR1, sigusr1_handler);

    char cwd[MAX_PATH];
    getcwd(cwd, sizeof(cwd));
    config_get_string("CACHE", "dir", g_cache_dir, MAX_PATH, CACHE_DIR);
    g_http_port = config_get_int("HTTP", "port", PORT);
    g_debug_msg_enabled= config_get_int("LOG", "debug_msg_enabled", 0);

    logmsg("GeoD starting on port %d in working dir %s", g_http_port, cwd);
    cache_init(g_cache_dir);
    
    atexit(remove_pidfile);
    write_pidfile_or_exit();
    

    plugin_scan_and_register();
    start_housekeeper();

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_http_port);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }
    if (listen(server_socket, 8) < 0) {
        perror("listen");
        exit(1);
    }
    fcntl(server_socket, F_SETFL, O_NONBLOCK);

    while (keep_running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(server_socket, &fds);

        struct timeval timeout = {1, 0}; // 1 sec
        int ret = select(server_socket + 1, &fds, NULL, NULL, &timeout);

        if (ret > 0 && FD_ISSET(server_socket, &fds)) {
            struct sockaddr_in client_addr;
            socklen_t addrlen = sizeof(client_addr);
            int client_fd = accept(server_socket, (struct sockaddr *)&client_addr, &addrlen);
            if (client_fd < 0) continue;
            ClientContext *ctx = malloc(sizeof(ClientContext));
            ctx->socket_fd = client_fd;
            inet_ntop(AF_INET, &client_addr.sin_addr, ctx->client_ip, sizeof(ctx->client_ip));
            pthread_t thread_id;
            pthread_create(&thread_id, NULL, http_handle_client, ctx);
            pthread_detach(thread_id);
        } else if (ret == 0) {
            // timeout, restart loop, meanwhile we can check if keep_running is set to 0
        }
    }
    close(server_socket);
    closeSo();
    stop_housekeeper();
    logmsg("GeoD shutdown.");
    return 0;
}
