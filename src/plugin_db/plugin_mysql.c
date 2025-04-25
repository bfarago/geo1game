/**
 * MYSQL Client plugin
 */
#define _GNU_SOURCE
#include "../plugin.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <mysql/mysql.h>


#define DB_MYSQL_MAX_QUEUE_SIZE 16

static pthread_t g_mysql_thread;
static int g_mysql_thread_running = 0;
static pthread_key_t mysql_conn_key;
static pthread_once_t mysql_key_once = PTHREAD_ONCE_INIT;
static __thread int db_connection_valid = 0;
typedef void (*QueryResultProc)(MYSQL *conn, const char *query, MYSQL_RES *result);

typedef struct QueryRequest {
    char query[256];
    QueryResultProc result_proc;
} QueryRequest;

static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
static QueryRequest queue[DB_MYSQL_MAX_QUEUE_SIZE];
static int queue_size = 0;
static int queue_head = 0;

const PluginHostInterface *g_host;

void handle_mysql_status(PluginContext *pc, ClientContext *ctx, RequestParams *params);
void handle_mysql_query(PluginContext *pc, ClientContext *ctx, RequestParams *params);
void handle_mysql(PluginContext *pc, ClientContext *ctx, RequestParams *params);

static void (*http_routes[])(PluginContext *, ClientContext *, RequestParams *) = {
    handle_mysql_status, handle_mysql_query
};

const char* plugin_http_get_routes[]={"/mysql", "/mysql/query"};
int plugin_http_get_routes_count = 2;
int g_mysql_con_permanent = 1;

static int close_key() {
    pthread_key_delete(mysql_conn_key);
    return 0;
}
static void db_mysql_destructor(void *ptr) {
    if (!ptr) return;
    MYSQL *conn = (MYSQL *)ptr;
    g_host->logmsg("db_mysql_destructor: closing thread-local MySQL connection");
    int res=0;
    if (NULL !=conn)
        mysql_close(conn);
}
static void make_key() {
    pthread_key_create(&mysql_conn_key, (void (*)(void *))db_mysql_destructor); // automatikus close
}
static int db_thread_init() {
    pthread_once(&mysql_key_once, make_key);
    mysql_thread_init();  // must be called before mysql_init

    MYSQL *conn = mysql_init(NULL);
    if (NULL == conn) {
        g_host->logmsg("mysql_init() failed");
        return -1;
    }
    g_host->debugmsg("mysql_init() successful on thread");
    pthread_setspecific(mysql_conn_key, conn);
    return 0;
}
static MYSQL *db_conn() {
    MYSQL *conn = (MYSQL *)pthread_getspecific(mysql_conn_key);
    if (conn == NULL) {
        g_host->errormsg("mysql_conn() failed");
        return NULL;
    }
    return conn;
}
static int db_thread_end() {
    MYSQL *conn = db_conn();
    if (conn) {
        g_host->debugmsg("db_thread_end(): closing MySQL connection %p", conn);
        mysql_close(conn);
        pthread_setspecific(mysql_conn_key, NULL);
        db_connection_valid = 0;
    } else {
        g_host->logmsg("db_thread_end(): no connection to close");
    }
    mysql_thread_end();
    return 0;
}

