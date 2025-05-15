/**
 * SQLite Client plugin
 */
#include "../plugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sqlite3.h>
#define MAX_QUERY_LEN (1024)
static sqlite3 *g_sqlite_db = NULL;
static pthread_mutex_t sqlite_mutex = PTHREAD_MUTEX_INITIALIZER;
const PluginHostInterface *g_host;
void handle_sqlite_status(PluginContext *pc, ClientContext *ctx, RequestParams *params);
void handle_sqlite_query(PluginContext *pc, ClientContext *ctx, RequestParams *params);
void handle_sqlite_query_chunked(PluginContext *pc, ClientContext *ctx, RequestParams *params);
void handle_sqlite(PluginContext *pc, ClientContext *ctx, RequestParams *params);

typedef struct {
    char query[MAX_QUERY_LEN];
    CacheFile cache_file;
} sqlite_result_data_t;

static void (*http_routes[])(PluginContext *, ClientContext *, RequestParams *) = {
    handle_sqlite_status, handle_sqlite_query //, handle_sqlite_query_chunked
};
const char* plugin_http_get_routes[]={"/sqlite", "/sqlite/query"};
int plugin_http_get_routes_count = 2;

static int sqlite_open() {
    const char *filename = "geo.db"; // Default db name
    char db_file[256];
    g_host->config_get_string("SQLITE", "db_file", db_file, sizeof(db_file), filename);
    if (sqlite3_open(db_file, &g_sqlite_db) != SQLITE_OK) {
        g_host->logmsg("sqlite_open() failed: %s", sqlite3_errmsg(g_sqlite_db));
        return -1;
    }
    g_host->logmsg("sqlite_open(): opened %s", db_file);
    return 0;
}

static void sqlite_close() {
    if (g_sqlite_db) {
        sqlite3_close(g_sqlite_db);
        g_sqlite_db = NULL;
    }
}

