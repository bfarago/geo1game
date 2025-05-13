/**
 * File:    plugin_mysql.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-05-02
 * 
 * MYSQL Client plugin
 */
#define _GNU_SOURCE
#include "../plugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include "sync.h"

#include <mysql/mysql.h>

#define DB_MYSQL_MAX_QUEUE_SIZE (16)
#define QUEUE_LOCK_TIMEOUT (20ul)  // 20ms
#define QUEUE_WAIT_TIMEOUT (100ul) // 100ms

//static pthread_t g_mysql_thread;
//static int g_mysql_thread_running = 0;
static pthread_key_t mysql_conn_key;
static pthread_once_t mysql_key_once = PTHREAD_ONCE_INIT;
static __thread int db_connection_valid = 0;

// mysql internal
typedef struct QueryRequest {
    DbQuery *query;
    QueryResultProc result_proc;
    void *user_data;
} QueryRequest;

// the main queue thred is a singleton, we save the mutex and cond here
static PluginThreadControl *g_queue_control = NULL;
static QueryRequest queue[DB_MYSQL_MAX_QUEUE_SIZE];
static int queue_size = 0;
static int queue_head = 0;

const PluginHostInterface *g_host;

void handle_mysql_status(PluginContext *pc, ClientContext *ctx, RequestParams *params);
void handle_mysql_query(PluginContext *pc, ClientContext *ctx, RequestParams *params);
void handle_mysql(PluginContext *pc, ClientContext *ctx, RequestParams *params);
void handle_user(PluginContext *pc, ClientContext *ctx, RequestParams *params);

static void (*http_routes[])(PluginContext *, ClientContext *, RequestParams *) = {
    handle_mysql_status, handle_mysql_query, handle_user
};

const char* plugin_http_get_routes[]={"/mysql", "/mysql/query", "/user"};
int plugin_http_get_routes_count = 3;
int g_mysql_con_permanent = 1;

typedef enum{
    det_args,
    det_memory,
    det_init, det_destroy,
    det_lock, det_lock_timeout, det_unlock,
    det_signal, det_broadcast,
    det_wait, det_wait_timeout,
    det_join,
    det_file, det_dlopen, det_pluginapi,
    det_connect, det_character_set,
    det_conn_invalid,
    det_query, det_result,
    det_queue_full,
    det_max
} detid;

static unsigned char g_dets[det_max];
static unsigned short g_detlines[det_max];

