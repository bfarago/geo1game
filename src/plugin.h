/*
 * File:    plugin.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * Plugin API interface
 * Key features:
 *  registration, host and plugin side APIs
 */
#ifndef PLUGIN_H
#define PLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "global.h"
#include "sync.h"
#include "data_table.h"
#include "http.h"
#include "image.h"
#include "cache.h"
#include "cmd.h"


#define PLUGIN_SUCCESS 0
#define PLUGIN_SUCCESS_OWN_THEAD 1
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

/** MAP subsystem
 */

 /** MAP TerraInfo struct
 * the basic datatype of an atomic point of the map. */
 typedef struct TerrainInfo{
    float elevation;
    unsigned char r, g, b;
    unsigned char precip, temp;
} TerrainInfo;

/** MAP subsystem user API
 * in case of a plugin or main code needs a map data, this API is used. */
typedef struct MapHostInterface{
    int (*start_map_context)(void);
    int (*stop_map_context)(void);
    int (*get_map_info)(TerrainInfo *info, float lat, float lon);
} MapHostInterface;

typedef struct MapPluginInterface{
    int (*get_info)(TerrainInfo *info, float lat, float lon);
} MapPluginInterface;

/**
 * statistics subsystem
 */
typedef struct{
    int (*det_str_dump)(char* buf, int len);
    int (*det_access)(int *size, int *counters, int *lines );
    int (*stat_str_dump)(char* buf, int len);
    void (*stat_clear)(void);
}StatInterface;

/** Server or daemon side API for plugins to call
 * The following function are provided to plugins by the host (daemon)
 * To mainly for register a capability in the unloadable plugin.
 * In example, the http request path which can be handled by the plugin */
typedef struct{
    void (*register_http_route)(struct PluginContext* pc, int cout, const char *route[]);
    server_execute_commands_fn execute_commands;

    cmd_register_commands_fn register_commands;
    cmd_search_fn cmd_search;
    cmd_get_fn cmd_get;
    cmd_get_cound_fn cmd_get_count;

    int  (*get_plugin_count)(void);
    struct PluginContext* (*get_plugin)(int id);
    int (*server_det_str_dump)(char *buf, int len);
    void (*server_stat_table)(const TableDescr **tdout, TableResults **trout);
    void (*server_stat_clear)(void);
}ServerHostInterface;

/**
 * Data handling
 */
typedef struct{
    table_results_alloc_fn results_alloc;
    table_results_free_fn results_free;
    table_row_get_fn row_get;
    table_field_set_str_fn field_set_str;
    table_gen_text_fn gen_text;
}DataHandlingInterface;

struct ClientContext;
/** HTTP protocol specific host interface
 * Plugins / main (users) can call it, to use a http protocol feature, like 
 * send a resopose back to the clients socket. */
typedef struct {
    void (*send_response)(int clientid, int status_code, const char *content_type, const char *body);
    void (*send_file)(int clientid, const char * content_type, const char *path);
    /*
    void (*send_chunk_head)(struct ClientContext *ctx, int status_code, const char *content_type);
    void (*send_chunks)(struct ClientContext *ctx, char* buf, int offset);
    void (*send_chunk_end)(struct ClientContext *ctx);
    */
} HttpHostInterface;

/** WebSocket interface related API fns */
typedef struct {
    // other plugins can use this, when ws plugin is loaded...
    void (*handshake)(PCHANDLER pc, WsRequestParams* wsp, const char *msg); // dummy example.
} WsHostInterface;

/** Image support related API */
typedef struct {
    int (*context_start)(PCHANDLER pc);
    int (*context_stop)(PCHANDLER pc);
    int (*create)(PCHANDLER pc, Image *img, const char *filename, unsigned int width, unsigned int height, ImageBackendType backend, ImageFormat format, ImageBufferFormat buffer_format);
    int (*destroy)(PCHANDLER pc, Image *img);
    void (*get_buffer)(PCHANDLER pc, Image *img, void **buffer);
    void (*write_row)(PCHANDLER pc, Image *img, void *row);
} ImageHostInterface;

typedef void* (*plugin_thread_main_fn)(void*);
typedef struct {
    int (*create_own)(PCHANDLER pc, plugin_thread_main_fn mainfn, const char* name);
//    int (*register_own)(PCHANDLER pc, pthread_t tid, const char* name);
    int (*wait_for_own)(PCHANDLER pc);
    void (*enter_own)(PCHANDLER pc);
    void (*exit_own)(PCHANDLER pc);
} ThreadHostInterface;

