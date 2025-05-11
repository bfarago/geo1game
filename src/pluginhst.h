/*
 * File:    pluginhst.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-05-10
 * 
 * Plugin Host API interface
 * Key features:
 *  registration, host and plugin side APIs
 */
#ifndef PLUGINHST_H_
#define PLUGINHST_H_

typedef struct {
    pthread_t thread_id;
    sync_mutex_t *lock;
    unsigned char running;
    unsigned char mapgen_loaded;
    time_t last_mapgen_use;
} housekeeper_t;

extern housekeeper_t g_housekeeper;
extern char g_geod_plugin_dir[MAX_PATH];
extern const PluginHostInterface g_plugin_host;

extern int g_PluginCount;
extern PluginContext g_Plugins[MAX_PLUGIN];


// other internal forwards
void ws_hostside_handshake(PluginContext *pc, WsRequestParams* wsp, const char *msg);


//MAP
void closeSo();
int start_map_context(void);
int stop_map_context(void);
int get_map_info(TerrainInfo *info, float lat, float lon);

// image
int image_context_start(PluginContext *pc);
int image_context_stop(PluginContext *pc);
int image_create(PluginContext *pc, Image *image, const char *filename, unsigned int width, unsigned int height, ImageBackendType backend, ImageFormat format, ImageBufferFormat buffer_type);
int image_destroy(PluginContext *pc, Image *image);
void image_get_buffer(PluginContext *pc, Image *image, void** buffer);
void image_write_row(PluginContext *pc, Image *image, void *row);

void register_http_routes(PluginContext *ctx, int count, const char *routes[]);
void register_ws_routes(PluginContext *ctx, int count, const char *routes[]);
void register_control_routes(PluginContext *ctx, int count, const char *routes[]);
void register_db_queue(PluginContext *ctx, const char *db);


void plugin_scan_and_register();
void start_housekeeper();
void stop_housekeeper();
void housekeeper_server_clients(time_t now );
int pluginhst_det_str_dump(char* buf, int len);
#endif // PLUGINHST_H_