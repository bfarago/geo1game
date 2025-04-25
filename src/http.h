#ifndef HTTP_H
#define HTTP_H

#include "global.h"
#define MAX_HTTP_KEY_LEN (64)
#define MAX_HTTP_VALUE_LEN (256)

typedef struct {
    float lat_min, lat_max, lon_min, lon_max;
    float alt;
    float step,radius;
    int width, height, id, terrain;
    char path[MAX_HTTP_KEY_LEN];
} RequestParams;

typedef struct {
    char key[MAX_HTTP_KEY_LEN];
    char value[MAX_HTTP_VALUE_LEN];
} QueryParam;

typedef struct {
    char key[MAX_HTTP_KEY_LEN];
    char value[MAX_HTTP_VALUE_LEN];
} HeaderField;

typedef struct {
    char method[8];
    char path[MAX_HTTP_VALUE_LEN];
    QueryParam query[MAX_QUERY_VARS];
    int query_count;
    HeaderField headers[MAX_HEADER_LINES];
    int header_count;
} HttpRequest;

typedef struct {
    int socket_fd;
    char client_ip[16];
    HttpRequest request;
    char request_buffer[BUF_SIZE];
} ClientContext;

typedef void (*RequestHandler)(ClientContext *ctx, RequestParams *params);
typedef struct {
    char path[MAX_HTTP_KEY_LEN];
    RequestHandler handler;
} HttpRouteRule;

extern void send_response(int client, int status_code, const char *content_type, const char *body);
void send_file(int client, const char *content_type, const char *path);
void send_chunk_head(ClientContext *ctx, int status_code, const char *content_type);
void send_chunks(ClientContext *ctx, char* buf, int offset);
void send_chunk_end(ClientContext *ctx);

// Internal
void http_parse_request(ClientContext *ctx, HttpRequest *req);
void parse_request_path_and_params(const char *request, RequestParams *params);

#endif // HTTP_H
