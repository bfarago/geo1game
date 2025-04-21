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
#include "http.h"
#include "plugin.h"

void register_http_routes(PluginContext *ctx, int count, const char *routes[]);

volatile sig_atomic_t keep_running = 1;
volatile sig_atomic_t reload_plugins = 0;

void sigusr1_handler(int signum) {
    (void)signum;
    reload_plugins = 1;
}
void sigint_handler(int signum) {
    (void)signum; // suppress unused parameter warning
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

typedef struct {
    float elevation;
    unsigned char r, g, b;
    unsigned char precip, temp;
} TerrainInfo;

typedef TerrainInfo (*get_terrain_info_t)(float, float);

typedef void (*mapgen_finish_t)(void);
typedef int (*mapgen_init_t)(void);
typedef struct{
    void *lib_handle;
    get_terrain_info_t get_info;
    mapgen_finish_t mapgen_finish;
    mapgen_init_t mapgen_init;
} MapContext;
MapContext map_context;

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
        logmsg("Mutex lock timed out");
    } else if (res != 0) {
        logmsg("Mutex lock failed: %d", res);
    }   
    return res;
}

pthread_mutex_t plugin_housekeeper_mutex = PTHREAD_MUTEX_INITIALIZER;

#if PNG_LIBPNG_VER >= 10600
    // libpng 1.6.40 and later
    #define HAVE_OLD_PNG 0
    #define PNG_CRITICAL_START
    #define PNG_CRITICAL_END
#else
    // libpng 1.4.0 and earlier
    #define HAVE_OLD_PNG 1
    // multithread safe before v1.4.0 see github issue
    pthread_mutex_t png_mutex = PTHREAD_MUTEX_INITIALIZER;
    #define PNG_CRITICAL_START() geod_mutex_timedlock(&png_mutex, 20000UL)
    #define PNG_CRITICAL_END() pthread_mutex_unlock(&png_mutex)
#endif

typedef struct {
    unsigned long width, height;
    FILE *fp;
    png_structp png_ptr;
    png_infop info_ptr;
    const char *filename;
    char tmp_filename[128];
} PngImage;

#define MAPGEN_IDLE_TIMEOUT 60

typedef struct {
    pthread_t thread_id;
    unsigned char running;
    unsigned char mapgen_loaded, png_loaded, sqlite_loaded, mysql_loaded;
    time_t last_mapgen_use;
} housekeeper_t;

housekeeper_t g_housekeeper;
void loadSo(){
    map_context.lib_handle = dlopen("./libmapgen_c.so", RTLD_LAZY);
    if (!map_context.lib_handle) {
        perror("dlopen");
        exit(1);
    }
    map_context.get_info = (get_terrain_info_t)dlsym(map_context.lib_handle, "mapgen_get_terrain_info");
    if (!map_context.get_info) {
        perror("dlsym");
        exit(1);
    }
    map_context.mapgen_finish = (mapgen_finish_t)dlsym(map_context.lib_handle, "mapgen_finish");
    if (!map_context.mapgen_finish) {
        perror("dlsym");
        exit(1);
    }
    map_context.mapgen_init = (mapgen_init_t)dlsym(map_context.lib_handle, "mapgen_init");
    if (!map_context.mapgen_init) {
        perror("dlsym");
        exit(1);
    }
    g_housekeeper.mapgen_loaded = 1U;
    g_housekeeper.last_mapgen_use = time(NULL);
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
void send_response(int client, int status_code, const char *content_type, const char *body) {
    const char *status_text;
    switch (status_code) {
        case 200: status_text = "OK"; break;
        case 404: status_text = "Not Found"; break;
        case 500: status_text = "Internal Server Error"; break;
        default: status_text = "OK"; break;
    }
    dprintf(client, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n\r\n",
        status_code, status_text, content_type, strlen(body));
    write(client, body, strlen(body));
}
void send_file(int client, const char *content_type, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        dprintf(client, "HTTP/1.1 404 Not Found\r\n\r\n");
        return;
    }
    dprintf(client, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n\r\n", content_type);
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) write(client, buf, n);
    fclose(f);
}


typedef struct {
    int interval;
} plugin_timer_t;