/** Host interfaces
 *  This is the complete collection of the host provided interfaces.
 *  Different specialized APIs are definied sperately, but added here,
 *  Therefore users (plugins) could acces to it, like http.send_response()
 */
typedef struct {
    struct PluginContext* (*get_plugin_context)(const char *name);
    int (*start)(int id);
    void (*stop)(int id);
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
    ThreadHostInterface thread;
    DataHandlingInterface data;
    ServerHostInterface server;
    HttpHostInterface http;
    WsHostInterface ws;
    MapHostInterface map;
    ImageHostInterface image;
    CacheHostInterface cache;
} PluginHostInterface;

/** HOST side called Plugin interfaces
 * Basically called only by the daemon during operation.
 * For exaple, a HTTP protocol based daemon thred may directly call
 * a plugin provided function to deal with the actual phase of the
 * communication, like routing the request to the best handler fn.
 */

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
    PluginControlRequestHandler request_handler; // the communication layer
    plugin_execute_commands_fn execute_command;
} PluginControlFunctions;

/** DB subsystem
 * There is a generous database abstraction, it provides a table view of a
 * specific data query, and may filled up by another plugin in the background.
 * Therefore user code may not need a specific knowladge about the data driver.
 */

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

/** Image generator subsystem
 * Right now, a simple png file exporter only.
 */

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

/** PLUGIN API for host
 * Called only by the host side during operation to handle systematic needs, like init, finish an operation cycle.
 * */
typedef int (*plugin_register_t)(PCHANDLER, const PluginHostInterface*);
typedef int (*plugin_init_t)(PCHANDLER, const PluginHostInterface*);
typedef void (*plugin_finish_t)(PCHANDLER);
typedef int (*plugin_thread_init_t)(PCHANDLER );
typedef int (*plugin_thread_finish_t)(PCHANDLER );

/** Capabilities
 * A plugin may (or may not) implement some specific capabilities, like HTTP route handling.
 */

// initialized at the first init, but kept in memory.
typedef struct HttpCapabilities{
    int http_route_count;
    char **http_routes;
} HttpCapabilities;

// WebSocket capabilities
typedef struct WsCapabilities{
    int ws_route_count;
    char **ws_routes;
}WsCapabilities;

// Control protocol capabilities
typedef struct{
    int route_count;
    char **routes;
} ControlCapabilities;

typedef enum {
    PLUGIN_STATE_NONE = 0,
    PLUGIN_STATE_UNLOADED,
    PLUGIN_STATE_LOADING,
    PLUGIN_STATE_LOADED,
    PLUGIN_STATE_INITIALIZED,
    PLUGIN_STATE_RUNNING,
    PLUGIN_STATE_SHUTTING_DOWN,
    PLUGIN_STATE_DISABLED, //due to a detected error
    PLUGIN_STATE_MAX // the number of the plugin states
} PluginState_t;

#define MAX_PLUGIN_OWN_THREADS (3) // Typically one only

typedef struct {
    volatile int keep_running;
    sync_cond_t *cond;
    sync_mutex_t *mutex;
} PluginThreadControl;

typedef struct {
    sync_thread_t thread; // pthread_t
    int running;
    char name[16];
    PluginThreadControl control;
} PluginOwnThreadInfo;

typedef struct {
    PluginOwnThreadInfo own_threads[MAX_PLUGIN_OWN_THREADS];
    int own_threads_count;
}PluginThreadData;

// This is the common collection (union of) the all plugins
typedef struct PluginContext{
    int id;
    char name[MAX_PLUGIN_NAME];  // plugin file neve
    PluginState_t state;

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

    PluginThreadData thread;

    // registered information, this part will kept in memory
    HttpCapabilities http_caps;
    ControlCapabilities control_caps;
    WsCapabilities ws_caps;

    // dynamic/optional functions, this is only available, when a plugin is already started.
    // Statistics
    StatInterface stat;
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
    // Map functions
    MapPluginInterface map;
    // Generic event handler (optional)
    plugin_event_handler_t plugin_event;
} PluginContext;

#ifdef PLUGINHST_STATIC_LINKED
PluginContext* get_plugin_context(const char *name);
int plugin_start(int id);
void plugin_stop(int id);
#endif

#ifdef __cplusplus
}
#endif
#endif // PLUGIN_H