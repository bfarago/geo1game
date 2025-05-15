/*
 * File:    handlers.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * Main handlers are statistics, and exaples for the APIs only
 * Key features:
 *  status report, info, test page
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HTTP_STATIC_LINKED
#define PLUGINHST_STATIC_LINKED

#include "global.h"
#include "config.h"
#include "plugin.h"
#include "pluginhst.h"
#include "handlers.h"
#include <png.h>

int http_route_get(size_t index, PluginHttpRequestHandler *prh, PluginContext **ppc);

// extras for stats only
extern int g_PluginCount;
extern PluginContext g_Plugins[MAX_PLUGIN];
extern const PluginHostInterface g_plugin_host;
const PluginHostInterface *g_host= &g_plugin_host;

#define start_map_context g_host->map.start_map_context
#define stop_map_context g_host->map.stop_map_context
#define image_context_start g_host->image.context_start
#define image_context_stop g_host->image.context_stop
#define image_create g_host->image.create

#define MAX_HTML_BUF (8192)

void handle_status_html(PluginContext*pc, ClientContext *ctx, RequestParams *params) {
    (void)pc;
    (void)ctx;
    size_t o =0;
    char buf[BUF_SIZE];
    size_t len = BUF_SIZE;
    o += snprintf(buf + o, len - o, "<html><head>");
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
    o += snprintf(buf + o, len - o, "</head><body><h1>GeoD Status</h1><p>Server is running.</p>");

    const char *html_end =  "</body></html>";
    char html[MAX_HTML_BUF];
    int offset = 0;

    offset += snprintf(html + offset, sizeof(html) - offset, "%s", buf);
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
    size_t nr_of_routes= http_route_count();
    for (int i = 0; i < g_PluginCount; i++) {
        PluginContext *pc = &g_Plugins[i];
        if (pc->handle) {
            offset += snprintf(html + offset, sizeof(html) - offset, "<li>%d: %s  (used:%d) ",
             pc->id, pc->name, pc->used_count );
            for (size_t j = 0; j < nr_of_routes; j++) {
                PluginContext *pc2; PluginHttpRequestHandler rh2;
                http_route_get(j, &rh2, &pc2);
                if (pc2)
                if (pc2->id == pc->id){
                    const char* route= NULL;
                    http_route_get_path(j, &route);
                    offset += snprintf(html + offset, sizeof(html) - offset, "[<a href=\"%s%s\">%s</a>] ",
                        ctx->request.server_url_prefix,
                        route, route );
                }
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
            for (size_t j = 0; j < nr_of_routes; j++) {
                PluginContext *pc2; PluginHttpRequestHandler rh2;
                http_route_get(j, &rh2, &pc2);
                if (pc2)
                if (pc2->id == pc->id){
                    const char* route= NULL;
                    http_route_get_path(j, &route);
                    offset += snprintf(html + offset, sizeof(html) - offset, "[<a href=\"%s%s\">%s</a>] ",
                        ctx->request.server_url_prefix,
                        route, route );
                }
            }
            offset += snprintf(html + offset, sizeof(html) - offset, "</li>");
        }
    }
    offset += snprintf(html + offset, sizeof(html) - offset, "</ul>");
    offset += snprintf(html + offset, sizeof(html) - offset, "<p>Mapgen Loaded: %s</p>",
        housekeeper_is_mapgen_loaded() ? "Yes" : "No");
    
    offset += snprintf(html + offset, sizeof(html) - offset, "%s", html_end);
    send_response(ctx->socket_fd, 200, "text/html", html);
    logmsg("%s status_html request", ctx->client_ip);
}

void handle_status_json(PluginContext*pc, ClientContext *ctx, RequestParams *params) {
    (void)pc;
    (void)params; // suppress unused parameter warning
    const char *json = "{\"status\":\"running\"}";
    send_response(ctx->socket_fd, 200, "application/json", json);
    //logmsg("%s status_json request", ctx->client_ip);
}

void infopage(PluginContext*pc, ClientContext *ctx, RequestParams *params) {
    (void)pc;
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
void test_image(PluginContext*pc, ClientContext *ctx, RequestParams *params) {
    (void)pc;
    char filename[MAX_PATH];
    snprintf(filename, sizeof(filename), "%s/test_lat%.2f_lon%.2f_%dx%d.png",
        cache_get_dir(),
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
                        TerrainInfo info ;
                        g_host->map.get_map_info( &info, lat, lon);
                        
                        row[x*3 + 0] = info.r;
                        row[x*3 + 1] = info.g;
                        row[x*3 + 2] = info.b;
                    }
                    g_host->image.write_row(pcimg, &img, row);
                    free(row);
                }
                g_host->image.destroy(pcimg, &img);
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
