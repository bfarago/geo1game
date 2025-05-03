#define _GNU_SOURCE
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

// #include <json-c/json.h>

#include "data.h"
#include "data_sql.h"
#include "plugin.h"

//this one is preliminary due to later we could move this to a plugin...
extern PluginHostInterface g_plugin_host;
PluginHostInterface *g_host= &g_plugin_host;

// Updated sql_data_t definition
typedef struct {
    DbQuery *query;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int ready;
} sql_internal_db_request_t;

void data_sql_init();
void data_sql_destroy();
void data_sql_load();
void data_sql_store();

const data_descriptor_class_t g_data_descriptor_sql = {
    .id = 0,    // run-time type identifier
    .name = "sql",
    .init = data_sql_init,
    .destroy = data_sql_destroy,
    .load = data_sql_load,
    .store = data_sql_store,
};
void data_sql_queue_callback(PluginContext* dbplugin, DbQuery *req, void *user_data){
    sql_internal_db_request_t *res = (sql_internal_db_request_t *)user_data;
    pthread_mutex_lock(&res->lock);
    res->ready = 1;
    pthread_cond_signal(&res->cond);
    pthread_mutex_unlock(&res->lock);
};
int data_sql_execute(data_handle_t *handle, DbQuery *db_query) {
    (void)handle; // not used until internal pool.. later.
    PluginContext *pcmysql = get_plugin_context("mysql");
    if (!pcmysql) {
        errormsg("Failed to get plugin context for mysql");
        return -1;
    }
    if (plugin_start(pcmysql->id)){
        errormsg("Failed to start plugin context for mysql");
        return -1;
    }
    // mysql plugin is surely started, so we can use it
    // prepare the internal request container
    sql_internal_db_request_t req_internal = {0};
    sql_internal_db_request_t *req = &req_internal;
    req->ready = 0;
    req->query = db_query;
    // Prepare the result container
    db_query->result_count = 0;
    
    pthread_mutex_init(&req->lock, NULL);
    pthread_cond_init(&req->cond, NULL);
    // Lock and enqueue
    pthread_mutex_lock(&req->lock);
    // g_host->db.  // todo , when db router at the host side is ready, we can use it, but plugin context is needed.
    pcmysql->db.request(db_query, data_sql_queue_callback, (void*)req); // queue_push(&req);
    while (!req->ready) {
        pthread_cond_wait(&req->cond, &req->lock);
    }
    pthread_mutex_unlock(&req->lock);
    plugin_stop(pcmysql->id);
    return db_query->result_count;
}
void data_sql_init() {
    sql_data_t* sql_data = malloc(sizeof(sql_data_t));
    if (!sql_data) {
        fprintf(stderr, "Failed to allocate sql_data\n");
        return;
    }

    g_data_descriptor_sql.init(sql_data, g_data_descriptor_sql.name);

    int result = data_register_instance(
        g_data_descriptor_sql.name,  // instance name
        NULL,                        // optional parameter
        &g_data_descriptor_sql,      // descriptor
        &g_data_api_sql,             // api descriptor
        sql_data                     // the instance
    );

    if (result < 0) {
        fprintf(stderr, "Failed to register sql_data instance\n");
        free(sql_data);
    }
}
void data_sql_destroy() {
    sql_data_t* sql_data = data_get_instance(g_data_descriptor_sql.name);
    if (sql_data) {
        data_unregister_instance(g_data_descriptor_sql.name);
        free(sql_data);
    }
}
void data_sql_load() {
    sql_data_t* sql_data = data_get_instance(g_data_descriptor_sql.name);
    if (sql_data) {
        sql_load(sql_data);
    }
}
void data_sql_store() {
    sql_data_t* sql_data = data_get_instance(g_data_descriptor_sql.name);
    if (sql_data) {
        sql_store(sql_data);
    }
}
/** specific api descriptor */
const data_api_sql_t g_data_api_sql = {
    .execute = data_sql_execute,
};
