#include "image.h"
#include "plugin.h"
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <png.h>


typedef struct {
    unsigned long width, height;
    FILE *fp;
    png_structp png_ptr;
    png_infop info_ptr;
    const char *filename;
    char tmp_filename[MAX_PATH];
} PngImage;

#if PNG_LIBPNG_VER >= 10600
    // libpng 1.6.40 and later
    #define HAVE_OLD_PNG 0
    #define PNG_CRITICAL_START
    #define PNG_CRITICAL_END
#else
    // libpng 1.4.0 and earlier
    #define HAVE_OLD_PNG 1
    // multithread safe before v1.4.0 see github issue
    pthread_mutex_t png_mutex = PTHREAD_MUTEX_INITIALIZER;
    #define PNG_CRITICAL_START() image_mutex_timedlock(&png_mutex, 20000UL)
    #define PNG_CRITICAL_END() pthread_mutex_unlock(&png_mutex)
#endif

const PluginHostInterface *g_host;
void handle_image(PluginContext *ctx, ClientContext *client, RequestParams *params);
static void (*g_http_handlers[])(PluginContext *, ClientContext *, RequestParams *) = {
    handle_image
};
const char* g_http_routes[]={"/image"};
int g_http_routes_count = 1;

/** The backend PNG related functions. */
void print_png_version() {
    if (g_host) g_host->debugmsg("libpng version (static, dynamic, mutex): %s, %s, %s", PNG_LIBPNG_VER_STRING, png_libpng_ver, HAVE_OLD_PNG ? "yes" : "no");
}
int image_mutex_timedlock(pthread_mutex_t *lock, const unsigned long timeout_ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    int res= pthread_mutex_timedlock(lock, &ts);
    if (res == ETIMEDOUT) {
        g_host->errormsg("Mutex lock timed out");
    } else if (res != 0) {
        g_host->errormsg("Mutex lock failed: %d", res);
    }   
    return res;
}
int PngImage_init(PngImage *img, int width, int height, const char *filename, unsigned char color_type) {
    img->width = width;
    img->height = height;
    img->filename = filename;
    img->tmp_filename[0] = '\0';
    snprintf(img->tmp_filename, sizeof(img->tmp_filename), "%s_", filename);
    g_host->debugmsg("Opening PNG file for writing. %s", img->tmp_filename);
    img->fp = fopen(img->tmp_filename, "wb");
    if (!img->fp) {
        g_host->errormsg("Failed to open PNG file for writing. %s", filename);
        return 1;
    }
    PNG_CRITICAL_START();
    img->png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    img->info_ptr = png_create_info_struct(img->png_ptr);
    if (!img->png_ptr || !img->info_ptr) {
        fclose(img->fp);
        g_host->errormsg("Failed to create PNG structures.");
        return 2;
    }
    png_init_io(img->png_ptr, img->fp);
    png_set_IHDR(img->png_ptr, img->info_ptr, img->width, img->height,
                 8, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(img->png_ptr, img->info_ptr);
    return 0;
}
void PngImage_finish(PngImage *img) {
    g_host->debugmsg("Finished writing PNG file. %s to %s", img->tmp_filename, img->filename);
    png_write_end(img->png_ptr, NULL);
    png_destroy_write_struct(&img->png_ptr, &img->info_ptr);
    fclose(img->fp);
    int res=rename(img->tmp_filename, img->filename);
    if (res != 0) {
        g_host->errormsg("Failed to rename %s to %s", img->tmp_filename, img->filename);
    }
    PNG_CRITICAL_END();
}

/** Image */
int image_create(PluginContext *pc, Image* img, const char *filename, unsigned int width, unsigned int height, ImageBackendType backend, ImageFormat format, ImageBufferFormat buffer_type){
    (void)pc;
    img->backend=backend;
    img->format=format;
    img->buffer_format=buffer_type;
    img->width=width;
    img->height=height;
    if (backend == ImageBackend_Png) {
        int pngcolor;
        switch(format){
            case ImageFormat_RGBA: pngcolor=PNG_COLOR_TYPE_RGBA; break;
            case ImageFormat_Grayscale: pngcolor=PNG_COLOR_TYPE_GRAY; break;
            case ImageFormat_RGB:
            default:
                pngcolor=PNG_COLOR_TYPE_RGB; break;
        }
        PngImage *pngimg= malloc(sizeof(PngImage));
        if (PngImage_init(pngimg, width, height, filename, pngcolor)) {
            g_host->errormsg("Failed to initialize PNG image.");
            return -2;
        }
        img->backend_data=pngimg;
        return 0;
    }else{
        g_host->errormsg("Backend not supported");
        return -1;
    }
    return -1;
}
int image_destroy(PluginContext *pc, Image* img){
    (void)pc; // Unused parameter
    if (img->backend == ImageBackend_Png) {
        PngImage *pngimg=(PngImage*)img->backend_data;
        g_host->logmsg("PngImage_finish: %s", pngimg->filename);
        PngImage_finish(pngimg);
        free(pngimg);
    }
    return 0;
}
void image_get_buffer(PluginContext *pc, Image* img, void** buffer){
    (void)pc; // Unused parameter
    (void)img; // Unused parameter
    (void)buffer; // Unused parameter
}
void image_write_row(PluginContext *pc, Image* img, void* row){
    (void)pc; // Unused parameter
    if (img->backend == ImageBackend_Png) {
        PngImage *pngimg=(PngImage*)img->backend_data;
        png_write_row(pngimg->png_ptr, row);
    }
}

void handle_image(PluginContext *pc, ClientContext *ctx, RequestParams *params){
    (void)pc; // Unused parameter
    (void)ctx; // Unused parameter
    (void)params; // Unused parameter
    char body[1024];
    snprintf(body, 1024, "{\"libpng version\":\"%s\"}", png_libpng_ver );
    g_host->http.send_response(ctx->socket_fd, 200, "application/json", body);
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
    g_host->logmsg("%s image request", ctx->client_ip);
}
int plugin_register(PluginContext *pc, const PluginHostInterface *host) {
    g_host = host;
    host->http.register_http_route((void*)pc, g_http_routes_count, g_http_routes);
    return PLUGIN_SUCCESS;
}

int plugin_init(PluginContext* pc, const PluginHostInterface *host) {
    g_host = host;
    pc->http.request_handler = (void*) handle_http;
    pc->image.create = image_create;
    pc->image.destroy = image_destroy;
    pc->image.get_buffer = image_get_buffer;
    pc->image.write_row = image_write_row;
    return PLUGIN_SUCCESS;
}
void plugin_finish(PluginContext* pc) {
    // Cleanup code here
    pc->http.request_handler = NULL;
    // Free any allocated resources
}