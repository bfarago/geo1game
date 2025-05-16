/*
 * File:    http.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * Hyper Text Transfer Protocol layer
 * Key features:
 * Collects the header and querery keys. 
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define HTTP_STATIC_LINKED

#include "global.h"
#include "sync.h"
#include "http.h"
#include "plugin.h"
#include "config.h"
#include "hashmap.h"

#define HTTP_ROUTE_LOCK_TIMEOUT (50) // 50ms


//just for debug
void logmsg(const char *fmt, ...) ;
void errormsg(const char *fmt, ...) ;
void debugmsg(const char *fmt, ...) ;

const char* get_status_text(int status_code){
    const char *status_text;
    switch (status_code) {
        case 200: status_text = "OK"; break;
        case 404: status_text = "Not Found"; break;
        case 500: status_text = "Internal Server Error"; break;
        default: status_text = "OK"; break;
    }
    return status_text;
}

int http_write(int client, const char *buf, size_t n){
    size_t total_written = 0;
    int retry = 0;
    while (total_written < n) {
        ssize_t written = write(client, buf + total_written, n - total_written);
        if (written <= 0) {
            if (++retry > 3) {
                errormsg("write failed repeatedly, aborting send_file()");
                return -1;
            }
            continue;
        }
        total_written += written;
        retry = 0; // Reset retry counter on successful write
    }
    return 0;
}

void send_response(int client, int status_code, const char *content_type, const char *body) {
    size_t len=0;
    if  (body) len=strlen(body);
    dprintf(client, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n\r\n",
        status_code, get_status_text(status_code), content_type, len);
    if (body) {
        (void) http_write(client, body, len);
    }
}

void send_file(int client, const char *content_type, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        dprintf(client, "HTTP/1.1 404 Not Found\r\n\r\n");
        return;
    }
    dprintf(client, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n\r\n", content_type);
    char buf[BUF_SIZE];
    int error =0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        error |= http_write(client, buf, n);
        if (error) break;
    }
    fclose(f);
    if (error) errormsg("There was an error during send_file, write operation.");
}

/** send chunked response first block
 * See: RFC 7230
 */
void send_chunk_head(ClientContext *ctx, int status_code, const char *content_type){
    char header[256];
    int len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n", status_code, get_status_text(status_code), content_type);
    int error = http_write(ctx->socket_fd, header, len);
    if (error) errormsg("There was an error during send_chunk_head, write operation.");
}

void send_chunks(ClientContext *ctx, char* buf, int offset) {
    char header[32];
    int header_len = snprintf(header, sizeof(header), "%x\r\n", offset);
    int error = http_write(ctx->socket_fd, header, header_len);
    error |= http_write(ctx->socket_fd, buf, offset);
    error |= http_write(ctx->socket_fd, "\r\n", 2);
    if (error) errormsg("There was an error during send_chunks, write operation.");
}

void send_chunk_end(ClientContext *ctx){
    int error = write(ctx->socket_fd, "0\r\n\r\n", 5);
    if (error) errormsg("There was an error during send_chunk_end, write operation.");
}
void http_debug_hexdump(const char* prefix, char* buf, int len){
    char hex[256];
    char ascii[256];
    int ofs=0;
    for (int i = 0; i < len; i++) {
        if (i && (i%32 == 0)) {
            logmsg("%s %s %s", prefix, hex, ascii);
            ofs=0;
        }
        sprintf(hex + ofs * 3, "%02X ", (unsigned char)buf[i]);
        char c= (unsigned char)buf[i];
        if ( (c == '\r' || c == '\n' || c == '\t') || ( c < 32)) {
            c='.';
        }
        sprintf(ascii + ofs, "%c ", c);
        ofs++;
    }
    if (ofs){
        logmsg("%s %s %s", prefix, hex, ascii);
    }
}

void http_debug_hexdump_ctx(ClientContext *ctx, int maxlen){
    int len = ctx->request_buffer_len;  // strlen(ctx->request_buffer);
    if (len>maxlen) len=maxlen;
    http_debug_hexdump( "RX", ctx->request_buffer, len);
}

