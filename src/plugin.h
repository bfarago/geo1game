#ifndef PLUGIN_H
#define PLUGIN_H

#include "global.h"
//#include "plugin_http.h"
#include "http.h"

#define PLUGIN_SUCCESS 0
#define PLUGIN_ERROR (-1)

#define PCHANDLER void*
typedef struct {
    void (*register_http_route)(PCHANDLER, int cout, const char *route[]);
    void* (*get_plugin_context)(PCHANDLER);
    void (*start_timer)(PCHANDLER pc, int interval, void (*callback)(PCHANDLER));
    void (*stop_timer)(PCHANDLER pc);
    void (*logmsg)(const char *fmt, ...);
    void (*send_response)(int clientid, int status_code, const char *content_type, const char *body);
    void (*send_file)(int clientid, const char * content_type, const char *path);
    int (*file_exists_recent)(const char *filename, int cache_time);
} PluginHostInterface;


typedef void (*PluginHttpRequestHandler)(PCHANDLER, ClientContext *ctx, RequestParams *params);
typedef struct {
    PluginHttpRequestHandler request_handler;
} PluginHttpFunctions;

typedef int (*plugin_register_t)(PCHANDLER, const PluginHostInterface*);
typedef int (*plugin_init_t)(PCHANDLER, const PluginHostInterface*);
typedef void (*plugin_finish_t)(PCHANDLER);

typedef struct{
    int id;
    char name[128];  // plugin file neve

    // loaded information
    void *handle;
    plugin_register_t plugin_register;
    plugin_init_t plugin_init;
    plugin_finish_t plugin_finish;
    unsigned long file_mtime;
    unsigned long last_used;
    int used_count;

    // registered information
    int http_route_count;
    char **http_routes;

    // dynamic functions
    PluginHttpFunctions http;

} PluginContext;

#undef PCHANDLER
#define PCHANDLER PluginContext*

#endif // PLUGIN_H