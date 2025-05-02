/**
 * @file plugin_shape.c
 */
#define _GNU_SOURCE

#include "plugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "shape.h"
#include "plan.h"

#define logmsg(...) g_host->logmsg(__VA_ARGS__)
#define send_response(...) g_host->http.send_response(__VA_ARGS__)
#define send_file(...) g_host->http.send_file(__VA_ARGS__)
#define file_exists_recent(...) g_host->file_exists_recent(__VA_ARGS__)

char g_cache_dir[MAX_PATH];
const PluginHostInterface *g_host;
void handle_shipmodel(ClientContext *ctx, RequestParams *params);

int http_routes_count = 1;
const HttpRouteRule http_routes[] = {
    {"/shipmodel", handle_shipmodel}
};

void handle_shipmodel(ClientContext *ctx, RequestParams *params) {
    char filename[128];
    snprintf(filename, sizeof(filename), "%s/shipmodel_%d.json",g_cache_dir, params->id);
    if (0 == g_host){
        return;
    }
    if (1){
    //if (!file_exists_recent(filename, 60)) {
        logmsg("Generating new shipmodel JSON: %s", filename);
        /*
        if (start_map_context()) {
            logmsg("Failed to start map context");
            send_response(ctx->socket_fd, 500, "text/plain", "Failed to start map context\n");
            return;
        }
        */
        // Generate ship model here
        generate_plans();
        ShipModelParams *smparams = malloc(sizeof(ShipModelParams));
        smparams->id = params->id;
        synthesize_shipmodel(smparams, get_plan(params->id));
        int vertex_count_limit = 10000;
        int json_size_limit = 100* vertex_count_limit* 100;
        char *json= malloc(10 + vertex_count_limit * 100); // Rough estimate for space
        int offset = 0;
        offset += snprintf(json + offset, json_size_limit - offset, "{");
        offset += snprintf(json + offset, json_size_limit - offset, "  \"components\": [");
        offset += snprintf(json + offset, json_size_limit - offset, "    {");
        offset += snprintf(json + offset, json_size_limit - offset, "      \"type\": \"shipmodel\",");
        offset += snprintf(json + offset, json_size_limit - offset, "      \"id\": %d,", params->id);
        /* //later
        offset += snprintf(json + offset, json_size_limit - offset, "      \"name\": \"Ship Model\",");
        offset += snprintf(json + offset, json_size_limit - offset, "      \"description\": \"A ship model for testing purposes\","); 
        offset += snprintf(json + offset, json_size_limit - offset, "      \"scale\": 1.0,");
        offset += snprintf(json + offset, json_size_limit - offset, "      \"position\": [0.0, 0.0, 0.0],");
        offset += snprintf(json + offset, json_size_limit - offset, "      \"rotation\": [0.0, 0.0, 0.0],");*/

        offset += snprintf(json + offset, json_size_limit - offset, "\n \"vertices\": [");
        // Add vertices here
        for(int i = 0; i < smparams->vertex_count; i++) {
            offset += snprintf(json + offset, json_size_limit - offset, "[%.2f, %.2f, %.2f],", smparams->px[i], smparams->py[i], smparams->pz[i]);
            //if ((i&7)==7) offset += snprintf(json + offset, json_size_limit - offset, "\n");
        }
        if (offset > 1) json[offset - 1] = ']'; // replace last comma with closing brace
        else strcpy(json + offset, "]");
        offset += snprintf(json + offset, json_size_limit - offset, ",");
        offset += snprintf(json + offset, json_size_limit - offset, "\n \"faces\": [");
        // Add faces here
        for(int i = 0; i < smparams->face_count; i++) {
            offset += snprintf(json + offset, json_size_limit - offset, "[%d, %d, %d],", smparams->fa[i], smparams->fb[i], smparams->fc[i]);
            //if ((i&7)==7) offset += snprintf(json + offset, json_size_limit - offset, "\n");
        }
        if (offset > 1) json[offset - 1] = ']'; // replace last comma with closing brace
        else strcpy(json + offset, "]");
        offset += snprintf(json + offset, json_size_limit - offset, ",");
        offset += snprintf(json + offset, json_size_limit - offset, "\n \"colors\": [");
        // Add colors here
        for(int i = 0; i < smparams->vertex_count; i++) {
            int colorindex = smparams->color_indices[i];
            offset += snprintf(json + offset, json_size_limit - offset, "[%d, %d, %d],",
                smparams->r[colorindex], smparams->g[colorindex], smparams->b[colorindex]);
            //if ((i&7)==7) offset += snprintf(json + offset, json_size_limit - offset, "\n");
        }
        if (offset > 1) json[offset - 1] = ']'; // replace last comma with closing brace
        else strcpy(json + offset, "]");
        offset += snprintf(json + offset, json_size_limit - offset, "}\n");
        offset += snprintf(json + offset, json_size_limit - offset, "]\n}\n");
        json[offset+1] = '\0'; // null-terminate the string
        send_response(ctx->socket_fd, 200, "application/json", json);
        logmsg("%s shipmodel request", ctx->client_ip);
        FILE *fp = fopen(filename, "w");
        if (!fp) {
            logmsg("Failed to open shipmodel file for writing");
        } else {
            fwrite(json, 1, strlen(json), fp);
            fclose(fp);
        }
        free_shipmodel(smparams);
        free(smparams);
        free(json);
    }
    else {
        logmsg("Using cached shipmodel json: %s", filename);
        send_file(ctx->socket_fd, "application/json", filename);
    }
}

void handle_shape(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc; // Unused parameter
    (void)params; // Unused parameter
    (void)ctx; // Unused parameter
    // Handle specific paths
    for (int i = 0; i < http_routes_count; i++) {
        if (strcmp(http_routes[i].path, params->path) == 0) {
            http_routes[i].handler(ctx, params);
            return;
       }
    }
    const char *body = "{\"msg\":\"Unknown sub path\"}";
    send_response(ctx->socket_fd, 200, "application/json", body);
    logmsg("%s shape request", ctx->client_ip);
}

const char* plugin_http_get_routes[]={"/shipmodel", NULL};
int plugin_http_get_routes_count = 1;

int plugin_register(PluginContext *pc, const PluginHostInterface *host) {
    g_host = host;
    host->server.register_http_route((void*)pc, plugin_http_get_routes_count, plugin_http_get_routes);
    return PLUGIN_SUCCESS;
}

int plugin_init(PluginContext* pc, const PluginHostInterface *host) {
    // Initialization code here
    g_host = host;
    g_host->config_get_string("CACHE", "dir", g_cache_dir, MAX_PATH, CACHE_DIR);
    pc->http.request_handler = (void*) handle_shape;
    return PLUGIN_SUCCESS;
}
void plugin_finish(PluginContext* pc) {
    // Cleanup code here
    pc->http.request_handler = NULL;
    // Free any allocated resources
}