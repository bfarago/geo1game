/*
 * File:    http.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * Hyper Text Transfer Protocol layer
 * Key features:
 * Collects the header and querery keys. 
 */
#ifndef HTTP_H
#define HTTP_H
#define _GNU_SOURCE
#include <time.h>
#include "global.h"

#define MAX_HTTP_KEY_LEN (128)
#define MAX_HTTP_VALUE_LEN (512)

/* used by statistics after thread termination, to decide which counter
// needs to be incremented...*/
typedef enum{
    CTX_RUNNING,
    CTX_FINISHED_OK,
    CTX_ERROR
} CtxResultStatus;

/* App specific request params, soo basically these are the
// temporary storage of the app vs pugin variables now.
// Basically these can came from session, database, query, etc.*/
typedef struct {
    float lat_min, lat_max, lon_min, lon_max;
    float alt;
    float step,radius;
    int width, height, id, terrain;
    char path[MAX_HTTP_KEY_LEN];
} RequestParams;

// WS requestss get these params.
typedef struct {
    char session_id[MAX_HTTP_KEY_LEN]; // or key or something..
} WsRequestParams;

// Control request gets these
typedef struct ControlRequestParam
{
    char cmd[MAX_HTTP_KEY_LEN];
    int argc;
    char **argv;
} ControlRequestParam;

// More detailed HTTP request structures

// HTTP protocol consists of headers and body, these are the header key-value pairs.
typedef struct {
    char key[MAX_HTTP_KEY_LEN];
    char value[MAX_HTTP_VALUE_LEN];
} QueryParam;

// These are the query key-value pairs.
typedef struct {
    char key[MAX_HTTP_KEY_LEN];
    char value[MAX_HTTP_VALUE_LEN];
} HeaderField;

/* The http request could use these params, and data-structures to resolve
// the app requested task and generate the output html/json/etc... */
typedef struct {
    char method[8];
    char server_url_prefix[MAX_HTTP_VALUE_LEN];
    char server_host[MAX_HTTP_VALUE_LEN];
    char server_name[MAX_HTTP_VALUE_LEN];
    char server_uri_prefix[MAX_HTTP_VALUE_LEN];
    char path[MAX_HTTP_VALUE_LEN];
    QueryParam query[MAX_QUERY_VARS];
    int query_count;
    HeaderField headers[MAX_HEADER_LINES];
    int header_count;
    char session_id[MAX_HTTP_VALUE_LEN];
    char cross_forwarded;
} HttpRequest;

/**
 * Client context
 * HTTP server accept a new client connection, and process the header. When the necessary information
 * has been sucessfully collected, then request path field processed, by matching with predefinied
 * path routing handlers. In case of one of the plugin's route is matching, then http_handler function
 * will be called, with this Context. 
 */
typedef struct ClientContext {
    // Common fields
    int socket_fd;
    char client_ip[16];

    // Statistics related fields
    CtxResultStatus result_status;
    struct timespec start_time;
    struct timespec end_time;
    double elapsed_time;

    // http protocol specific
    HttpRequest request;
    int request_buffer_len;
    char request_buffer[BUF_SIZE];
} ClientContext;

/** HTTP protocol related request's handler
 */
typedef void (*RequestHandler)(ClientContext *ctx, RequestParams *params);
typedef struct {
    char path[MAX_HTTP_KEY_LEN];
    RequestHandler handler;
} HttpRouteRule;
/**************************************************************************/
/** Web Socket Request handler */
typedef void (*WsRequestHandler)(ClientContext *ctx, WsRequestParams *params);
typedef struct {
    char path[MAX_HTTP_KEY_LEN];
    RequestHandler handler;
} WsRouteRule;
/**************************************************************************/
/** Control socket Request handler */
typedef void (*ControlRequestHandler)(ClientContext *ctx, ControlRequestParam *params);
typedef struct {
    char path[MAX_HTTP_KEY_LEN];
    RequestHandler handler;
} ControlRouteRule;

#ifdef HTTP_STATIC_LINKED
// This part probably will be moved to a separated file later. historical reason...
extern void send_response(int client, int status_code, const char *content_type, const char *body);
void send_file(int client, const char *content_type, const char *path);
void send_chunk_head(ClientContext *ctx, int status_code, const char *content_type);
void send_chunks(ClientContext *ctx, char* buf, int offset);
void send_chunk_end(ClientContext *ctx);

// Internal, also not relevant here actually...
void http_parse_request(ClientContext *ctx, HttpRequest *req);
void parse_request_path_and_params(const char *request, RequestParams *params);

void http_init();
void http_destroy();
size_t http_route_count();
int http_route_get_path(size_t index, const char **path);

#else
// #define send_chunk_head g_host->http.send_chunk_head
// #define send_chunks g_host->http.send_chunks
// #define send_chunk_end g_host->http.send_chunk_end
#endif

#endif // HTTP_H
