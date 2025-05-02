#include "global.h"
#include "plugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846 /* pi */
#endif

typedef struct {
    float x, y, z;
} vec3;

char g_cache_dir[MAX_PATH];

static vec3 vec3_add(vec3 a, vec3 b) {
    return (vec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

static vec3 vec3_mul(vec3 v, float s) {
    return (vec3){v.x * s, v.y * s, v.z * s};
}

static float vec3_dot(vec3 a, vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float vec3_length2(vec3 v) {
    return vec3_dot(v, v);
}

static vec3 latlon_to_vec3(float lat, float lon) {
    float lat_rad = lat * M_PI / 180.0f;
    float lon_rad = lon * M_PI / 180.0f;
    return (vec3){
        cosf(lat_rad) * cosf(lon_rad),
        sinf(lat_rad),
        cosf(lat_rad) * sinf(lon_rad)
    };
}

static float vec3_to_lat(vec3 v) {
    return asinf(v.y) * 180.0f / M_PI;
}

static float vec3_to_lon(vec3 v) {
    return atan2f(v.z, v.x) * 180.0f / M_PI;
}

// Compute ray direction based on camera tangent plane
static vec3 compute_tangent_ray_direction(vec3 cam_pos, float azimuth_rad, float elevation_rad) {
    // Normalize camera position to get "up" direction
    float cam_len = sqrtf(vec3_length2(cam_pos));
    vec3 up = vec3_mul(cam_pos, 1.0f / cam_len);

    // Define local tangent frame: up, east, north
    vec3 global_y = {0.0f, 1.0f, 0.0f};
    vec3 east = {
        up.y * global_y.z - up.z * global_y.y,
        up.z * global_y.x - up.x * global_y.z,
        up.x * global_y.y - up.y * global_y.x
    };

    float east_len = sqrtf(vec3_length2(east));
    if (east_len < 1e-6f) {
        // fallback if pole
        global_y = (vec3){1.0f, 0.0f, 0.0f};
        east = (vec3){
            up.y * global_y.z - up.z * global_y.y,
            up.z * global_y.x - up.x * global_y.z,
            up.x * global_y.y - up.y * global_y.x
        };
        east_len = sqrtf(vec3_length2(east));
    }
    east = vec3_mul(east, 1.0f / east_len);

    vec3 north = {
        east.y * up.z - east.z * up.y,
        east.z * up.x - east.x * up.z,
        east.x * up.y - east.y * up.x
    };

    // Compute direction in tangent space
    vec3 dir_tangent = {
        cosf(elevation_rad) * cosf(azimuth_rad),
        sinf(elevation_rad),
        cosf(elevation_rad) * sinf(azimuth_rad)
    };

    // Transform tangent space to world space
    vec3 dir_world = {
        dir_tangent.x * east.x + dir_tangent.y * up.x + dir_tangent.z * north.x,
        dir_tangent.x * east.y + dir_tangent.y * up.y + dir_tangent.z * north.y,
        dir_tangent.x * east.z + dir_tangent.y * up.z + dir_tangent.z * north.z
    };

    float len = sqrtf(vec3_length2(dir_world));
    return vec3_mul(dir_world, 1.0f / len);
}
const PluginHostInterface *g_host;

void handle_biome(PluginContext *pc, ClientContext *ctx, RequestParams *params);
void handle_elevation(PluginContext *pc, ClientContext *ctx, RequestParams *params);
void handle_clouds(PluginContext *pc, ClientContext *ctx, RequestParams *params);
void handle_pano(PluginContext *pc, ClientContext *ctx, RequestParams *params);

static void (*g_http_handlers[])(PluginContext *, ClientContext *, RequestParams *) = {
    handle_biome, handle_elevation, handle_clouds, handle_pano
};
const char* g_http_routes[]={"/biome", "/elevation", "/clouds", "/pano"};
int g_http_routes_count = 4;

void handle_biome(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc;
    char filename[MAX_PATH];
    snprintf(filename, sizeof(filename), "%s/biome_lat%.2f_lon%.2f_%dx%d.png",
        g_cache_dir,
        params->lat_min, params->lon_min, 
        params->width, params->height);
    if (!g_host->file_exists_recent(filename, CACHE_TIME)) {
        g_host->logmsg("Generating new biome PNG: %s", filename);
        
        if (g_host->map.start_map_context()) {
            g_host->logmsg("Failed to start map context");
            g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Failed to start map context\n");
            return;
        }
        PluginContext *pcimg= g_host->get_plugin_context("image");
        if (pcimg){
            g_host->logmsg("Starting image context");
            g_host->image.context_start(pcimg);
//            image_context_start(pcimg);
            Image img;
            int res=g_host->image.create(pcimg, &img, filename, params->width, params->height, ImageBackend_Png, ImageFormat_RGB, ImageBuffer_AoS);
            if (!res){
                for (unsigned int y = 0; y < img.height; y++) {
                    unsigned char *row = malloc(3 * img.width);
                    float lat = params->lat_max - ((params->lat_max - params->lat_min) / img.height) * y;
                    for (unsigned int x = 0; x < img.width; x++) {
                        float lon = params->lon_min + ((params->lon_max - params->lon_min) / img.width) * x;
                        TerrainInfo info;
                        g_host->map.get_map_info(&info, lat, lon);
                        row[x*3 + 0] = info.r;
                        row[x*3 + 1] = info.g;
                        row[x*3 + 2] = info.b;
                    }
                    g_host->image.write_row(pcimg, &img, row); //png_write_row(img.png_ptr, row);
                    free(row);
                }
                g_host->image.destroy(pcimg, &img);  // PngImage_finish(&img);
                g_host->logmsg("Biome PNG generated: %s", filename);
            }else{
                g_host->logmsg("Failed to create PNG image: %s", filename);
            }
            g_host->image.context_stop(pcimg);
        }else{
            g_host->logmsg("Failed to start image context");
        }
        g_host->map.stop_map_context();
    } else {
        g_host->logmsg("Using cached Biome PNG: %s", filename);
    }
    g_host->http.send_file(ctx->socket_fd, "image/png", filename);
}

void handle_elevation(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc;
    char filename[MAX_PATH];
    snprintf(filename, sizeof(filename), "%s/elevation_lat%.2f_lon%.2f_%dx%d.png",
        g_cache_dir,
        params->lat_min, params->lon_min, 
        params->width, params->height);
    if (!g_host->file_exists_recent(filename, CACHE_TIME)) {
        g_host->logmsg("Generating new elevation PNG: %s", filename);
        
        if (g_host->map.start_map_context()) {
            g_host->logmsg("Failed to start map context");
            g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Failed to start map context\n");
            return;
        }
        PluginContext *pcimg= g_host->get_plugin_context("image");
        if (pcimg){
            g_host->image.context_start(pcimg);
            Image img;
            if (!g_host->image.create(pcimg, &img, filename, params->width, params->height, ImageBackend_Png, ImageFormat_Grayscale, ImageBuffer_AoS)){
                for (unsigned int y = 0; y < img.height; y++) {
                    unsigned char *row = malloc(1 * img.width);
                    float lat = params->lat_max - ((params->lat_max - params->lat_min) / img.height) * y;
                    for (unsigned int x = 0; x < img.width; x++) {
                        float lon = params->lon_min + ((params->lon_max - params->lon_min) / img.width) * x;
                        TerrainInfo info;
                        g_host->map.get_map_info(&info, lat, lon);
                        int elevation = info.elevation * 255.0f;
                        if (elevation < 0) elevation = 0;
                        if (elevation > 255) elevation = 255;
                        row[x] = elevation;
                    }
                    g_host->image.write_row(pcimg, &img, row); //png_write_row(img.png_ptr, row);
                    free(row);
                }
                g_host->image.destroy(pcimg, &img);  // PngImage_finish(&img);
                g_host->logmsg("Elevation PNG generated: %s", filename);
            }
            g_host->image.context_stop(pcimg);
        }
        g_host->map.stop_map_context();
    } else {
        g_host->logmsg("Using cached Elevation PNG: %s", filename);
    }
    g_host->http.send_file(ctx->socket_fd, "image/png", filename);
}

void handle_clouds(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc;
    char filename[MAX_PATH];
    snprintf(filename, sizeof(filename), "%s/clouds_lat%.2f_lon%.2f_%dx%d.png",
        g_cache_dir,
        params->lat_min, params->lon_min, 
        params->width, params->height);
    if (!g_host->file_exists_recent(filename, CACHE_TIME)) {
        g_host->logmsg("Generating new clouds PNG: %s", filename);
        
        if (g_host->map.start_map_context()) {
            g_host->logmsg("Failed to start map context");
            g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Failed to start map context\n");
            return;
        }
        PluginContext *pcimg= g_host->get_plugin_context("image");
        if (pcimg){
            g_host->image.context_start(pcimg);
            Image img;
            if (!g_host->image.create(pcimg, &img, filename, params->width, params->height, ImageBackend_Png, ImageFormat_RGBA, ImageBuffer_AoS)){
                for (unsigned int y = 0; y < img.height; y++) {
                    unsigned char *row = malloc(4 * img.width);
                    float lat = params->lat_max - ((params->lat_max - params->lat_min) / img.height) * y;
                    for (unsigned int x = 0; x < img.width; x++) {
                        float lon = params->lon_min + ((params->lon_max - params->lon_min) / img.width) * x;
                        TerrainInfo info;
                        g_host->map.get_map_info(&info, lat, lon);
                        row[x*4 + 0] = 255;
                        row[x*4 + 1] = 255;
                        row[x*4 + 2] = 255;
                        row[x*4 + 3] = info.precip;
                    }
                    g_host->image.write_row(pcimg, &img, row); //png_write_row(img.png_ptr, row);
                    free(row);
                }
                g_host->image.destroy(pcimg, &img);  // PngImage_finish(&img);
                g_host->logmsg("Clouds PNG generated: %s", filename);
            }
            g_host->image.context_stop(pcimg);
        }
        g_host->map.stop_map_context();
    } else {
        g_host->logmsg("Using cached Clouds PNG: %s", filename);
    }
    g_host->http.send_file(ctx->socket_fd, "image/png", filename);
}

void handle_pano(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc;
    char filename[MAX_PATH];
    snprintf(filename, sizeof(filename), "%s/pano_lat%.2f_lon%.2f_alt=%.4f_%dx%d.png",
        g_cache_dir,
        params->lat_min, params->lon_min, params->alt,
        params->width, params->height);
    if (!g_host->file_exists_recent(filename, CACHE_TIME)) {
        g_host->logmsg("Generating new clouds PNG: %s", filename);
        
        if (g_host->map.start_map_context()) {
            g_host->logmsg("Failed to start map context");
            g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Failed to start map context\n");
            return;
        }
        PluginContext *pcimg= g_host->get_plugin_context("image");
        if (pcimg){
            g_host->image.context_start(pcimg);
            Image img;
            if (!g_host->image.create(pcimg, &img, filename, params->width, params->height, ImageBackend_Png, ImageFormat_RGB, ImageBuffer_AoS)){
                float R = 1.0;                  // earth radius
                float Rcloud= 1.5;               // 1.2;              // cloud radius
                float lat0 = params->lat_min;   //camera standpoint latitude
                float lon0 = params->lon_min;   //camera standpoint longitude
                float alt0 = 0;                // ocean level
                TerrainInfo info_camera;
                g_host->map.get_map_info(&info_camera, lat0, lon0);
                alt0 = info_camera.elevation + params->alt;

                float W = params->width;        // panorama width
                float H = params->height;       // panorama height
                float vertical_fov = 20.0f;       // vertical field of view
//                float y_horizon = H * 0.5f; // A horizon vizuális középre helyezése, az alt0-t inkább a theta számításban vegyük figyelembe
                
                // Allocate arrays for max_theta and max_d by x
                float *max_theta_by_x = malloc(sizeof(float) * (int)W);
                float *max_d_by_x = malloc(sizeof(float) * (int)W);
                // Allocate array for max_color_by_x
                unsigned char (*max_color_by_x)[3] = malloc(sizeof(unsigned char[3]) * (int)W);
                unsigned char *terrain_hit_by_x = malloc(sizeof(unsigned char) * (int)W);
                for (int i = 0; i < W; i++) {
                    max_theta_by_x[i] = -INFINITY;
                    max_d_by_x[i] = 100.0f;
                    max_color_by_x[i][0] = 60;
                    max_color_by_x[i][1] = 50;
                    max_color_by_x[i][2] = 40;
                    terrain_hit_by_x[i] = 0;
                }
                for (unsigned int y = 0; y < img.height; y++) {
                    unsigned char *row = malloc(3 * img.width);
                    // Compute the elevation angle (from tangent plane) from the zenith_horizon_angle
                    float elevation_rad = (0.5f - (float)y / H) * vertical_fov * (M_PI / 180.0f);
                    int lcount = 0;
                    for (unsigned int x = 0; x < W; x++) {
                        float azimuth_rad = (float)x * 2.0 * M_PI / W;
                        unsigned char color[3] = {160, 150, 255}; // fallback sky color
                        vec3 cam_pos = vec3_mul(latlon_to_vec3(lat0, lon0), R + alt0);
                        vec3 dir = compute_tangent_ray_direction(cam_pos, azimuth_rad, elevation_rad);
                        if (terrain_hit_by_x[x] == 0) {
                            if (elevation_rad < 0.0f) {
                                // below horizon, terrain color is dominated first.
                                color[0] = 60;
                                color[1] = 50;
                                color[2] = 40;
                            } else {
                                float dotPD = vec3_dot(cam_pos, dir);
                                float cam_len2 = vec3_length2(cam_pos);
                                float under_root = dotPD * dotPD - (cam_len2 - Rcloud * Rcloud);
                                if (under_root > 0.0f) {
                                    float t = -dotPD + sqrtf(under_root);
                                    vec3 hit = vec3_add(cam_pos, vec3_mul(dir, t));
                                    float lat_cloud = roundf(vec3_to_lat(hit) * 10.0f) / 10.0f;
                                    float lon_cloud = roundf(vec3_to_lon(hit) * 10.0f) / 10.0f;
                                    if (lon_cloud > 180.0f) lon_cloud -= 360.0f;
                                    if (lon_cloud < -180.0f) lon_cloud += 360.0f;
                                    if (lat_cloud > 90.0f) lon_cloud -= 180.0f;
                                    if (lon_cloud < -90.0f) lon_cloud += 180.0f;
                                    TerrainInfo info;
                                    g_host->map.get_map_info(&info, lat_cloud, lon_cloud);

                                    if (info.precip > 2) {
                                        int shade = 255- info.precip;
                                        if (shade < 0) shade = 0;
                                        if (shade > 255) shade = 255;
                                        color[0] = shade;
                                        color[1] = shade;
                                        color[2] = 255;
                                    }
                                    if (0) //disabled for now
                                    if ((lcount++ % 10 == 0) && (lcount < 2000)) {
                                        g_host->logmsg("azm = %f, elev:%f, hit=(%.3f,%.3f,%.3f), lat_cloud = %.2f, lon_cloud = %.2f, precip = %.3f",
                                                    azimuth_rad, elevation_rad, hit.x, hit.y, hit.z, lat_cloud, lon_cloud, info.precip);
                                    }
                                }
                            }
                        }
                        // cloud-style spherical stepping for terrain elevation sampling
                        if (params->terrain) {
                            float dotPD = vec3_dot(cam_pos, dir);
                            float cam_len2 = vec3_length2(cam_pos);
                            float under_root = dotPD * dotPD - (cam_len2 - R * R);

                            if (under_root > 0.0f) {
//                                float t = -dotPD + sqrtf(under_root);
//                                vec3 hit = vec3_add(cam_pos, vec3_mul(dir, t));
                                // there where hit to the globe, so we need to sample the terrain, use  vec3_to_lat/lon
                            
                                for (float d = 0.1f; d <= max_d_by_x[x]; d += 0.1f) {
                                //for (float d = 0.1f; d <= t; d += 0.1f) {
                                    vec3 p = vec3_add(cam_pos, vec3_mul(dir, d));
                                    float lat = round(vec3_to_lat(p)* 10.0f) / 10.0f;
                                    float lon = round(vec3_to_lon(p)* 10.0f) / 10.0f;
                                    if (lon > 180.0f) lon -= 360.0f;
                                    if (lon < -180.0f) lon += 360.0f;
                                    if (lat > 90.0f) lon -= 180.0f;
                                    if (lon < -90.0f) lon += 180.0f;

                                    TerrainInfo info;
                                    g_host->map.get_map_info(&info, lat, lon);
                                    float elevation = info.elevation / d;
                                    float h = elevation - alt0;
                                    float theta = asinf(h / sqrtf(h * h + d * d));
                                    if (theta  >= max_theta_by_x[x]) {
                                        max_theta_by_x[x] = theta;
                                        max_d_by_x[x] = d;
                                        max_color_by_x[x][0] = info.r;
                                        max_color_by_x[x][1] = info.g;
                                        max_color_by_x[x][2] = info.b;
                                        terrain_hit_by_x[x] = 1U;
                                    }
                                }
                            }
                        }
                        // After stepping, update color if terrain was hit
                        if (terrain_hit_by_x[x]) {
                            color[0] = max_color_by_x[x][0];
                            color[1] = max_color_by_x[x][1];
                            color[2] = max_color_by_x[x][2];
                        }
                        row[x*3 + 0] = color[0];
                        row[x*3 + 1] = color[1];
                        row[x*3 + 2] = color[2];
                    }
                    g_host->image.write_row(pcimg, &img, row);
                    free(row);
                }
                free(max_theta_by_x);
                free(max_d_by_x);
                free(max_color_by_x);
                free(terrain_hit_by_x);
                g_host->image.destroy(pcimg, &img);
                g_host->logmsg("Pano PNG generated: %s", filename);
            }
            g_host->image.context_stop(pcimg);
        }
        g_host->map.stop_map_context();
    } else {
        g_host->logmsg("Using cached Pano PNG: %s", filename);
    }
    g_host->http.send_file(ctx->socket_fd, "image/png", filename);
}
void handle_http(PluginContext *pc, ClientContext *ctx, RequestParams *params){
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
    g_host->logmsg("%s texture request", ctx->client_ip);
}
int plugin_register(PluginContext *pc, const PluginHostInterface *host) {
    g_host = host;
    host->server.register_http_route((void*)pc, g_http_routes_count, g_http_routes);
    return PLUGIN_SUCCESS;
}

int plugin_init(PluginContext* pc, const PluginHostInterface *host) {
    g_host = host;
    g_host->config_get_string("CACHE", "dir", g_cache_dir, MAX_PATH, CACHE_DIR);
    pc->http.request_handler = (void*) handle_http;
    return PLUGIN_SUCCESS;
}
void plugin_finish(PluginContext* pc) {
    // Cleanup code here
    pc->http.request_handler = NULL;
    // Free any allocated resources
}