static inline void reportDet(detid id, unsigned short line){
    if (g_dets[id] < 255) g_dets[id]++;
    g_detlines[id] = line;
}
int plugin_det_str_dump(char* buf, int len){
    int o=0;
    buf[0]=0;
    for (int i=0; i<det_max; i++){
        if (g_dets[i]){
            o+= snprintf(buf, len - o, "%d:%03d %03d ", i, g_dets[i], g_detlines[i]);
        }
    }
    o+=sync_det_str_dump(buf+o, len-o);
    return o;
}
void plugin_stat_clear(){
    sync_det_clear();
}
static int close_key() {
    pthread_key_delete(mysql_conn_key);
    return 0;
}
static void db_mysql_destructor(void *ptr) {
    if (!ptr) return;
    MYSQL *conn = (MYSQL *)ptr;
    g_host->logmsg("db_mysql_destructor: closing thread-local MySQL connection");
    int res=0;
    if (NULL !=conn){
        mysql_close(conn);
    }
}
static void make_key() {
    pthread_key_create(&mysql_conn_key, (void (*)(void *))db_mysql_destructor); // automatikus close
}
static int db_thread_init() {
    pthread_once(&mysql_key_once, make_key);
    mysql_thread_init();  // must be called before mysql_init

    MYSQL *conn = mysql_init(NULL);
    if (NULL == conn) {
        reportDet(det_init, __LINE__);
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
        reportDet(det_connect, __LINE__);
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
        reportDet(det_connect, __LINE__);
        return -1;
    }

    char db_host[32];
    char db_user[16];
    char db_password[32];
    char db_database[32];
    int db_port;
    if (!g_host){
        reportDet(det_init, __LINE__);
        return -1;
    }
    db_connection_valid = 0;
    g_host->config_get_string("MYSQL", "db_host", db_host, 32, "localhost");
    g_host->config_get_string("MYSQL", "db_user", db_user, 16, "geod");
    g_host->config_get_string("MYSQL", "db_password", db_password, 32, "geo123");
    g_host->config_get_string("MYSQL", "db_database", db_database, 32, "geo");
    db_port = g_host->config_get_int("MYSQL", "db_port", 3306);
    if (mysql_real_connect(*conn, db_host, db_user, db_password, db_database, db_port, NULL, 0) == NULL) {
        reportDet(det_connect, __LINE__);
        g_host->errormsg("mysql_real_connect(%s, %s, , %s, %d) failed: %s", db_host, db_user, db_database, db_port, mysql_error(*conn));
        pthread_setspecific(mysql_conn_key, NULL);
        return -1;
    }
    if (mysql_set_character_set(*conn, "utf8") != 0) {
        reportDet(det_character_set, __LINE__);
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
        reportDet(det_conn_invalid, __LINE__);
        g_host->logmsg("db_query(): attempted to use invalid connection");
        return -1;
    }
    if (mysql_query(conn, query)) {
        reportDet(det_query, __LINE__);
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
    if (!g_queue_control) return;
    if (!sync_mutex_lock(g_queue_control->mutex, QUEUE_LOCK_TIMEOUT)){
        queue_pop_all();
        sync_mutex_unlock(g_queue_control->mutex);
    }else{
        reportDet(det_lock, __LINE__);
        errormsg("queue_clear lock failure");
    }
}
static int queue_is_empty() {
    return queue_size == 0;
}
static void queue_push(QueryRequest *req) {
    if (!g_queue_control || !g_queue_control->keep_running){
        debugmsg("Queue already stoped, but there is more push coming...");
        return;
    }
    if (!sync_mutex_lock(g_queue_control->mutex, QUEUE_LOCK_TIMEOUT)){
        if (queue_size < sizeof(queue) / sizeof(queue[0])) {
            queue[(queue_head + queue_size) % (sizeof(queue) / sizeof(queue[0]))] = *req;
            queue_size++;
            sync_cond_signal(g_queue_control->cond);
        } else {
            reportDet(det_queue_full, __LINE__);
            g_host->logmsg("Queue is full, dropping request");
        }
        sync_mutex_unlock(g_queue_control->mutex);
    }else{
        reportDet(det_lock, __LINE__);
        errormsg("queue_push lock failure");
    }
}
void plugin_mysql_db_request_handler(DbQuery *query, QueryResultProc result_proc, void *user_data) {
    QueryRequest req ;
    req.query = query;
    req.result_proc = result_proc;
    req.user_data = user_data;
    queue_push(&req); //will copy it in the queue
}

static void process_request(QueryRequest *req) {
    MYSQL *conn = db_conn();
    if (conn == NULL) {
        reportDet(det_conn_invalid, __LINE__);
        g_host->logmsg("Failed to get MySQL connection");
        return;
    }

    DbQuery *dbq = req->query;
    if (!dbq) {
        reportDet(det_query, __LINE__);
        g_host->logmsg("Internal query error.");
         return;
    }

    if (db_query(conn, dbq->query) != 0) {
        reportDet(det_query, __LINE__);
        g_host->logmsg("Failed to execute query: %s", dbq->query);
        db_close(conn);
        return;
    }
    MYSQL_RES *result = mysql_store_result(conn);
    if (result == NULL) {
        reportDet(det_result, __LINE__);
        g_host->logmsg("mysql_store_result() failed: %s", mysql_error(conn));
        db_close(conn);
        return;
    }
    int row_index = 0;
    int num_fields = mysql_num_fields(result);
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) && row_index < DB_QUERY_MAX_ROWS) {
        int offset = 0;
        for (int i = 0; i < num_fields; ++i) {
            char *row_out = dbq->rows[row_index];
            int row_out_size = sizeof(dbq->rows[row_index]);
            if (i > 0) offset += snprintf(row_out + offset, row_out_size - offset, "|");
            offset += snprintf(row_out + offset, row_out_size - offset, "%s", row[i] ? row[i] : "");
        }
        row_index++;
    }
    dbq->result_count = row_index;
    req->result_proc(NULL, dbq, req->user_data);
    mysql_free_result(result);
}