int db_open(MYSQL **conn) {
    if (NULL == conn) {
        g_host->errormsg("db_open() failed: NULL connection pointer");
        return -1;
    }
    if (NULL == *conn) {
        *conn = db_conn();
    }

    if (NULL == *conn) {
        return -1;
    }

    char db_host[32];
    char db_user[16];
    char db_password[32];
    char db_database[32];
    int db_port;
    if (!g_host){
        return -1;
    }
    db_connection_valid = 0;
    g_host->config_get_string("MYSQL", "db_host", db_host, 32, "localhost");
    g_host->config_get_string("MYSQL", "db_user", db_user, 16, "geod");
    g_host->config_get_string("MYSQL", "db_password", db_password, 32, "geo123");
    g_host->config_get_string("MYSQL", "db_database", db_database, 32, "geo");
    db_port = g_host->config_get_int("MYSQL", "db_port", 3306);
    if (mysql_real_connect(*conn, db_host, db_user, db_password, db_database, db_port, NULL, 0) == NULL) {
        g_host->errormsg("mysql_real_connect(%s, %s, , %s, %d) failed: %s", db_host, db_user, db_database, db_port, mysql_error(*conn));
        pthread_setspecific(mysql_conn_key, NULL);
        return -1;
    }
    if (mysql_set_character_set(*conn, "utf8") != 0) {
        g_host->errormsg("mysql_set_character_set() failed: %s", mysql_error(*conn));
        return -1;
    }
    db_connection_valid = 1;
    g_host->logmsg("mysql_real_connect(%s, %s, , %s, %d) ok", db_host, db_user, db_database, db_port);
    return 0;
}

/**Blocking query */
int db_query(MYSQL *conn, const char *query) {
    if (!db_connection_valid) {
        db_open(&conn);
    }
    if (!db_connection_valid) {
        g_host->logmsg("db_query(): attempted to use invalid connection");
        return -1;
    }
    if (mysql_query(conn, query)) {
        g_host->errormsg("mysql_query() failed: %s", mysql_error(conn));
        return -1;
    }
    return 0;
}
int db_close(MYSQL *conn) {
    if (g_mysql_con_permanent) return 0;
    if (conn) {
        db_connection_valid = 0;
        mysql_close(conn);
        pthread_setspecific(mysql_conn_key, NULL);
        return 0;
    }
    return -1;
}

static QueryRequest queue_pop() {
    if (queue_size > 0) {
        QueryRequest req = queue[queue_head];
        queue_head = (queue_head + 1) % (sizeof(queue) / sizeof(queue[0]));
        queue_size--;
        return req;
    }
    QueryRequest empty_req = {0};
    return empty_req;
}
static void queue_pop_all() {
    while (queue_size > 0) {
        queue_pop();
    }
}
static void queue_clear() {
    pthread_mutex_lock(&queue_mutex);
    queue_pop_all();
    pthread_mutex_unlock(&queue_mutex);
}
static int queue_is_empty() {
    return queue_size == 0;
}
static void queue_push(QueryRequest *req) {
    pthread_mutex_lock(&queue_mutex);
    if (queue_size < sizeof(queue) / sizeof(queue[0])) {
        queue[(queue_head + queue_size) % (sizeof(queue) / sizeof(queue[0]))] = *req;
        queue_size++;
        pthread_cond_signal(&queue_cond);
    } else {
        g_host->logmsg("Queue is full, dropping request");
    }
    pthread_mutex_unlock(&queue_mutex);
}

static void process_request(QueryRequest *req) {
    MYSQL *conn = db_conn();
    if (conn == NULL) {
        g_host->logmsg("Failed to get MySQL connection");
        return;
    }

    if (db_query(conn, req->query) != 0) {
        g_host->logmsg("Failed to execute query: %s", req->query);
        db_close(conn);
        return;
    }
    MYSQL_RES *result = mysql_store_result(conn);
    if (result == NULL) {
        g_host->logmsg("mysql_store_result() failed: %s", mysql_error(conn));
        db_close(conn);
        return;
    }
    req->result_proc(conn, req->query, result);
    mysql_free_result(result);
}
void *mysql_thread_main(void *arg) {
    db_thread_init();
    g_mysql_thread_running = 1;
    MYSQL *conn = db_conn();
    if (db_open(&conn) != 0) {
        g_host->logmsg("Failed to open MySQL connection");
    } else {
        while (g_mysql_thread_running) {
            pthread_mutex_lock(&queue_mutex);
            while (queue_is_empty() && g_mysql_thread_running) {
                pthread_cond_wait(&queue_cond, &queue_mutex);
            }
            if (g_mysql_thread_running){
                QueryRequest req = queue_pop();
                pthread_mutex_unlock(&queue_mutex);
                process_request(&req);  // db_query stb.
            }else{
                pthread_mutex_unlock(&queue_mutex);
            }
        }
        db_close(conn);
    }
    db_thread_end();
    return NULL;
}
int plugin_init(PluginContext* pc, const PluginHostInterface *host) {
    g_host = host;
    pc->http.request_handler = (void*) handle_mysql;
    if (pthread_create(&g_mysql_thread, NULL, mysql_thread_main, NULL) != 0) {
        g_host->logmsg("Failed to create MySQL thread");
        return PLUGIN_ERROR;
    }
    return PLUGIN_SUCCESS;
}
void plugin_finish(PluginContext* pc) {
    g_mysql_thread_running = 0;

    pthread_mutex_lock(&queue_mutex);
    pthread_cond_broadcast(&queue_cond);  // wake up the main thread
    pthread_mutex_unlock(&queue_mutex);

    pthread_join(g_mysql_thread, NULL);
    pc->http.request_handler = NULL;
}
int plugin_thread_init(PluginContext *ctx) {
    return db_thread_init();  // mandatory!
}

