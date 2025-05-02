/**
 * @file plugin_region.c
 * @brief Region table
 */
#define _GNU_SOURCE
#include "global.h"
#include "plugin.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define MAP_API_ENABLED 1
#define REGIONS_BINFILE_ENABLED 1
void handle_map_json(PluginContext *pc, ClientContext *ctx, RequestParams *params) ;
void handle_region_status(PluginContext *pc, ClientContext *ctx, RequestParams *params) ;
void handle_regions_chunk_json(PluginContext *pc, ClientContext *ctx, RequestParams *params);

const PluginHostInterface *g_host;
static void (*g_http_handlers[])(PluginContext *, ClientContext *, RequestParams *) = {
    handle_map_json, handle_regions_chunk_json, handle_region_status
};
const char* g_http_routes[]={"/map", "/regions_chunk", "/region"};
int g_http_routes_count = 3;
char g_cache_dir[MAX_PATH];
#ifdef REGIONS_BINFILE_ENABLED
typedef struct {
    float lat, lon;
    float elevation;
    unsigned char r, g, b;
    unsigned char polution;
    char name[128];
} RegionsDataRecord;
#endif //REGIONS_BINFILE_ENABLED

int get_region_cache_filename(char*buf, int buflen, 
    const char *dir, const char* fname,
    float lat_min, float lon_min, float lat_max, float lon_max, float step)
{
    return snprintf(buf, buflen, "%s/%s_lat%.2f_lon%.2f_lat%.2f_lon%.2f_step%.4f.json",
         dir, fname, lat_min, lon_min, lat_max, lon_max, step);
}

void handle_region_status(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc; // Unused parameter
    (void)params; // Unused parameter
    (void)ctx; // Unused parameter
    const char *body = "{\"msg\":\"Hello from plugin!\"}";
    g_host->http.send_response(ctx->socket_fd, 200, "application/json", body);
    g_host->logmsg("%s hello request", ctx->client_ip);
}

void handle_map_json(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc; // Unused parameter
    double lat_min = params->lat_min;
    double lat_max = params->lat_max;
    double lon_min = params->lon_min;
    double lon_max = params->lon_max;
    double step = params->step > 0 ? params->step : 0.5;

    char cache_filename[MAX_PATH];
    get_region_cache_filename(cache_filename, MAX_PATH, 
        g_cache_dir, "map", lat_min, lon_min, lat_max, lon_max, step);
    if (g_host->file_exists_recent(cache_filename, CACHE_TIME)){
        g_host->http.send_file(ctx->socket_fd, "application/json", cache_filename);
        return;
    }
    
    if (lat_min >= lat_max || lon_min >= lon_max || step <= 0) {
        g_host->http.send_response(ctx->socket_fd, 400, "application/json", NULL);
        return;
    }
    if (g_host->map.start_map_context()){
        g_host->logmsg("Failed to start map context");
        g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Failed to start map context\n");
        return;   
    }
    FILE *fp = fopen(cache_filename, "w");
    fprintf(fp,"{\n");
    int elementnr=0;
    for (float lat = lat_min; lat <= lat_max; lat += step) {
        for (float lon = lon_min; lon <= lon_max; lon += step) {
            TerrainInfo info;
            if (elementnr++){
                fprintf(fp, ",");
            }
            g_host->map.get_map_info(&info, lat, lon);
            fprintf(fp, "\"%.2f,%.2f\":{\"r\":%d,\"g\":%d,\"b\":%d,\"e\":%.2f}",
                     lat, lon, info.r, info.g, info.b, info.elevation);
        }
    }
    g_host->map.stop_map_context();
    fprintf(fp, "}\n");
    fclose(fp);
    g_host->http.send_file(ctx->socket_fd, "application/json", cache_filename);
    g_host->logmsg("%s map_json request", ctx->client_ip);
}

#ifdef MAP_API_ENABLED
int region_create_binfile(const char *fname) {
    FILE *fp = fopen(fname, "wb");
    if (!fp) {
        g_host->logmsg("Failed to open regions file for writing");
        return -1;
    }
    g_host->logmsg("Generating new regions file: %s", fname);
    //EZT
    g_host->map.start_map_context();
    for (float lat = -70.0f; lat <= 70.0f; lat += 0.5f) {
        for (float lon = -180.0f; lon <= 180.0f; lon += 0.5f) {
            TerrainInfo info ;
            g_host->map.get_map_info(&info,lat, lon);
            if (info.elevation > 0.0f && info.elevation < 0.6f) {
                int needed  = rand() % 200;
                if (needed == 1) {
                    RegionsDataRecord region;
                    region.lat = lat;
                    region.lon = lon;
                    region.elevation = info.elevation;
                    region.r = info.r;
                    region.g = info.g;
                    region.b = info.b;
                    region.polution = 255-info.precip;
                    snprintf(region.name, sizeof(region.name), "city-%04d", rand()%10000);
                    fwrite(&region, sizeof(RegionsDataRecord), 1, fp);
                }
            }
        }
    }
    g_host->map.stop_map_context();
    fclose(fp);
    return 0;
}
#endif // MAP_API_ENABLED

