/*
 * File:    data_sql.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * Data layer sql specific part
 * Key features:
 *  execute query
 */
#define _GNU_SOURCE
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "data.h"
#include "data_sql.h"
#include "plugin.h"
// #include "pluginhst.h"
#include "sync.h"

volatile int g_data_sql_abort = 0;

#ifndef DATA_SQL_LONG_RUN_LOOP
#define DATA_SQL_LONG_RUN_LOOP()  {static unsigned int count=0; if (count++>0xFFFFFFFFU) break;} // documentation of the possible endless loop.
#endif // DATA_SQL_LONG_RUN_LOOP

#define DATA_SQL_LOCK_TIMEOUT (1000)

//this one is preliminary due to later we could move this to a plugin...
// extern PluginHostInterface g_plugin_host;
extern PluginHostInterface *g_host; //= &g_plugin_host;

// Updated sql_data_t definition, which is an instance? or user data
typedef struct {
    DbQuery *query;
    sync_mutex_t *lock;
    sync_cond_t *cond;
    int ready;
} sql_internal_db_request_t;

// actually singleton, but lets define the class ype:
typedef struct sql_data_t{
    sql_internal_db_request_t request; // maybe multiple requests in the future ?
    int initialized;
}sql_data_t;

int data_sql_init();
void data_sql_destroy();
int data_sql_load();
int data_sql_store();

const data_descriptor_class_t g_data_descriptor_sql = {
    .id = 0,    // run-time type identifier
    .name = "sql",
    .init = data_sql_init,
    .destroy = data_sql_destroy,
    .load = data_sql_load,
    .store = data_sql_store,
};

void data_sql_queue_callback(PluginContext* dbplugin, DbQuery *req, void *user_data){
    (void)dbplugin;
    (void)req;
    sql_internal_db_request_t *res = (sql_internal_db_request_t *)user_data;
    sync_mutex_lock(res->lock, DATA_SQL_LOCK_TIMEOUT);
    res->ready = 1;
    sync_cond_signal(res->cond);
    sync_mutex_unlock(res->lock);
}

sql_internal_db_request_t* data_sql_prepare_instance(sql_data_t *inst, DbQuery *db_query){
    if (!inst){
        return NULL;
    }
    if (!db_query){
        return NULL;
    }
    inst->initialized = 1;
    sql_internal_db_request_t* req = &inst->request;
    req->ready = 0;
    db_query->result_count =0;
    req->query = db_query;
    return req;
}

int data_sql_execute(data_handle_t *handle, DbQuery *db_query) {
    PluginContext *pcmysql = get_plugin_context("mysql");
    if (!pcmysql) {
        errormsg("Failed to get plugin context for mysql");
        return -1;
    }
    if (!handle || !handle->instance) {
        errormsg("Invalid data handle");
        return -1;
    }
    if (plugin_start(pcmysql->id)){
        errormsg("Failed to start plugin context for mysql");
        return -1;
    }
    // not fully used until internal pool.. later.
    sql_data_t* sql_data = (sql_data_t*)handle->instance;
    sql_internal_db_request_t* req = data_sql_prepare_instance(sql_data, db_query);
    if (!req) {
        errormsg("Instance request pull full?");
        return -1;
    }     
    // Lock and enqueue
    if (sync_mutex_lock(req->lock, DATA_SQL_LOCK_TIMEOUT)) {
        return -2;
    }
    // g_host->db.  // todo , when db router at the host side is ready, we can use it, but plugin context is needed.
    pcmysql->db.request(db_query, data_sql_queue_callback, (void*)req); // queue_push(&req);
    while (!req->ready) {
        if (g_data_sql_abort) {
            errormsg("SQL wait aborted globally.");
            break;
        }
        if (sync_cond_wait(req->cond, req->lock, DATA_SQL_LOCK_TIMEOUT)) {
            break; // todo: return to caller with an error ? or let them know due to result_count is zero anyway...
        }
        DATA_SQL_LONG_RUN_LOOP();
    }
    sync_mutex_unlock(req->lock);

    plugin_stop(pcmysql->id);
    return db_query->result_count;
}
/** specific api descriptor */
const data_api_sql_t g_data_api_sql = {
    .execute = data_sql_execute
};

int data_sql_init() {
    g_data_sql_abort = 0;
    sql_data_t* sql_data = malloc(sizeof(sql_data_t));
    if (!sql_data) {
        fprintf(stderr, "Failed to allocate sql_data\n");
        return -1;
    }
    sql_data->initialized = 1;
    // g_data_descriptor_sql.init(sql_data, g_data_descriptor_sql.name);

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
        return -1;
    }else{
        sync_mutex_init(&sql_data->request.lock);
        sync_cond_init(&sql_data->request.cond);
    }
    return 0;
}
void data_sql_destroy(void) {
    g_data_sql_abort = 1;
    sql_data_t* sql_data = data_get_instance(g_data_descriptor_sql.name);
    if (sql_data) {
        sync_mutex_destroy(sql_data->request.lock);
        sync_cond_destroy(sql_data->request.cond);
        data_unregister_instance(g_data_descriptor_sql.name);
        free(sql_data);
    }
}
int data_sql_load() {
    sql_data_t* sql_data = data_get_instance(g_data_descriptor_sql.name);
    if (sql_data) {
        //sql_load_instance(sql_data);
    }
    return 0;
}
int data_sql_store() {
    sql_data_t* sql_data = data_get_instance(g_data_descriptor_sql.name);
    if (sql_data) {
        //sql_store_instance(sql_data);
    }
    return 0;
}