int plugin_thread_finish(PluginContext *ctx) {
    return db_thread_end();   // mandatory!
}

void handle_mysql_status(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc; // Unused parameter
    (void)params; // Unused parameter
    (void)ctx; // Unused parameter
    char body[512];
    int offset = 0;
    offset += snprintf(body + offset, sizeof(body) - offset, "{\n");
    offset += snprintf(body + offset, sizeof(body) - offset, "\"queue_size\": %d,\n", queue_size);
    offset += snprintf(body + offset, sizeof(body) - offset, "\"queue_running\": %d\n", g_mysql_thread_running);
    offset += snprintf(body + offset, sizeof(body) - offset, "}\n");
    g_host->http.send_response(ctx->socket_fd, 200, "application/json", body);
    g_host->logmsg("%s mysql status request", ctx->client_ip);
}
void handle_mysql_query(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc; // Unused parameter
    (void)params; // Unused parameter
    (void)ctx; // Unused parameter
    const char *query = "SELECT * FROM auser";  // Example query
    MYSQL *conn = db_conn();
    if (conn == NULL) {
        g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Failed to get MySQL connection");
        return;
    }
    if (db_open(&conn) != 0) {
        g_host->logmsg("Failed to open MySQL connection");
        g_host->http.send_response(ctx->socket_fd, 200, "text/plain", "Failed to open MySQL connection on client thread.");
    } else {
        if (db_query(conn, query) != 0) {
            g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Failed to execute query");
            db_close(conn);
            return;
        }
        MYSQL_RES *result = mysql_store_result(conn);
        if (result == NULL) {
            g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "Failed to store result");
            db_close(conn);
            return;
        }
        int num_fields = mysql_num_fields(result);
        MYSQL_FIELD *fields = mysql_fetch_fields(result);
        MYSQL_ROW row;
        char body[4096];
        int offset = 0;
        int size=sizeof(body);
        offset += snprintf(body + offset, size - offset, "{\"res\":[\n");
        int row_count = 0;
        int i;
        while ((row = mysql_fetch_row(result)) != NULL) {
            // Process each row
            if (row_count++ > 0) {
                offset += snprintf(body + offset, size - offset, ",\n");
            }
            offset += snprintf(body + offset, size - offset, "  {");
            for (i = 0; i < num_fields; i++) {
                if (i > 0) {
                    offset+= snprintf(body + offset, size - offset, ", ");
                }
                offset+= snprintf(body + offset, size - offset, "\"%s\":\"%s\"", fields[i].name, row[i] ? row[i] : "");
            }
            offset+= snprintf(body + offset, size - offset, "}\n");
        }
        offset+= snprintf(body + offset, size - offset, "]}\n");
        mysql_free_result(result);
        g_host->http.send_response(ctx->socket_fd, 200, "application/json", body);
    }
}
void handle_mysql(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
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
    host->http.register_http_route((void*)pc, plugin_http_get_routes_count, plugin_http_get_routes);
    return PLUGIN_SUCCESS;
}