void http_parse_request(ClientContext *ctx, HttpRequest *req) {
    memset(req, 0, sizeof(HttpRequest));
    //logmsg("RXLEN:%d", ctx->request_buffer_len);

    char *saveptr1;
    char *line = strtok_r(ctx->request_buffer, "\r\n", &saveptr1);
    if (!line) return;
    //logmsg("len:%d %s", strlen(line), (unsigned int)(line - ctx->request_buffer));

//    int rr = 
    sscanf(line, "%7s %255s", req->method, req->path);
    //logmsg("rr:%d %s %s", rr, req->method, req->path);

    char *query_start = strchr(req->path, '?');
    if (query_start) {
        *query_start = '\0';
        query_start++;
        char *param;
        char *saveptr2;
        param = strtok_r(query_start, "&", &saveptr2);
        while (param && req->query_count < MAX_QUERY_VARS) {
            char *eq = strchr(param, '=');
            if (eq) {
                *eq = '\0';
                strncpy(req->query[req->query_count].key, param, sizeof(req->query[req->query_count].key) - 1);
                strncpy(req->query[req->query_count].value, eq + 1, sizeof(req->query[req->query_count].value) - 1);
                //logmsg("%s=%s", req->query[req->query_count].key, req->query[req->query_count].value);
                req->query_count++;
            }
            param = strtok_r(NULL, "&", &saveptr2);
        }
    }

    //logmsg(" %s %s", req->method, req->path);
    //http_debug_hexdump("L0", line, 128);

    while ((line = strtok_r(NULL, "\r\n", &saveptr1)) && *line && req->header_count < MAX_HEADER_LINES) {
        //http_debug_hexdump("L1", line, 128);
        char *sep = strchr(line, ':');
        //http_debug_hexdump("L2", line, 128);
        if (sep) {
            *sep = '\0';
            char *key = line;
            char *val = sep + 1;
            while (isspace(*val)) val++;
            strncpy(req->headers[req->header_count].key, key, sizeof(req->headers[req->header_count].key) - 1);
            strncpy(req->headers[req->header_count].value, val, sizeof(req->headers[req->header_count].value) - 1);
            //logmsg("%s=%s", req->headers[req->header_count].key, req->headers[req->header_count].value);
            req->header_count++;
        } else {
            //logmsg("sep=0");
        }
    }
    


    //logmsg(" %s %s", req->method, req->path);
    req->session_id[0] = '\0';
    req->cross_forwarded = 0;
    for (int i = 0; i < req->header_count; i++) {
        // parse all headers
        if (strcasecmp(req->headers[i].key, "X-Forwarded-Server") == 0) {
            strncpy(req->server_name, req->headers[i].value, sizeof(req->server_name) - 1);
            req->cross_forwarded = 1;
        } else if (strcasecmp(req->headers[i].key, "X-Forwarded-For") == 0) {
            strncpy(ctx->client_ip, req->headers[i].value, sizeof(ctx->client_ip) - 1);
            req->cross_forwarded = 1;
        } else if (strcasecmp(req->headers[i].key, "Cookie") == 0) {
            const char* match = "PHPSESSID=";
            char *phpsessionid = strcasestr(req->headers[i].value, match);
            if (phpsessionid) {
                phpsessionid += strlen(match);
                char *end = strchr(phpsessionid, ';');
                size_t len = end ? (size_t)(end - phpsessionid) : strlen(phpsessionid);
                if (len >= sizeof(req->session_id)) len = sizeof(req->session_id) - 1;
                strncpy(req->session_id, phpsessionid, len);
                req->session_id[len] = '\0';
            }
        }else if (strcasecmp(req->headers[i].key, "host") == 0) {
            strncpy(ctx->request.server_host, req->headers[i].value, sizeof(ctx->request.server_host) - 1);
        }

    }
    char buf[MAX_HTTP_VALUE_LEN];
    size_t buflen = sizeof(buf);
    if (req->cross_forwarded) {
        // there where a proxy, so we need the server outer url, not the actual one.
        config_get_string("HTTP", "server_uri_prefix", req->server_uri_prefix, MAX_HTTP_VALUE_LEN, "/geoapi");
        snprintf(buf,buflen, "https://%s%s", req->server_name, req->server_uri_prefix);
        config_get_string("HTTP", "server_url_prefix", req->server_url_prefix, MAX_HTTP_VALUE_LEN, buf);
    }else{
        // there wherer no proxy, so the actual url is the requested.
        config_get_string("HTTP", "internal_uri_prefix", req->server_uri_prefix, MAX_HTTP_VALUE_LEN, "");
        snprintf(buf,buflen, "http://%s%s", req->server_host, req->server_uri_prefix);
        config_get_string("HTTP", "server_url_prefix", req->server_url_prefix, MAX_HTTP_VALUE_LEN, buf);
    }

    //http_debug_hexdump_ctx(ctx, 8192);
   // logmsg(" %s %s", req->method, req->path);
}
void parse_request_path_and_params(const char *request, RequestParams *params) {
    memset(params, 0, sizeof(RequestParams));
    strncpy(params->path, "/", sizeof(params->path));
    params->lat_min = -90.0f;
    params->lat_max = 90.0f;
    params->lon_min = -180.0f;
    params->lon_max = 180.0f;
    params->alt = 0.0f;
    params->step = 0.5f;
    params->radius =10.0f;
    params->width = 1024;
    params->height = 512;
    params->terrain = 1;
    params->id = 0;

    const char *line = strstr(request, "GET ");
    if (!line) return;
    line += 4;
    const char *space = strchr(line, ' ');
    if (!space) return;

    char path_query[1024];
    size_t len = space - line;
    if (len >= sizeof(path_query)) len = sizeof(path_query) - 1;
    strncpy(path_query, line, len);
    path_query[len] = '\0';

    char *query = strchr(path_query, '?');
    if (query) {
        *query++ = '\0';
        char *token = strtok(query, "&");
        while (token) {
            float fval;
            int ival;
            if (sscanf(token, "lat_min=%f", &fval) == 1) params->lat_min = fval;
            else if (sscanf(token, "lat_max=%f", &fval) == 1) params->lat_max = fval;
            else if (sscanf(token, "lon_min=%f", &fval) == 1) params->lon_min = fval;
            else if (sscanf(token, "lon_max=%f", &fval) == 1) params->lon_max = fval;
            else if (sscanf(token, "alt=%f", &fval) == 1) params->alt = fval;
            else if (sscanf(token, "width=%d", &ival) == 1) params->width = ival;
            else if (sscanf(token, "height=%d", &ival) == 1) params->height = ival;
            else if (sscanf(token, "terrain=%d", &ival) == 1) params->terrain = ival;
            else if (sscanf(token, "step=%f", &fval) == 1) params->step = fval;
            else if (sscanf(token, "radius=%f", &fval) == 1) params->radius = fval;
            else if (sscanf(token, "id=%d", &ival) == 1) params->id = ival;
            token = strtok(NULL, "&");
        }
    }
    strncpy(params->path, path_query, sizeof(params->path)-1);
}