int file_exists_recent(const char *path, int interval);
void plugin_start_timer(PCHANDLER pc, int interval, void (*callback)(PCHANDLER)){
    (void)pc;
    (void)interval;
    (void)callback;

}
void plugin_stop_timer(PCHANDLER pc){
    (void)pc;
    // Stop the timer for the plugin
}

#define MAX_PLUGIN 10
int g_PluginCount = 0;
PluginContext g_Plugins[MAX_PLUGIN];
const PluginHostInterface g_plugin_host = {
    .register_http_route = (void*)register_http_routes,
    .get_plugin_context = NULL,
    .start_timer = (void*)plugin_start_timer,
    .stop_timer = (void*)plugin_stop_timer,
    .logmsg = logmsg,
    .send_response = send_response,
    .send_file = send_file,
    .file_exists_recent = file_exists_recent
};

int plugin_load(const char *name, int id){
    PluginContext *pc=&g_Plugins[id];
    
    char filename[512];
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
    return 0;
}
void plugin_stop(int id) {
    PluginContext *pc = &g_Plugins[id];
    if (pc->handle) {
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
        logmsg("Cannot open plugin directory: %s", PLUGIN_DIR);
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
            int res = plugin_load(entry->d_name, g_PluginCount);
            strncpy(pc->name, entry->d_name, sizeof(pc->name) - 1);
            pc->id = g_PluginCount;
            if (res == 0) {
                int rr = pc->plugin_register(pc, &g_plugin_host);
                if (rr == 0) {
                    logmsg("Plugin registered: %s", entry->d_name);
                } else {
                    logmsg("Plugin registration failed: %s", entry->d_name);
                }
            } else {
                logmsg("Plugin load failed: %s", entry->d_name);
            }
            g_PluginCount++;
        }
    }

    closedir(dir);
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
void housekeeper_plugins(time_t now ){
    for (int i = 0; i < g_PluginCount; i++) {
        PluginContext *pc = &g_Plugins[i];
        if (pc->handle) {
            if (pc->used_count <= 0) {
                time_t last = (time_t)pc->last_used;
                if (difftime(now, last) > PLUGIN_IDLE_TIMEOUT) {
                    plugin_unload(i);
                    logmsg("Plugin unloaded: %s", pc->name);
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
            logmsg("Mapgen idle timeout. Unloading mapgen");
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
    g_housekeeper.mysql_loaded = 0U;
    g_housekeeper.png_loaded = 0U;
    if (pthread_create(&g_housekeeper.thread_id, NULL, housekeeper_thread, NULL) != 0) {
        perror("Failed to create housekeeper thread");
        exit(1);
    }
}

void stop_housekeeper() {
    g_housekeeper.running = 0U;
    pthread_join(g_housekeeper.thread_id, NULL);
}

void parse_http_request(ClientContext *ctx, HttpRequest *req) {
    memset(req, 0, sizeof(HttpRequest));

    char *line = strtok(ctx->request_buffer, "\r\n");
    if (!line) return;

    sscanf(line, "%7s %255s", req->method, req->path);

    char *query_start = strchr(req->path, '?');
    if (query_start) {
        *query_start = '\0';
        query_start++;
        char *param = strtok(query_start, "&");
        while (param && req->query_count < MAX_QUERY_VARS) {
            char *eq = strchr(param, '=');
            if (eq) {
                *eq = '\0';
                strncpy(req->query[req->query_count].key, param, sizeof(req->query[req->query_count].key) - 1);
                strncpy(req->query[req->query_count].value, eq + 1, sizeof(req->query[req->query_count].value) - 1);
                req->query_count++;
            }
            param = strtok(NULL, "&");
        }
    }

    while ((line = strtok(NULL, "\r\n")) && *line && req->header_count < MAX_HEADER_LINES) {
        char *sep = strchr(line, ':');
        if (sep) {
            *sep = '\0';
            char *key = line;
            char *val = sep + 1;
            while (isspace(*val)) val++;
            strncpy(req->headers[req->header_count].key, key, sizeof(req->headers[req->header_count].key) - 1);
            strncpy(req->headers[req->header_count].value, val, sizeof(req->headers[req->header_count].value) - 1);
            req->header_count++;
        }
    }
}

void print_png_version() {
    logmsg("libpng version (static, dynamic, mutex): %s, %s, %s", PNG_LIBPNG_VER_STRING, png_libpng_ver, HAVE_OLD_PNG ? "yes" : "no");
}
int PngImage_init(PngImage *img, int width, int height, const char *filename, unsigned char color_type) {
    img->width = width;
    img->height = height;
    img->filename = filename;
    img->tmp_filename[0] = '\0';
    snprintf(img->tmp_filename, sizeof(img->tmp_filename), "%s_", filename);
    logmsg("Opening PNG file for writing. %s", img->tmp_filename);
    img->fp = fopen(img->tmp_filename, "wb");
    if (!img->fp) {
        logmsg("Failed to open PNG file for writing. %s", filename);
        return 1;
    }
    PNG_CRITICAL_START();
    img->png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    img->info_ptr = png_create_info_struct(img->png_ptr);
    if (!img->png_ptr || !img->info_ptr) {
        fclose(img->fp);
        logmsg("Failed to create PNG structures.");
        return 2;
    }
    png_init_io(img->png_ptr, img->fp);
    png_set_IHDR(img->png_ptr, img->info_ptr, img->width, img->height,
                 8, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(img->png_ptr, img->info_ptr);
    return 0;
}

void PngImage_finish(PngImage *img) {
    png_write_end(img->png_ptr, NULL);
    png_destroy_write_struct(&img->png_ptr, &img->info_ptr);
    fclose(img->fp);
    int res=rename(img->tmp_filename, img->filename);
    if (res != 0) {
        logmsg("Failed to rename %s to %s", img->tmp_filename, img->filename);
    }
    PNG_CRITICAL_END();
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

void generate_biome_png(RequestParams *params, const char *filename) {
    // Create a PNG image with the specified dimensions
    PngImage img;
    if (PngImage_init(&img, params->width, params->height, filename, PNG_COLOR_TYPE_RGB)) {
        logmsg("Failed to initialize PNG image.");
        return;
    }
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
        png_write_row(img.png_ptr, row);
        free(row);
    }
    PngImage_finish(&img);
    logmsg("Biome PNG generated: %s", filename);
}
void generate_elevation_png(RequestParams *params, const char *filename) {
    PngImage img;
    if (PngImage_init(&img, params->width, params->height, filename, PNG_COLOR_TYPE_GRAY)) {
        logmsg("Failed to initialize PNG image.");
        return;
    }
    for (unsigned int y = 0; y < img.height; y++) {
        png_bytep row = malloc(3 * img.width);
        float lat = params->lat_max - ((params->lat_max - params->lat_min) / img.height) * y;
        for (unsigned int x = 0; x < img.width; x++) {
            float lon = params->lon_min + ((params->lon_max - params->lon_min) / img.width) * x;
            TerrainInfo info = map_context.get_info(lat, lon);
            int elevation = info.elevation * 255.0f;
            if (elevation < 0) elevation = 0;
            if (elevation > 255) elevation = 255;
            row[x] = elevation;
        }
        png_write_row(img.png_ptr, row);
        free(row);
    }
    PngImage_finish(&img);
    logmsg("Elevation PNG generated: %s", filename);
}
void generate_clouds_png(RequestParams *params, const char *filename) {
    PngImage img;
    if (PngImage_init(&img, params->width, params->height, filename, PNG_COLOR_TYPE_RGBA)) {
        logmsg("Failed to initialize PNG image.");
        return;
    }
    for (unsigned int y = 0; y < img.height; y++) {
        png_bytep row = malloc(4 * img.width);
        float lat = params->lat_max - ((params->lat_max - params->lat_min) / img.height) * y;
        for (unsigned int x = 0; x < img.width; x++) {
            float lon = params->lon_min + ((params->lon_max - params->lon_min) / img.width) * x;
            TerrainInfo info = map_context.get_info(lat, lon);
            row[x*4 + 0] = 255;
            row[x*4 + 1] = 255;
            row[x*4 + 2] = 255;
            row[x*4 + 3] = info.precip;
        }
        png_write_row(img.png_ptr, row);
        free(row);
    }
    PngImage_finish(&img);

    logmsg("Clouds PNG generated: %s", filename);
}

typedef struct {
    float lat, lon;
    float elevation;
    unsigned char r, g, b;
    char name[128];
} RegionsDataRecord;

int cached_regions_chunk_json(char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        logmsg("Failed to open regions file for reading");
        return 0;
    }
    fclose(fp);
    return 1;
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
            offset += snprintf(html + offset, sizeof(html) - offset, "<li>%d: %s  (used:%d)</li>", pc->id, pc->name, pc->used_count );
        }
    }
    offset += snprintf(html + offset, sizeof(html) - offset, "</ul>");
    offset += snprintf(html + offset, sizeof(html) - offset, "<p>Plugins not loaded:</p><ul>");
    for (int i = 0; i < g_PluginCount; i++) {
        PluginContext *pc = &g_Plugins[i];
        if (!pc->handle) {
            offset += snprintf(html + offset, sizeof(html) - offset, "<li>%d: %s</li>", pc->id, pc->name );
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
    logmsg("%s status_json request", ctx->client_ip);
}

void handle_regions_chunk_json(ClientContext *ctx, RequestParams *params) {
    
    char cache_filename[128];
    snprintf(cache_filename, sizeof(cache_filename), "../var/regions_lat%.2f_lon%.2f_lat%.2f_lon%.2f.json",
        params->lat_min, params->lon_min, params->lat_max, params->lon_max );
    if (file_exists_recent(cache_filename, CACHE_TIME)) {
        logmsg("Regions chunk JSON file is up to date: %s", cache_filename);
        send_file(ctx->socket_fd, "application/json", cache_filename);
        return;
    }
    FILE *fp;
    if (!file_exists(REGIONS_FILE)) {
        fp = fopen(REGIONS_FILE, "wb");
        if (!fp) {
            logmsg("Failed to open regions file for writing");
            send_response(ctx->socket_fd, 500, "text/plain", "Failed to open regions file for writing\n");
            return;
        }
        logmsg("Generating new regions file: %s", REGIONS_FILE);
        start_map_context();
        for (float lat = -70.0f; lat <= 70.0f; lat += 0.5f) {
            for (float lon = -180.0f; lon <= 180.0f; lon += 0.5f) {
                TerrainInfo info = map_context.get_info(lat, lon);
                if (info.elevation > 0.0f && info.elevation < 0.6f) {
                    int needed  = rand() % 300;
                    if (needed == 1) {
                        RegionsDataRecord region;
                        region.lat = lat;
                        region.lon = lon;
                        region.elevation = info.elevation;
                        region.r = info.r;
                        region.g = info.g;
                        region.b = info.b;
                        snprintf(region.name, sizeof(region.name), "city-%04d", rand()%10000);
                        fwrite(&region, sizeof(RegionsDataRecord), 1, fp);
                    }
                }
            }
        }
        stop_map_context();
        fclose(fp);
    }
    fp = fopen(REGIONS_FILE, "rb");
    if (fp){
        #define REGIONS_JSON_SIZE_LIMIT (1024*1024*10)
        char *json = malloc(REGIONS_JSON_SIZE_LIMIT); // Rough estimate for space
        if (!json) {
            dprintf(ctx->socket_fd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
            return;
        }
        strcpy(json, "{");
        int offset = 1; // skipping opening brace
        while(!feof(fp)) {
            RegionsDataRecord region;
            if (fread(&region, sizeof(RegionsDataRecord), 1, fp) == 1) {
                // filter to the provided region bounds
                if (region.lat >= params->lat_min && region.lat <= params->lat_max &&
                    region.lon >= params->lon_min && region.lon <= params->lon_max) {
                    char keyval[256];
                    int keyval_len = snprintf(keyval, sizeof(keyval), "\"%.2f,%.2f\":{\"r\":%d,\"g\":%d,\"b\":%d,\"e\":%.2f,\"name\":\"%s\"},",
                             region.lat, region.lon, region.r, region.g, region.b, region.elevation, region.name);
                    if (offset + keyval_len +1 > REGIONS_JSON_SIZE_LIMIT) {
                        logmsg("Regions JSON size limit exceeded");
                        dprintf(ctx->socket_fd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
                        free(json);
                        fclose(fp);
                        return;
                    }
                    strcpy(json + offset, keyval);
                    offset += keyval_len;  
                    json[offset+1] = '\0'; // null-terminate the string
                }
            }
        }
        if (offset > 1) json[offset - 1] = '}'; // replace last comma with closing brace
        else strcpy(json + offset, "}");
        json[offset+1] = '\0'; // null-terminate the string
        send_response(ctx->socket_fd, 200, "application/json", json);
        logmsg("%s regions_chunk_json request", ctx->client_ip);
        
        FILE *fp_out = fopen(cache_filename, "w");
        if (!fp_out) {
            logmsg("Failed to open regions file for writing");
        }else{
            fwrite(json, 1, strlen(json), fp_out);
            fclose(fp_out);
        }
        free(json);
        fclose(fp);
    } else {
        logmsg("Failed to open regions file for reading");
    }
}
void handle_map_json(ClientContext *ctx, RequestParams *params) {
    double lat_min = params->lat_min;
    double lat_max = params->lat_max;
    double lon_min = params->lon_min;
    double lon_max = params->lon_max;
    double step = params->step > 0 ? params->step : 0.5;

    if (lat_min >= lat_max || lon_min >= lon_max || step <= 0) {
        dprintf(ctx->socket_fd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
        return;
    }

    int point_count = (int)(((lat_max - lat_min) / step) * ((lon_max - lon_min) / step));
    if (point_count > 50000) {
        dprintf(ctx->socket_fd, "HTTP/1.1 413 Request Entity Too Large\r\nContent-Length: 0\r\n\r\n");
        return;
    }else if (point_count<1) {
        dprintf(ctx->socket_fd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
        return;
    }
    

    char *json = malloc(10 + point_count * 100); // Rough estimate for space
    if (!json) {
        dprintf(ctx->socket_fd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
        return;
    }
    if (start_map_context()){
        logmsg("Failed to start map context");
        free(json);
        send_response(ctx->socket_fd, 500, "text/plain", "Failed to start map context\n");
        return;   
    }
    strcpy(json, "{");
    int offset = 1; // skipping opening brace
    for (double lat = lat_min; lat <= lat_max; lat += step) {
        for (double lon = lon_min; lon <= lon_max; lon += step) {
            TerrainInfo info = map_context.get_info(lat, lon);
            char keyval[128];
            snprintf(keyval, sizeof(keyval), "\"%.2f,%.2f\":{\"r\":%d,\"g\":%d,\"b\":%d,\"e\":%.2f},",
                     lat, lon, info.r, info.g, info.b, info.elevation);
            strcpy(json + offset, keyval);
            offset += strlen(keyval);
        }
    }
    stop_map_context();
    if (offset > 1) json[offset - 1] = '}'; // replace last comma with closing brace
    else strcpy(json + offset, "}");
    json[offset+1] = '\0'; // null-terminate the string
    send_response(ctx->socket_fd, 200, "application/json", json);
    logmsg("%s map_json request", ctx->client_ip);
    free(json);
}
void handle_biome(ClientContext *ctx, RequestParams *params) {
    char filename[128];
    snprintf(filename, sizeof(filename), "../var/biome_lat%.2f_lon%.2f_%dx%d.png",
        params->lat_min, params->lon_min, 
        params->width, params->height);
    if (!file_exists_recent(filename, CACHE_TIME)) {
        logmsg("Generating new biome PNG: %s", filename);
        if (start_map_context()) {
            logmsg("Failed to start map context");
            send_response(ctx->socket_fd, 500, "text/plain", "Failed to start map context\n");
            return;
        }
        generate_biome_png(params, filename);
        stop_map_context();
    } else {
        logmsg("Using cached biome PNG: %s", filename);
    }
    //logmsg("%s biome request", ctx->client_ip);
    send_file(ctx->socket_fd, "image/png", filename);
}

void handle_elevation(ClientContext *ctx, RequestParams *params) {
    char filename[128];
    snprintf(filename, sizeof(filename), "../var/elevation_lat%.2f_lon%.2f_%dx%d.png",
        params->lat_min, params->lon_min, 
        params->width, params->height);
    if (!file_exists_recent(filename, 60)) {
        logmsg("Generating new elevation PNG: %s", filename);
        if (start_map_context()) {
            logmsg("Failed to start map context");
            send_response(ctx->socket_fd, 500, "text/plain", "Failed to start map context\n");
            return;
        }
        generate_elevation_png(params, filename);
        stop_map_context();
    } else {
        logmsg("Using cached elevation PNG: %s", filename);
    }
    send_file(ctx->socket_fd, "image/png", filename);
}
void handle_clouds(ClientContext *ctx, RequestParams *params) {
    char filename[128];
    snprintf(filename, sizeof(filename), "../var/clouds_lat%.2f_lon%.2f_%dx%d.png",
        params->lat_min, params->lon_min, 
        params->width, params->height);
    if (!file_exists_recent(filename, 60)) {
        logmsg("Generating new elevation PNG: %s", filename);
        if (start_map_context()) {
            logmsg("Failed to start map context");
            send_response(ctx->socket_fd, 500, "text/plain", "Failed to start map context\n");
            return;
        }
        generate_clouds_png(params, filename);
        stop_map_context();
    } else {
        logmsg("Using cached elevation PNG: %s", filename);
    }
    send_file(ctx->socket_fd, "image/png", filename);
}

void parse_request_path_and_params(const char *request, RequestParams *params) {
    memset(params, 0, sizeof(RequestParams));
    strncpy(params->path, "/", sizeof(params->path));
    params->lat_min = -90.0f;
    params->lat_max = 90.0f;
    params->lon_min = -180.0f;
    params->lon_max = 180.0f;
    params->step = 0.5f;
    params->width = 1024;
    params->height = 512;
    params->id = 0;

    const char *line = strstr(request, "GET ");
    if (!line) return;
    line += 4;
    const char *space = strchr(line, ' ');
    if (!space) return;

    char path_query[1024];
    size_t len = space - line;
    if (len >= sizeof(path_query)) len = sizeof(path_query) - 1;
    strncpy(path_query, line, len);
    path_query[len] = '\0';

    char *query = strchr(path_query, '?');
    if (query) {
        *query++ = '\0';
        char *token = strtok(query, "&");
        while (token) {
            float fval;
            int ival;
            if (sscanf(token, "lat_min=%f", &fval) == 1) params->lat_min = fval;
            else if (sscanf(token, "lat_max=%f", &fval) == 1) params->lat_max = fval;
            else if (sscanf(token, "lon_min=%f", &fval) == 1) params->lon_min = fval;
            else if (sscanf(token, "lon_max=%f", &fval) == 1) params->lon_max = fval;
            else if (sscanf(token, "width=%d", &ival) == 1) params->width = ival;
            else if (sscanf(token, "height=%d", &ival) == 1) params->height = ival;
            else if (sscanf(token, "step=%f", &fval) == 1) params->step = fval;
            else if (sscanf(token, "id=%d", &ival) == 1) params->id = ival;
            token = strtok(NULL, "&");
        }
    }
    strncpy(params->path, path_query, sizeof(params->path)-1);
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

int http_routes_count = 8;
const HttpRouteRule http_routes[] = {
    {"/map", handle_map_json},
    {"/biome", handle_biome},
    {"/elevation", handle_elevation},
    {"/clouds", handle_clouds},
    {"/regions_chunk", handle_regions_chunk_json},
    {"/status.html", handle_status_html},
    {"/status.json", handle_status_json},
    {"/", infopage},
};

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

void *handle_client(void *arg) {
    ClientContext *ctx = (ClientContext *)arg;
    int len = read(ctx->socket_fd, ctx->request_buffer, BUF_SIZE - 1);
    if (len <= 0) {
        close(ctx->socket_fd);
        free(ctx);
        return NULL;
    }
    ctx->request_buffer[len] = '\0';
    parse_http_request(ctx, &ctx->request);
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
                    logmsg("Plugin %s is busy", pc->name);
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

int main() {
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigterm_handler);
    signal(SIGUSR1, sigusr1_handler);

    char cwd[256];
    getcwd(cwd, sizeof(cwd));
    printf("Current working dir: %s\n", cwd);

    atexit(remove_pidfile);
    write_pidfile_or_exit();

    logmsg("GeoD starting on port %d", PORT);
    print_png_version();
    plugin_scan_and_register();
    start_housekeeper();

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
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

        struct timeval timeout = {1, 0}; // 1 másodperc
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
            pthread_create(&thread_id, NULL, handle_client, ctx);
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
