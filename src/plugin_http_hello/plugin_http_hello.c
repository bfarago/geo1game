/**
 * @file plugin_http_hello.c
 * @brief Example HTTP plugin for handling requests.
 * @details This plugin responds to a specific route with a JSON message.
 * @note This is a simplified example and should be adapted for production use.
 * 
 * gcc -fPIC -shared -o plugin_hello.so plugin_hello.c
 */
#define _GNU_SOURCE

#include "plugin.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

const PluginHostInterface *g_host;
void handle_hello(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc; // Unused parameter
    (void)params; // Unused parameter
    (void)ctx; // Unused parameter
    const char *body = "{\"msg\":\"Hello from plugin!\"}";
    g_host->send_response(ctx->socket_fd, 200, "application/json", body);
    g_host->logmsg("%s hello request", ctx->client_ip);
}

const char* plugin_http_get_routes[]={"/hello", NULL};
int plugin_http_get_routes_count = 1;

int plugin_register(PluginContext *pc, const PluginHostInterface *host) {
    g_host = host;
    host->register_http_route((void*)pc, plugin_http_get_routes_count, plugin_http_get_routes);
    return PLUGIN_SUCCESS;
}

int plugin_init(PluginContext* pc) {
    // Initialization code here
    pc->http.request_handler = (void*) handle_hello;
    return PLUGIN_SUCCESS;
}
void plugin_finish(PluginContext* pc) {
    // Cleanup code here
    pc->http.request_handler = NULL;
    // Free any allocated resources
}