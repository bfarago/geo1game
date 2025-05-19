#define PLUGINHST_STATIC_LINKED
#include "unity.h"
#include "data.h"
#include "data_sql.h"
#include "plugin.h"
#include "sync.h"

#include "mock_plugin.h"
#include "mock_sync.h"

void errormsg(const char *fmt, ...) {
    // no-op, just for testing
}

#include "data_sql.c"


void setUp(void) {
    // Optional: fut minden teszt előtt
}

void tearDown(void) {
    // Optional: fut minden teszt után
}


/** test for sql class/instance of the data API
 *
 */


 

/**
 * Requirement: The system shall initialize the SQL data instance and register it using the data registration API.
 */
void test_data_sql_init_registers_instance(void) {
    sync_mutex_init_IgnoreAndReturn(0);
    sync_cond_init_IgnoreAndReturn(0);
    data_sql_init();

    sql_data_t* instance = data_get_instance(g_data_descriptor_sql.name);
    TEST_ASSERT_NOT_NULL(instance);
    TEST_ASSERT_EQUAL(1, instance->initialized);

    sync_mutex_destroy_IgnoreAndReturn(0);
    sync_cond_destroy_IgnoreAndReturn(0);
    data_sql_destroy();
}

/**
 * Requirement: The system shall unregister and free the SQL data instance during destruction.
 */
void test_data_sql_destroy_unregisters_instance(void) {
    sync_mutex_init_IgnoreAndReturn(0);
    sync_cond_init_IgnoreAndReturn(0);
    data_sql_init();
    sync_mutex_destroy_IgnoreAndReturn(0);
    sync_cond_destroy_IgnoreAndReturn(0);
    data_sql_destroy();
    sql_data_t* instance = data_get_instance(g_data_descriptor_sql.name);
    TEST_ASSERT_NULL(instance);
}

/**
 * Requirement: The system shall return 0 from data_sql_load even if no instance is found.
 */
void test_data_sql_load_returns_zero(void) {
    int result = data_sql_load();
    TEST_ASSERT_EQUAL(0, result);
}

/**
 * Requirement: The system shall return 0 from data_sql_store even if no instance is found.
 */
void test_data_sql_store_returns_zero(void) {
    int result = data_sql_store();
    TEST_ASSERT_EQUAL(0, result);
}

/**
 * Requirement: The system shall return -1 from data_sql_execute if plugin context cannot be retrieved.
 */
void test_data_sql_execute_returns_error_if_no_plugin_context(void) {
    DbQuery dummy_query = {0};
    get_plugin_context_ExpectAndReturn("mysql", NULL);  // fix: set expectation
    int result = data_sql_execute(NULL, &dummy_query);
    TEST_ASSERT_EQUAL(-1, result);
}

// how many times the function was called.
int g_testfn_called =0;
// simulate the callback for test
void stubRequest(DbQuery *query, QueryResultProc result_proc, void *user_data){
    g_testfn_called++;
    query->result_count = 3; // test data.
    result_proc(NULL, query, user_data);
}
/**
 * Requirement: The system shall return the query result row number >0 or 0 if the query was empty.
 */
void test_data_sql_execute_success(void) {
    DbQuery dummy_query = {0};
    PluginContext pc;
    pc.db.request = stubRequest;
    pc.id = 1; // some valid id
    data_handle_t dh;
    sql_internal_db_request_t req;
    dh.instance = (void*)&req;
    g_testfn_called = 0;

    get_plugin_context_ExpectAndReturn("mysql", &pc);  // fix: set expectation
    plugin_start_ExpectAndReturn(1, 0); // id, int return ok.
    sync_mutex_lock_IgnoreAndReturn(0);
    sync_cond_signal_IgnoreAndReturn(0);
    sync_mutex_unlock_IgnoreAndReturn(0);
    plugin_stop_Expect(1); // id (void return)
    int result = data_sql_execute(&dh, &dummy_query);
    TEST_ASSERT_EQUAL(1, g_testfn_called);
    TEST_ASSERT_EQUAL(3, result); // test datafom the dummy query
}
