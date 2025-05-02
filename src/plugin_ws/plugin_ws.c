#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#include "../plugin.h"
// globals
const PluginHostInterface *g_host;

const char* g_http_routes[1]={"status.json"};
int g_http_routess_count = 1;
const char* g_control_routes[1]={"ws"}; // like WS HELP enter.
int g_control_routess_count = 1;
const char* g_ws_routes[1]={"id_dont_know_yet"};
int g_ws_routess_count = 1;


/* When a control CLI command typed by user match with the provided list,
// the client request will be landed here, to provides some meaingful
// response on WebSocket protocol.
*/
void ws_control_handler(PluginContext *pc, ClientContext *ctx, char* cmd, int argc, char **argv){
    (void)pc;
    if (cmd){
        if (0 == strcasecmp(cmd, "ws")){
            if (argc >= 1){
                if (0 == strcmp(argv[0], "help")){
                    dprintf(ctx->socket_fd, 
                        "WS HELP:\nThis is the command line interface of the plugin ws.\n"
                        " available sub-commands are:\n"
                        " help: prints this text.\n"
                    );
                }
            }
        }
    }
    
    return;
}

/* When a http route match with the provided list, a client request
// will land here, to provides some meaingful response on http protocol.
*/
void ws_http_handler(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc;
    (void)params;
    g_host->http.send_response(ctx->socket_fd, 404, "text/plain", "This path is not yet impkemented.\n");
}
void ws_ws_handler(PluginContext *pc, ClientContext *ctx, WsRequestParams *wsparams) {
    (void)pc;
    (void)ctx;
    // todo: entry point of the ws plugin behind the client
    // thread. (after accept)
    wsparams->session_id[0]=0; //some input/output params, similar to the http use-case.
    //using addition host api fn-s we can do other things here...
}
int plugin_register(PluginContext *pc, const PluginHostInterface *host) {
    (void)pc;
    g_host = host;
    g_host->server.register_control_route(pc, g_control_routess_count, g_control_routes);
    return PLUGIN_SUCCESS;
}

int plugin_thread_init(PluginContext *ctx) {
    (void)ctx;
    // this will be run for each new connection
    return 0;
}

int plugin_thread_finish(PluginContext *ctx) {
    (void)ctx;
    // this will be run for each connection, when finished.
    return 0;
}
int plugin_init(PluginContext* pc, const PluginHostInterface *host) {
    (void)pc;
    g_host = host;
    pc->control.request_handler = ws_control_handler;
    pc->http.request_handler = ws_http_handler;
    pc->ws.request_handler = ws_ws_handler;
    return PLUGIN_SUCCESS;
}
void plugin_finish(PluginContext* pc) {
    (void)pc;
    // Will runs once, when plugin unloaded.
    pc->http.request_handler = NULL;
}
// Plugin event handler implementation
int plugin_event(PluginContext *pc, PluginEventType event, const PluginEventContext* ctx) {
    (void)pc;
    (void)ctx;
    if (event == PLUGIN_EVENT_STANDBY) {
        /*
        pthread_mutex_lock(&cgi_pool_mutex);
        for (int i = 0; i < MAX_CGI_PIDS; i++) {
            if (g_cgi_pool[i].state == CGI_STATE_RUNNING) {
                pthread_mutex_unlock(&cgi_pool_mutex);
                return 1;
            }
        }
        pthread_mutex_unlock(&cgi_pool_mutex);
        */
    }
    return 0;
}