void *mysql_thread_main(void *arg) {
    PluginContext *pc = (PluginContext*)arg;
    g_host->thread.enter_own(pc);
    // pthread_setname_np("mysql_queue");
    db_thread_init();
    if (g_queue_control == NULL){
        // actually we need these two globally due to the db api has no pc
        // yeah, the index may depends on implementation, but now 0...
        g_queue_control = &pc->thread.own_threads[0].control;
    }else{
        reportDet(det_args, __LINE__);
        debugmsg("how is it possible?");
    }
    //just for sure, the code is under development...
    if (g_queue_control != &pc->thread.own_threads[0].control){
        reportDet(det_args, __LINE__);
        errormsg("Sanity check failed. Developer issue, check the code !");
        g_queue_control = &pc->thread.own_threads[0].control;
    }

    PluginThreadControl *control= g_queue_control;
    control->keep_running = 1;
    MYSQL *conn = db_conn();
    int tick =0;
    if (db_open(&conn) != 0) {
        reportDet(det_conn_invalid, __LINE__);
        g_host->logmsg("Failed to open MySQL connection");
    } else {
        while (control->keep_running) {
            int retWait= 0;
            int is_empty = 1;
            if (!sync_mutex_lock(control->mutex, QUEUE_LOCK_TIMEOUT)){
                do{
                    is_empty = queue_is_empty();
                    if (ETIMEDOUT == retWait){
                        break;  // lets do something else
                    }
                    if (is_empty){
                        //wait for signal or timeout, this will automaticcally unlock while sleep, and lock when waken
                        retWait = sync_cond_wait(control->cond, control->mutex, QUEUE_WAIT_TIMEOUT);
                    }
                } while (control->keep_running && is_empty);
                if (control->keep_running && !is_empty){
                    //not empty, process the queue
                    QueryRequest req = queue_pop();
                    sync_mutex_unlock(control->mutex);
                    process_request(&req);  // db_query etc, but it is unlocked state.
                }else{
                    sync_mutex_unlock(control->mutex); // unlock anyway.
                }
                if (ETIMEDOUT == retWait){
                    // additionally do some home work...
                    tick++;
                    if (0==tick % 10){
                        // g_host->debugmsg("mysql thread idle");
                    }
                }
            }else{
                g_host->debugmsg("main mysql thread was not able to acquire lock");
                sleep(1); //retry later, or wait until the thread is running actually...
            }
        }
        db_close(conn);
    }
    g_host->debugmsg("main loop left");
    db_thread_end();
    g_host->thread.exit_own(pc);
    g_host->debugmsg("Thread left");
    return NULL;
}

/**
 * Plugin API init
 */
int plugin_init(PluginContext* pc, const PluginHostInterface *host) {
    g_host = host;
    pc->stat.det_str_dump = plugin_det_str_dump;
    pc->stat.stat_clear = plugin_stat_clear;
    pc->db.request = plugin_mysql_db_request_handler;
    pc->http.request_handler = (void*) handle_mysql;
    if (g_host->thread.create_own(pc, mysql_thread_main, "mysql_queue")){
        reportDet(det_init, __LINE__);
        g_host->logmsg("Failed to create MySQL thread");
        return PLUGIN_ERROR;
    }
    return PLUGIN_SUCCESS_OWN_THEAD;
}
void plugin_finish(PluginContext* pc) {
    // pc->thread.control.keep_running = 0; // it is already done
    g_queue_control = NULL; // forget the provider side
    pc->http.request_handler = NULL;
    pc->stat.stat_clear = NULL;
    pc->stat.det_str_dump= NULL;
    pc->stat.det_str_dump=NULL;
}
int plugin_thread_init(PluginContext *ctx) {
    return db_thread_init();  // mandatory!
}

int plugin_thread_finish(PluginContext *ctx) {
    return db_thread_end();   // mandatory!
}

// Helper: Lookup user info by session_id
static int get_user_info(MYSQL *conn, const char *session_id, int *user_id, char *nick, char *last_login, char *email, float *lat, float *lon, float *alt) {
    char query[256];
    snprintf(query, sizeof(query), "SELECT id, nick, last_login, email, lat, lon, alt FROM users WHERE session_id = '%s'", session_id);
    if (db_query(conn, query) != 0) return -1;
    MYSQL_RES *result = mysql_store_result(conn);
    if (!result) return -1;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row) {
        *user_id = atoi(row[0]);
        strncpy(nick, row[1], 59);
        strncpy(last_login, row[2], 59);
        strncpy(email, row[3], 59);
        *lat = row[4] ? atof(row[4]) : 0.0;
        *lon = row[5] ? atof(row[5]) : 0.0;
        *alt = row[6] ? atof(row[6]) : 0.0;
    }
    mysql_free_result(result);
    return 0;
}

// Helper: Count regions for user
static int get_region_count(MYSQL *conn, int user_id) {
    char query[128];
    snprintf(query, sizeof(query), "SELECT COUNT(*) FROM user_regions WHERE user_id = %d", user_id);
    if (db_query(conn, query) != 0) return -1;
    MYSQL_RES *result = mysql_store_result(conn);
    if (!result) return -1;
    MYSQL_ROW row = mysql_fetch_row(result);
    int count = row ? atoi(row[0]) : 0;
    mysql_free_result(result);
    return count;
}

// Helper: Get person stats (workers, soldiers, dead)
static void get_person_stats(MYSQL *conn, int user_id, int *workers, int *soldiers, int *dead) {
    char query[512];
    snprintf(query, sizeof(query),
        "SELECT "
        "SUM(CASE WHEN p.job_id = 2 AND e.status_id != 5 THEN 1 ELSE 0 END),"
        "SUM(CASE WHEN p.job_id = 3 AND e.status_id != 5 THEN 1 ELSE 0 END),"
        "SUM(CASE WHEN e.status_id = 5 THEN 1 ELSE 0 END) "
        "FROM persons p JOIN entities e ON p.entity_id = e.id "
        "WHERE e.user_id = %d", user_id);
    *workers = *soldiers = *dead = 0;
    if (db_query(conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) {
                *workers = row[0] ? atoi(row[0]) : 0;
                *soldiers = row[1] ? atoi(row[1]) : 0;
                *dead = row[2] ? atoi(row[2]) : 0;
            }
            mysql_free_result(res);
        }
    }
}

