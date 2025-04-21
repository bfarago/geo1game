#ifndef HTTP_H
#define HTTP_H

#include "global.h"
#define MAX_HTTP_KEY_LEN (64)
#define MAX_HTTP_VALUE_LEN (256)

typedef struct {
    float lat_min, lat_max, lon_min, lon_max;
    float step;
    int width, height, id;
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

#endif // HTTP_H
