#ifndef PLUGIN_H
#define PLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif


#include "global.h"
#include "http.h"
#include "image.h"
#include "cache.h"

#define PLUGIN_SUCCESS 0
#define PLUGIN_ERROR (-1)
// Plugin event types and context
typedef enum {
    PLUGIN_EVENT_SLEEP = 1, // sleep is possible (optional)
    PLUGIN_EVENT_STANDBY,   // keep waiting, would be nice to stop
    PLUGIN_EVENT_TERMINATE, // terminate it, shall be stopped.
    // Future events can be added here
} PluginEventType;

typedef struct {
    int reserved; // Future use, can be union
} PluginEventContext;

struct PluginContext;
#define PCHANDLER struct PluginContext*

// Generic plugin event handler type
typedef int (*plugin_event_handler_t)(PCHANDLER, PluginEventType event, const PluginEventContext* ctx);

struct DbQuery;

typedef struct TerrainInfo{
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

typedef struct MapHostInterface{
    int (*start_map_context)(void);
    int (*stop_map_context)(void);
    int (*get_map_info)(TerrainInfo *info, float lat, float lon);
} MapHostInterface;
typedef struct{
    void (*register_http_route)(struct PluginContext* pc, int cout, const char *route[]);
    void (*register_control_route)(struct PluginContext* pc, int cout, const char *route[]);
}ServerHostInterface;

typedef struct {
   //  void (*register_http_route)(struct PluginContext* pc, int cout, const char *route[]);
    void (*send_response)(int clientid, int status_code, const char *content_type, const char *body);
    void (*send_file)(int clientid, const char * content_type, const char *path);
    void (*send_chunk_head)(ClientContext *ctx, int status_code, const char *content_type);
    void (*send_chunks)(ClientContext *ctx, char* buf, int offset);
    void (*send_chunk_end)(ClientContext *ctx);
} HttpHostInterface;

typedef struct {
    // other plugins can use this, when ws plugin is loaded...
    void (*handshake)(PCHANDLER pc, WsRequestParams* wsp, const char *msg); // dummy example.
} WsHostInterface;

typedef struct {
    int (*context_start)(PCHANDLER pc);
    int (*context_stop)(PCHANDLER pc);
    int (*create)(PCHANDLER pc, Image *img, const char *filename, unsigned int width, unsigned int height, ImageBackendType backend, ImageFormat format, ImageBufferFormat buffer_format);
    int (*destroy)(PCHANDLER pc, Image *img);
    void (*get_buffer)(PCHANDLER pc, Image *img, void **buffer);
    void (*write_row)(PCHANDLER pc, Image *img, void *row);
} ImageHostInterface;

typedef struct {
    struct PluginContext* (*get_plugin_context)(const char *name);
    void (*start_timer)(PCHANDLER pc, int interval, void (*callback)(PCHANDLER));
    void (*stop_timer)(PCHANDLER pc);
    void (*logmsg)(const char *fmt, ...);
    void (*errormsg)(const char *fmt, ...);
    void (*debugmsg)(const char *fmt, ...);
    int (*file_exists)(const char *filename);
    int (*file_exists_recent)(const char *filename, int cache_time);
    int (*config_get_string)(const char *grou, const char *key, char *buf, int buf_size, const char *default_value);
    int (*config_get_int)(const char *grou, const char *key, int default_value);
    void (*register_db_queue)(PCHANDLER pc, const char *name);
    ServerHostInterface server;
    HttpHostInterface http;
    WsHostInterface ws;
    MapHostInterface map;
    ImageHostInterface image;
    CacheHostInterface cache;
} PluginHostInterface;

// HTTP host side
typedef void (*PluginHttpRequestHandler)(PCHANDLER, ClientContext *ctx, RequestParams *params);
typedef struct {
    PluginHttpRequestHandler request_handler;
} PluginHttpFunctions;

// WS host side
typedef void (*PluginWsRequestHandler)(PCHANDLER, ClientContext *ctx, WsRequestParams *params);
typedef struct {
    PluginWsRequestHandler request_handler;
} PluginWsFunctions;

// Control
typedef void (*PluginControlRequestHandler)(PCHANDLER, ClientContext *ctx, char* cmd, int argc, char **argv);
typedef struct{
    PluginControlRequestHandler request_handler;
} PluginControlFunctions;

// DB host side (preliminary)
struct DbQuery;
#define DB_QUERY_MAX_QUERY_LEN (256)
#define DB_QUERY_MAX_ROWS (16)
#define DB_QUERY_MAX_ROW_LEN (512)

typedef struct DbQuery{
    char query[DB_QUERY_MAX_QUERY_LEN];
    char rows[DB_QUERY_MAX_ROWS][DB_QUERY_MAX_ROW_LEN];
    int result_count;
} DbQuery;
typedef void (*QueryResultProc)(PCHANDLER,  DbQuery* query, void *user_data);
typedef void (*PluginDbRequestHandler)(DbQuery *query, QueryResultProc result_proc, void *user_data);
typedef void (*PluginDbQueuePush)();
typedef struct {
    PluginDbRequestHandler request;
} PluginDbFunctions;

// IMAGE host side
typedef int (*PluginImageCreate)(PCHANDLER, Image *image, const char *filename, unsigned int width, unsigned int height, ImageBackendType backend, ImageFormat format, ImageBufferFormat buffer_type);
typedef int (*PluginImageDestroy)(PCHANDLER, Image *image);
typedef void (*PluginImageGetBuffer)(PCHANDLER, Image *image, void** buffer);
typedef void (*PluginImageWriteRow)(PCHANDLER, Image *image, void* row);
typedef struct PluginImageFunctions{
    PluginImageCreate create;
    PluginImageDestroy destroy;
    PluginImageGetBuffer get_buffer;
    PluginImageWriteRow write_row;
}PluginImageFunctions;

// PLUGIN host side
typedef int (*plugin_register_t)(PCHANDLER, const PluginHostInterface*);
typedef int (*plugin_init_t)(PCHANDLER, const PluginHostInterface*);
typedef void (*plugin_finish_t)(PCHANDLER);
typedef int (*plugin_thread_init_t)(PCHANDLER );
typedef int (*plugin_thread_finish_t)(PCHANDLER );

// initialized at the first init, but kept in memory.
typedef struct HttpCapabilities{
    int http_route_count;
    char **http_routes;
} HttpCapabilities;

typedef struct WsCapabilities{
    int ws_route_count;
    char **ws_routes;
}WsCapabilities;

typedef struct{
    int route_count;
    char **routes;
} ControlCapabilities;

// This is the common (union of) the all plugins
typedef struct PluginContext{
    int id;
    char name[MAX_PLUGIN_NAME];  // plugin file neve

    // loaded information
    void *handle;
    plugin_register_t plugin_register;
    plugin_init_t plugin_init;
    plugin_finish_t plugin_finish;
    plugin_thread_init_t thread_init;
    plugin_thread_finish_t thread_finish;
    unsigned long file_mtime;
    unsigned long last_used;
    int used_count;
    int tried_to_shutdown;

    // registered information, this part will kept in memory
    HttpCapabilities http_caps;
    ControlCapabilities control_caps;
    WsCapabilities ws_caps;

    // dynamic/optional functions, this is only available, when a plugin is already started.
    // HTTP
    PluginHttpFunctions http;
    // WS
    PluginWsFunctions ws;
    // Control
    PluginControlFunctions control;
    // DB functions
    PluginDbFunctions db;
    // Image functions
    PluginImageFunctions image;
    // Generic event handler (optional)
    plugin_event_handler_t plugin_event;
} PluginContext;

void register_http_routes(PluginContext *ctx, int count, const char *routes[]);
void register_ws_routes(PluginContext *ctx, int count, const char *routes[]);
void register_control_routes(PluginContext *ctx, int count, const char *routes[]);
void register_db_queue(PluginContext *ctx, const char *db);

PluginContext* get_plugin_context(const char *name);
int plugin_start(int id);
void plugin_stop(int id);

#ifdef __cplusplus
}
#endif
#endif // PLUGIN_H