static int sqlite_exec_query(const char *query) {
    char *errmsg = NULL;
    pthread_mutex_lock(&sqlite_mutex);
    int rc = sqlite3_exec(g_sqlite_db, query, NULL, NULL, &errmsg);
    pthread_mutex_unlock(&sqlite_mutex);
    if (rc != SQLITE_OK) {
        g_host->logmsg("sqlite_exec_query() failed: %s", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

int plugin_init(PluginContext* pc, const PluginHostInterface *host) {
    g_host = host;
    pc->http.request_handler= handle_sqlite;
    if (sqlite_open() != 0) {
        return PLUGIN_ERROR;
    }
    return PLUGIN_SUCCESS;
}

void plugin_finish(PluginContext* pc) {
    (void)pc;
    sqlite_close();
}

int plugin_thread_init(PluginContext *ctx) {
    (void)ctx;
    return 0;
}

int plugin_thread_finish(PluginContext *ctx) {
    (void)ctx;
    return 0;
}


void handle_sqlite_status(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc; // Unused parameter
    (void)params; // Unused parameter
    (void)ctx; // Unused parameter
    char body[512];
    int offset = 0;
    offset += snprintf(body + offset, sizeof(body) - offset, "{\n");
    //offset += snprintf(body + offset, sizeof(body) - offset, "\"queue_size\": %d,\n", queue_size);
    //offset += snprintf(body + offset, sizeof(body) - offset, "\"queue_running\": %d\n", g_mysql_thread_running);
    offset += snprintf(body + offset, sizeof(body) - offset, "}\n");
    g_host->http.send_response(ctx->socket_fd, 200, "application/json", body);
    g_host->logmsg("%s sqlite status request", ctx->client_ip);
}
/*
void handle_sqlite_query_chunked(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc; // Unused parameter
    (void)params; // Unused parameter
    (void)ctx; // Unused parameter
    const char *query = "SELECT lat,lon,elevation,population,name FROM regions LIMIT 10000";
    
    if (NULL == g_sqlite_db) {
        g_host->logmsg("Failed to open SQLite connection");
        g_host->http.send_response(ctx->socket_fd, 200, "text/plain", "Failed to open SQLite connection on client thread.");
    } else {
        sqlite3_stmt *stmt;
        int rc;
        rc= sqlite3_prepare_v2(g_sqlite_db, query, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            g_host->logmsg("sqlite3_prepare_v2() failed: %s", sqlite3_errmsg(g_sqlite_db));
            pthread_mutex_unlock(&sqlite_mutex);
            g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Query preparation failed");
            return;
        }
        int offset = 0;
        int row_count = 0;
        int num_cols = sqlite3_column_count(stmt);

        size_t body_size = 1024 *8; // saccolt méret kb. 100 rekordhoz
        char *body = malloc(body_size);
        if (!body) {
            g_host->logmsg("Failed to allocate memory for response body");
            sqlite3_finalize(stmt);
            pthread_mutex_unlock(&sqlite_mutex);
            g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Memory allocation failed");
            return;
        }
        send_chunk_head(ctx, 200, "application/json");
        offset += snprintf(body + offset, body_size - offset, "{\"res\":[\n");

        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            
            if (row_count++ > 0) {
                offset += snprintf(body + offset, body_size - offset, ",\n");
            }
            offset += snprintf(body + offset, body_size - offset, "  {");
            for (int i = 0; i < num_cols; i++) {
                if (i > 0) {
                    offset += snprintf(body + offset, body_size - offset, ", ");
                }
                const char *col_name = sqlite3_column_name(stmt, i);
                const char *col_text = (const char *)sqlite3_column_text(stmt, i);
                offset += snprintf(body + offset, body_size - offset,
                                   "\"%s\":\"%s\"", col_name, col_text ? col_text : "");
            }
            offset += snprintf(body + offset, body_size - offset, "}");
            if ((double)offset >= body_size * 0.9) {
                send_chunks(ctx, body, offset);
                offset=0;
            }
        }

        offset += snprintf(body + offset, body_size - offset, "]}\n");
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&sqlite_mutex);
        send_chunks(ctx, body, offset);
        send_chunk_end(ctx);
        g_host->logmsg("Sqlite request sent: %d", offset);
        free(body);
    }
}
*/
unsigned long sdbm_hash(const char *str) {
    unsigned long hash = 0;
    int c;
    while ((c = *str++))
        hash = c + (hash << 6) + (hash << 16) - hash;
    return hash;
}

int get_db_cache_filename(char*buf, int buflen, const char* db, const char* table, const char *query ){
    return snprintf(buf, buflen, "%s_%s_%08lx.json", db, table, sdbm_hash(query));
}

void handle_sqlite_query(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc; // Unused parameter
    (void)params; // Unused parameter
    (void)ctx; // Unused parameter
    sqlite_result_data_t res_data;
    sqlite_result_data_t *rp = &res_data;

    const char *myquery = "SELECT lat,lon,elevation,population,name FROM regions LIMIT 10000";
    strcpy(rp->query, myquery);

    char cache_filename[255];
    const char* tablename="regions";
    get_db_cache_filename(cache_filename, sizeof(cache_filename), "sqlite", tablename, rp->query);
    g_host->cache.file_init(&rp->cache_file, cache_filename);
    
    if (!g_host->cache.file_exists_recent(&rp->cache_file)){
        if (g_host->cache.file_create(&rp->cache_file) < 1){
            g_host->logmsg("Failed to open cache file for writing");
        }else{
            if (NULL == g_sqlite_db) {
                g_host->logmsg("Failed to open SQLite connection");
                g_host->http.send_response(ctx->socket_fd, 200, "text/plain", "Failed to open SQLite connection on client thread.");
                g_host->cache.file_close(&rp->cache_file);
                return;
            } else {
                sqlite3_stmt *stmt;
                int rc;
                rc= sqlite3_prepare_v2(g_sqlite_db, rp->query, -1, &stmt, NULL);
                if (rc != SQLITE_OK) {
                    g_host->logmsg("sqlite3_prepare_v2() failed: %s", sqlite3_errmsg(g_sqlite_db));
                    pthread_mutex_unlock(&sqlite_mutex);
                    g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Query preparation failed");
                    g_host->cache.file_close(&rp->cache_file);
                    return;
                }
                int offset = 0;
                int row_count = 0;
                int num_cols = sqlite3_column_count(stmt);
                size_t body_size = 1024 *8; // saccolt méret kb. 100 rekordhoz
                char *body = malloc(body_size);
                if (!body) {
                    g_host->logmsg("Failed to allocate memory for response body");
                    sqlite3_finalize(stmt);
                    pthread_mutex_unlock(&sqlite_mutex);
                    g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Memory allocation failed");
                    g_host->cache.file_close(&rp->cache_file);
                    return;
                }
                offset += snprintf(body + offset, body_size - offset, "{\"res\":[\n");
                while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                    if (row_count++ > 0) {
                        offset += snprintf(body + offset, body_size - offset, ",\n");
                    }
                    offset += snprintf(body + offset, body_size - offset, "  {");
                    for (int i = 0; i < num_cols; i++) {
                        if (i > 0) {
                            offset += snprintf(body + offset, body_size - offset, ", ");
                        }
                        const char *col_name = sqlite3_column_name(stmt, i);
                        const char *col_text = (const char *)sqlite3_column_text(stmt, i);
                        offset += snprintf(body + offset, body_size - offset,
                                           "\"%s\":\"%s\"", col_name, col_text ? col_text : "");
                    }
                    offset += snprintf(body + offset, body_size - offset, "}");
                    if ((double)offset >= body_size * 0.9) {
                        g_host->cache.file_write(&rp->cache_file, body, offset);
                        offset=0;
                    }
                }
                offset += snprintf(body + offset, body_size - offset, "]}\n");
                sqlite3_finalize(stmt);
                pthread_mutex_unlock(&sqlite_mutex);
                g_host->cache.file_write(&rp->cache_file, body, offset);
                g_host->cache.file_close(&rp->cache_file);

                free(body);
            }
        }
    }
    g_host->http.send_file(ctx->socket_fd, "application/json", rp->cache_file.path);
}
void handle_sqlite(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc; // Unused parameter
    (void)params; // Unused parameter
    (void)ctx; // Unused parameter
    // Handle specific paths
    int i;
    for (i = 0; i < plugin_http_get_routes_count; i++) {
        if (strcmp(plugin_http_get_routes[i], params->path) == 0) {
            http_routes[i](pc, ctx, params);
            return;
        }
    }
    const char *body = "{\"msg\":\"Unknown sub path\"}";
    g_host->http.send_response(ctx->socket_fd, 200, "application/json", body);
    g_host->logmsg("%s mysql request", ctx->client_ip);
}

int plugin_register(PluginContext *pc, const PluginHostInterface *host) {
    g_host = host;
    host->server.register_http_route((void*)pc, plugin_http_get_routes_count, plugin_http_get_routes);
    host->register_db_queue(pc, "sqlite");
    return PLUGIN_SUCCESS;
}