#ifdef REGIONS_BINFILE_ENABLED
int binfile_based_json(PluginContext *pc, ClientContext *ctx, RequestParams *params, const char* cache_filename) {
    (void)pc; // Unused parameter
    FILE *fp = fopen(REGIONS_FILE, "rb");
    if (fp){
        #define REGIONS_JSON_SIZE_LIMIT (1024*1024*10)
        char *json = malloc(REGIONS_JSON_SIZE_LIMIT); // Rough estimate for space
        if (!json) {
            g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Failed to allocate memory for JSON");
            return 1;
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
                    int keyval_len = snprintf(keyval, sizeof(keyval), "\"%.2f,%.2f\":{\"r\":%d,\"g\":%d,\"b\":%d,\"e\":%.2f,\"p\":%d,\"name\":\"%s\"},",
                             region.lat, region.lon, region.r, region.g, region.b, region.elevation, region.polution, region.name);
                    if (offset + keyval_len +1 > REGIONS_JSON_SIZE_LIMIT) {
                        g_host->logmsg("Regions JSON size limit exceeded");
                        g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Regions JSON size limit exceeded");
                        free(json);
                        fclose(fp);
                        return 2;
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
        g_host->http.send_response(ctx->socket_fd, 200, "application/json", json);
        g_host->logmsg("%s regions_chunk_json request", ctx->client_ip);
        
        FILE *fp_out = fopen(cache_filename, "w");
        if (!fp_out) {
            g_host->logmsg("Failed to open regions file for writing");
        }else{
            fwrite(json, 1, strlen(json), fp_out);
            fclose(fp_out);
        }
        free(json);
        fclose(fp);
    } else {
        g_host->logmsg("Failed to open regions file for reading");
        return 3;
    }
    return 0;
}
#endif //REGIONS_BINFILE_ENABLED

void handle_regions_chunk_json(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    char cache_filename[MAX_PATH];
    get_region_cache_filename(cache_filename, sizeof(cache_filename),
        g_cache_dir, "regions",
        params->lat_min, params->lon_min, params->lat_max, params->lon_max, params->step);
    if (g_host->file_exists_recent(cache_filename, CACHE_TIME)) {
        g_host->logmsg("Regions chunk JSON file is up to date: %s", cache_filename);
        g_host->http.send_file(ctx->socket_fd, "application/json", cache_filename);
        return;
    }
    #ifdef REGIONS_BINFILE_ENABLED
    if (!g_host->file_exists(REGIONS_FILE)) {
        if (!region_create_binfile(REGIONS_FILE)) {
            g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Failed to open regions file for writing\n");
            return;
        }
    }
    binfile_based_json(pc, ctx, params, cache_filename);
    #endif //REGIONS_BINFILE_ENABLED
    
}
void handle_region(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc; // Unused parameter
    (void)params; // Unused parameter
    (void)ctx; // Unused parameter
    // Handle specific paths
    for (int i = 0; i < g_http_routes_count; i++) {
        if (strcmp(g_http_routes[i], params->path) == 0) {
            g_http_handlers[i](pc, ctx, params);
            return;
       }
    }
    const char *body = "{\"msg\":\"Unknown sub path\"}";
    g_host->http.send_response(ctx->socket_fd, 200, "application/json", body);
    g_host->logmsg("%s region request", ctx->client_ip);
}
int plugin_register(PluginContext *pc, const PluginHostInterface *host) {
    g_host = host;
    host->server.register_http_route((void*)pc, g_http_routes_count, g_http_routes);
    return PLUGIN_SUCCESS;
}

int plugin_init(PluginContext* pc, PluginHostInterface *host) {
    g_host = host;
    g_host->config_get_string("CACHE", "dir", g_cache_dir, MAX_PATH, CACHE_DIR);
    pc->http.request_handler = (void*) handle_region;
    return PLUGIN_SUCCESS;
}
void plugin_finish(PluginContext* pc) {
    // Cleanup code here
    pc->http.request_handler = NULL;
    // Free any allocated resources
}