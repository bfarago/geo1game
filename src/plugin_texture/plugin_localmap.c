#include "global.h"
#include "plugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846 /* pi */
#endif

const PluginHostInterface *g_host;
static const char *g_routes[] = { "/localmap", "/localelevation", "/localcloud" };
char g_cache_dir[MAX_PATH];

static inline void setPixel(unsigned char *row, unsigned long pixel_offset, int mode, TerrainInfo* info) {
    switch(mode){
        case 1:
            {
                int elevation = 0;
                if (info) elevation= info->elevation * 255.0f;
                if (elevation < 0) elevation = 0;
                if (elevation > 255) elevation = 255;
                row[pixel_offset] = elevation;
            }
            break;
        case 2:
            {
                int precip=0;
                if (info) precip= info->precip;
                row[pixel_offset + 0] = 255;
                row[pixel_offset + 1] = 255;
                row[pixel_offset + 2] = 255;
                row[pixel_offset + 3] = precip;
            }
            break;
        default:
        case 0:
            if (info){
                row[pixel_offset + 0] = info->r;
                row[pixel_offset + 1] = info->g;
                row[pixel_offset + 2] = info->b;
            }else{
                row[pixel_offset + 0] = 0;
                row[pixel_offset + 1] = 0;
                row[pixel_offset + 2] = 0;
            }
            break;

    }
}
void handle_localmap(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc;
    int mode=0;
    const char *fname;
    int pixel_size;
    ImageFormat image_format;
    for (int i = 0; i < 3; i++) {
        if (strcmp(g_routes[i], params->path) == 0) {
            mode=i;
            break;
       }
    }
    switch (mode) {
        case 1:
            fname= "localelevation";
            image_format= ImageFormat_Grayscale;
            pixel_size=1;
            break;
        case 2:
            fname= "localcloud";
            image_format= ImageFormat_RGBA;
            pixel_size=4;
            break;
        case 0:
        default:
            fname= "localmap";
            image_format= ImageFormat_RGB;
            pixel_size=3;
            break;
        
    }
    char filename[MAX_PATH];
    snprintf(filename, sizeof(filename), "%s/%s_lat%.2f_lon%.2f_r%.1f_%dx%d.png",
        g_cache_dir,
        fname,
        params->lat_min, params->lon_min, params->radius,
        params->width, params->height);

    if (!g_host->file_exists_recent(filename, CACHE_TIME)) {
        g_host->logmsg("Generating local top-down map: %s", filename);

        if (g_host->map.start_map_context()) {
            g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Map context start failed\n");
            return;
        }

        PluginContext *pcimg = g_host->get_plugin_context("image");
        if (pcimg) {
            g_host->image.context_start(pcimg);
            Image img;

            if (!g_host->image.create(pcimg, &img, filename, params->width, params->height,
                                       ImageBackend_Png, image_format, ImageBuffer_AoS)) {
                float lat0 = params->lat_min;
                float lon0 = params->lon_min;
                float delta = params->radius;
                unsigned char *row = malloc( pixel_size * img.width );
                    
                // Project pixels to lat/lon using polar-distance-preserving (great-circle) approximation
                for (unsigned int y = 0; y < img.height; y++) {
                    for (unsigned int x = 0; x < img.width; x++) {
                        unsigned long pixel_offset= x * pixel_size;
                        float dx = ((float)x / (img.width - 1) - 0.5f) * 2.0f * delta;
                        float dy = ((float)y / (img.height - 1) - 0.5f) * 2.0f * delta;
                        float distance = sqrtf(dx * dx + dy * dy);
                        float angle = atan2f(dy, dx);
                        //float lat_rad = lat0 * (float)M_PI / 180.0f;
                        //float lat = lat0 + (distance * cosf(angle));
                        //float lon = lon0 + (distance * sinf(angle)) / cosf(lat_rad);
                        // Convert center point to radians
                        float lat0_rad = lat0 * (float)M_PI / 180.0f;
                        float lon0_rad = lon0 * (float)M_PI / 180.0f;
                        // Angular distance in radians
                        float angular_distance = distance * (float)M_PI / 180.0f;
                        if (angular_distance > M_PI / 2.0f) {
                            // túl messze van, nem látható a felszín (pl. világűr)
                            setPixel(row, pixel_offset, mode, NULL);
                            continue;
                        }
                        // Compute new latitude using spherical law of cosines
                        float lat_rad = asinf(sinf(lat0_rad) * cosf(angular_distance) +
                        cosf(lat0_rad) * sinf(angular_distance) * cosf(angle));

                        // Compute new longitude
                        float lon_rad = lon0_rad + atan2f(sinf(angle) * sinf(angular_distance) * cosf(lat0_rad),
                        cosf(angular_distance) - sinf(lat0_rad) * sinf(lat_rad));

                        // Convert back to degrees
                        float lat = lat_rad * 180.0f / (float)M_PI;
                        float lon = lon_rad * 180.0f / (float)M_PI;

                        // Normalize lon to [-180,180)
                        lon = fmodf(lon + 180.0f, 360.0f);
                        if (lon < 0) lon += 360.0f;
                        lon -= 180.0f;
                        
                        
                        lat = round(lat * 10.0f) / 10.0f;
                        lon = round(lon * 10.0f) / 10.0f;

                        TerrainInfo info;
                        g_host->map.get_map_info(&info, lat, lon);
                        setPixel(row, pixel_offset, mode, &info);
                    }
                    g_host->image.write_row(pcimg, &img, row);
                    
                }
                free(row);
                g_host->image.destroy(pcimg, &img);
                g_host->logmsg("Local map PNG generated: %s", filename);
            }
            g_host->image.context_stop(pcimg);
        }
        g_host->map.stop_map_context();
    } else {
        g_host->logmsg("Using cached local map: %s", filename);
    }
    g_host->http.send_file(ctx->socket_fd, "image/png", filename);
}

void handle_http(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    handle_localmap(pc, ctx, params);
    g_host->http.send_response(ctx->socket_fd, 404, "text/plain", "Unknown path\n");
}

int plugin_register(PluginContext *pc, const PluginHostInterface *host) {
    g_host = host;

    host->server.register_http_route(pc, 3, g_routes);
    return 0;
}

int plugin_init(PluginContext *pc, const PluginHostInterface *host) {
    g_host = host;
    g_host->config_get_string("CACHE", "dir", g_cache_dir, MAX_PATH, CACHE_DIR);
    pc->http.request_handler = (void *)handle_http;
    return PLUGIN_SUCCESS;
}

void plugin_finish(PluginContext *pc) {
    pc->http.request_handler = NULL;
}
