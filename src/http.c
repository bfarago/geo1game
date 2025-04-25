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

#include "global.h"
#include "http.h"
#include "plugin.h"

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
void send_response(int client, int status_code, const char *content_type, const char *body) {
    size_t len=0;
    if  (body) len=strlen(body);
    dprintf(client, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n\r\n",
        status_code, get_status_text(status_code), content_type, len);
    if (body) write(client, body, strlen(body));
}

void send_file(int client, const char *content_type, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        dprintf(client, "HTTP/1.1 404 Not Found\r\n\r\n");
        return;
    }
    dprintf(client, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n\r\n", content_type);
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) write(client, buf, n);
    fclose(f);
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
    write(ctx->socket_fd, header, len);
}
void send_chunks(ClientContext *ctx, char* buf, int offset) {
    char header[32];
    int header_len = snprintf(header, sizeof(header), "%x\r\n", offset);
    write(ctx->socket_fd, header, header_len);
    write(ctx->socket_fd, buf, offset);
    write(ctx->socket_fd, "\r\n", 2);
}
void send_chunk_end(ClientContext *ctx){
    write(ctx->socket_fd, "0\r\n\r\n", 5);
}

void http_parse_request(ClientContext *ctx, HttpRequest *req) {
    memset(req, 0, sizeof(HttpRequest));

    char *line = strtok(ctx->request_buffer, "\r\n");
    if (!line) return;

    sscanf(line, "%7s %255s", req->method, req->path);

    char *query_start = strchr(req->path, '?');
    if (query_start) {
        *query_start = '\0';
        query_start++;
        char *param = strtok(query_start, "&");
        while (param && req->query_count < MAX_QUERY_VARS) {
            char *eq = strchr(param, '=');
            if (eq) {
                *eq = '\0';
                strncpy(req->query[req->query_count].key, param, sizeof(req->query[req->query_count].key) - 1);
                strncpy(req->query[req->query_count].value, eq + 1, sizeof(req->query[req->query_count].value) - 1);
                req->query_count++;
            }
            param = strtok(NULL, "&");
        }
    }

    while ((line = strtok(NULL, "\r\n")) && *line && req->header_count < MAX_HEADER_LINES) {
        char *sep = strchr(line, ':');
        if (sep) {
            *sep = '\0';
            char *key = line;
            char *val = sep + 1;
            while (isspace(*val)) val++;
            strncpy(req->headers[req->header_count].key, key, sizeof(req->headers[req->header_count].key) - 1);
            strncpy(req->headers[req->header_count].value, val, sizeof(req->headers[req->header_count].value) - 1);
            req->header_count++;
        }
    }
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
