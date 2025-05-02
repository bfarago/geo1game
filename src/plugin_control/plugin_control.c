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
const char* g_control_routes[1]={"help"};
int g_control_routess_count = 1;

void pcontrol_handler(PluginContext *pc, ClientContext *ctx, char* cmd, int argc, char **argv)
{
    (void)pc;
    (void)cmd;
    (void)argc;
    (void)argv;
    dprintf(ctx->socket_fd, "HELP:\nThis is the comman line interface.\n");
    return;
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
    pc->control.request_handler = pcontrol_handler;
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