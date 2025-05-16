/**
 * File:    plugin_map.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-05-10
 * 
 *  Mapgen lib related plugin
 * TODO: move from main c file to this separate file.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <ctype.h>
#include <stdarg.h>

#include "sync.h"
#include "data_table.h"
#include "../plugin.h"
#include "cmd.h"
// #include "mapgen/mapgen.h"
#include "mapgen/perlin3d.h"

// globals
const PluginHostInterface *g_host = NULL;

static const char* g_http_routes[2]={"/map.html", "/map.json"};

static const int g_http_routes_count = 2;

typedef enum {
    CMD_MAP_REGENERATE,
    CMD_MAP_STAT,
    CMD_MAP_MAXID
} PluginMapCmdId;

static const CommandEntry g_plugin_map_cmds[CMD_MAP_MAXID] ={
    [CMD_MAP_REGENERATE] = {.path="map regenerate",      .help="Re-generate map", .arg_hint=""},
    [CMD_MAP_STAT]       = {.path="map stat",      .help="Map statistics", .arg_hint=""},
};

static int plugin_map_execute_command(PluginContext *pc, ClientContext *ctx, CommandEntry *pe, char* cmd){
    (void)pc;
    (void)ctx;
    (void)cmd;
    int ret=-1;
    if (pe){
        switch (pe->handlerid){
            case CMD_MAP_REGENERATE: ret = 0; break;
            case CMD_MAP_STAT: ret=0; break;
        }
    }
    return ret;
}
typedef enum {
    FID_MAP_ID,
    FID_MAP_MAXNUMBER
} MapReportFieldId_t;

const FieldDescr g_fields_MapStat[FID_MAP_MAXNUMBER] = {
    [FID_MAP_ID]        = { .name = "ID",           .fmt = "%6d",     .width = 6,  .align_right = 1, .type = FIELD_TYPE_INT,    .precision = -1 },
};

const TableDescr g_table_MapStat = {
    .fields_count = FID_MAP_MAXNUMBER,
    .fields = g_fields_MapStat
};

TableResults g_results_MapStat = {.fields = NULL, .rows_count=0};
void genStat_Map(){
    
}
/**
 * Textual dump from prviously calculated datas
 */
size_t stat_text_gen(TextContext *tc, char* buf, size_t len){
    size_t o = 0; 
    tc->title = "Map statistics"; tc->id = "map"; tc->flags=1;
    o += table_gen_text( &g_table_MapStat, &g_results_MapStat, buf + o, len - o, tc);
    return o;
}

void http_html_handler(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc;
    (void)params;
    char buf[BUF_SIZE];
    size_t len = BUF_SIZE -1;
    size_t o = 0; // offset
    o += snprintf(buf + o, len - o, "<html><head>\n");
    o += snprintf(buf + o, len - o, "<style>\n");
    o += snprintf(buf + o, len - o, "body { background-color: #000; color: #ddd; font-family: monospace, sans-serif; font-size: 14px; }\n");
    o += snprintf(buf + o, len - o, "table { border-collapse: collapse; margin: 1em 0; width: 100%%; }\n");
    o += snprintf(buf + o, len - o, "th, td { border: 1px solid #444; padding: 4px 8px; }\n");
    o += snprintf(buf + o, len - o, "tr.head { background-color: #444; color: #fff; font-weight: bold; }\n");
    o += snprintf(buf + o, len - o, "tr.roweven { background-color: #111; color: #ccc; }\n");
    o += snprintf(buf + o, len - o, "tr.rowodd { background-color: #222; color: #ccc; }\n");
    o += snprintf(buf + o, len - o, "td.cellleft { text-align: left; }\n");
    o += snprintf(buf + o, len - o, "td.cellright { text-align: right; }\n");
    o += snprintf(buf + o, len - o, "</style>\n");
    o += snprintf(buf + o, len - o, "</head><body>\n");
    genStat_Map();
    TextContext tc;
    tc.format= TEXT_FMT_HTML;
    o += stat_text_gen(&tc, buf+o, len-o);
    o += snprintf(buf + o, len - o, "\n</body></html>\n");
    g_host->http.send_response(ctx->socket_fd, 200, "text/html", buf);
}
void http_json_handler(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc;
    (void)params;
    char buf[BUF_SIZE];
    size_t len = BUF_SIZE -1;
    size_t o = 0;
    genStat_Map();
    TextContext tc;
    tc.format = TEXT_FMT_JSON_OBJECTS;
    o += stat_text_gen(&tc, buf+o, len-o);
    g_host->http.send_response(ctx->socket_fd, 200, "application/json", buf);
}
static void (* const g_http_handlers[2])(PluginContext *, ClientContext *, RequestParams *) = {
    http_html_handler, http_json_handler
};
void http_handler(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    for (int i = 0; i < g_http_routes_count; i++) {
        if (strcmp(g_http_routes[i], params->path) == 0) {
            g_http_handlers[i](pc, ctx, params);
            return;
        }
    }
    const char *body = "{\"msg\":\"Unknown sub path\"}";
    g_host->http.send_response(ctx->socket_fd, 200, "application/json", body);
    g_host->logmsg("%s mysql request", ctx->client_ip);
}

int plugin_register(PluginContext *pc, const PluginHostInterface *host) {
    (void)pc;
    g_host = host;
    g_host->server.register_commands(pc, g_plugin_map_cmds, sizeof(g_plugin_map_cmds)/sizeof(CommandEntry));
    g_host->server.register_http_route(pc, g_http_routes_count, g_http_routes);
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
    // control protocol
    pc->control.execute_command = plugin_map_execute_command; // command execution layer
    // http protocol
    pc->http.request_handler= http_handler;
    return PLUGIN_SUCCESS;
}

void plugin_finish(PluginContext* pc) {
    (void)pc;
    // Will runs once, when plugin unloaded.
    pc->http.request_handler = NULL;
    pc->control.execute_command  = NULL;
}

// Plugin event handler implementation
int plugin_event(PluginContext *pc, PluginEventType event, const PluginEventContext* ctx) {
    (void)pc;
    (void)ctx;
    if (event == PLUGIN_EVENT_STANDBY) {

    }
    return 0;
}