// Helper: Append region resources as JSON array
static int append_region_resources_json(MYSQL *conn, int user_id, char *out, int out_size) {
    char query[512];
    snprintf(query, sizeof(query),
        "SELECT rr.resource_id, res.name, SUM(rr.quantity) "
        "FROM user_regions ur "
        "JOIN region_resources rr ON rr.region_id = ur.region_id "
        "JOIN resources res ON res.id = rr.resource_id "
        "WHERE ur.user_id = %d "
        "GROUP BY rr.resource_id, res.name", user_id);
    if (db_query(conn, query) != 0) return 0;
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) return 0;
    MYSQL_ROW row;
    int written = snprintf(out, out_size, "\"resources\": [");
    int first = 1;
    while ((row = mysql_fetch_row(res))) {
        if (!first) written += snprintf(out + written, out_size - written, ",");
        written += snprintf(out + written, out_size - written,
            "{\"id\":%s,\"name\":\"%s\",\"quantity\":%s}",
            row[0], row[1], row[2]);
        first = 0;
    }
    written += snprintf(out + written, out_size - written, "]");
    mysql_free_result(res);
    return written;
}

void handle_user(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc; (void)params;
    MYSQL *conn = db_conn();
    if (!conn || db_open(&conn) != 0) {
        g_host->http.send_response(ctx->socket_fd, 500, "text/plain", "MySQL connection error");
        return;
    }

    int user_id = 0;
    char nick[60]="", last_login[60]="", email[60]="";
    float lat, lon, alt;
    if (get_user_info(conn, ctx->request.session_id, &user_id, nick, last_login, email, &lat, &lon, &alt) != 0) {
        g_host->http.send_response(ctx->socket_fd, 403, "text/plain", "Invalid session");
        return;
    }

    int region_count = get_region_count(conn, user_id);
    int workers, soldiers, dead;
    get_person_stats(conn, user_id, &workers, &soldiers, &dead);

    char body[4096];
    int offset = snprintf(body, sizeof(body),
        "{\n"
        "\"session_id\": \"%s\",\n"
        "\"user_id\": %d,\n"
        "\"nick\": \"%s\",\n"
        "\"last_login\": \"%s\",\n"
        "\"email\": \"%s\",\n"
        "\"region_count\": %d,\n"
        "\"workers\": %d,\n"
        "\"soldiers\": %d,\n"
        "\"dead\": %d,\n"
        "\"lat\": %.4f,\n"
        "\"lon\": %.4f,\n"
        "\"alt\": %.4f,\n",
        ctx->request.session_id, user_id, nick, last_login, email,
        region_count, workers, soldiers, dead, lat, lon, alt);

    offset += append_region_resources_json(conn, user_id, body + offset, sizeof(body) - offset);
    offset += snprintf(body + offset, sizeof(body) - offset, "\n}\n");

    g_host->http.send_response(ctx->socket_fd, 200, "application/json", body);
}
void handle_mysql_status(PluginContext *pc, ClientContext *ctx, RequestParams *params) {
    (void)pc; // Unused parameter
    (void)params; // Unused parameter
    (void)ctx; // Unused parameter
    char body[512];
    int offset = 0;
    offset += snprintf(body + offset, sizeof(body) - offset, "{\n");
    offset += snprintf(body + offset, sizeof(body) - offset, "\"queue_size\": %d,\n", queue_size);
    int rr=0;
    if (g_queue_control){
        rr = g_queue_control->keep_running;
    }
    offset += snprintf(body + offset, sizeof(body) - offset, "\"queue_running\": %d\n", rr);
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
    host->server.register_http_route((void*)pc, plugin_http_get_routes_count, plugin_http_get_routes);
    return PLUGIN_SUCCESS;
}

int plugin_event(PluginContext *pc, PluginEventType event, const PluginEventContext* ctx) {
    (void)ctx;
    if (event == PLUGIN_EVENT_STANDBY) {
        if (!queue_is_empty()) {
            return 1;   // If queue is not empty, return 1 to indicate that the plugin should continue processing events
        }
    }
    if ((event == PLUGIN_EVENT_STANDBY) || (event == PLUGIN_EVENT_TERMINATE)){
        if (pc->used_count < 1){
            if (g_queue_control) {
                g_queue_control->keep_running = 0;
            }
        }
    }
    return 0;
}