typedef struct{
    size_t id;
    char route[MAX_HTTP_KEY_LEN];
    PluginContext *pc;
    PluginHttpRequestHandler handler;
} HttpRouteHandler_t;

HttpRouteHandler_t g_http_route_array[MAX_QUERY_VARS];
size_t g_http_route_numbers=0;
hashmap_t g_http_route_hasmap;
sync_mutex_t *g_http_route_lock;

size_t http_route_count(){
    return g_http_route_numbers;
}

int http_route_get(size_t index, PluginHttpRequestHandler *prh, PluginContext **ppc){
    if (index >= g_http_route_numbers) return -1;
    *prh = g_http_route_array[index].handler;
    *ppc = g_http_route_array[index].pc;
    return 0;
}
int http_route_get_path(size_t index, const char **path){
    if (index >= g_http_route_numbers) return -1;
    *path = g_http_route_array[index].route;
    return 0;
}
void http_route_register(const char *route, PluginHttpRequestHandler handler, PluginContext* pc){
    if (!sync_mutex_lock(g_http_route_lock, HTTP_ROUTE_LOCK_TIMEOUT)){
        size_t index= g_http_route_numbers++;
        HttpRouteHandler_t *rh = &g_http_route_array[index];
        rh->id= index;
        strncpy(rh->route, route, sizeof(rh->route));
        rh->handler = handler;
        rh->pc = pc;
        int r=hashmap_add(&g_http_route_hasmap, rh->route, index);
        if (r){
            errormsg("hashmap returns error");
        }
        sync_mutex_unlock(g_http_route_lock);
    }else{
        errormsg("http_route_register mutex lock error");
    }
}
int http_route_search(const char *route, PluginHttpRequestHandler *prh, PluginContext **ppc){
    size_t index=(size_t)-1;
    int ret = hashmap_search( &g_http_route_hasmap, route, &index);
    if (0 == ret){
        *prh = g_http_route_array[index].handler;
        *ppc = g_http_route_array[index].pc;
    }
    return ret;
}
// temporary solution , to initialize host's internal handlers
void handle_status_html(PluginContext* pc, ClientContext *ctx, RequestParams *params);
void handle_status_json(PluginContext *pc, ClientContext *ctx, RequestParams *params);
void infopage(PluginContext *pc, ClientContext *ctx, RequestParams *params);
void test_image(PluginContext*pc, ClientContext *ctx, RequestParams *params);

void http_init(){
    sync_mutex_init(&g_http_route_lock);
    hashmap_init(&g_http_route_hasmap, 128);
    http_route_register( "/test", test_image, NULL);
    http_route_register( "/status.html", handle_status_html, NULL);
    http_route_register( "/status.json", handle_status_json, NULL);
    http_route_register( "/", infopage, NULL);
}
void http_destroy(){
    hashmap_destroy(&g_http_route_hasmap);
    sync_mutex_destroy(g_http_route_lock);
    g_http_route_lock = NULL;
}