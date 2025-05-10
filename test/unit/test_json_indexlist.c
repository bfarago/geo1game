#include "unity.h"
#include "sync.h"

#include "mock_sync.h"

void errormsg(const char *fmt, ...) {
    // no-op, just for testing
}

// #define IN_UNITTEST // vagy milyen define van már készen a unity-ben ?
#include "json_indexlist.c"


void setUp(void) {
    // Optional: fut minden teszt előtt
}

void tearDown(void) {
    // Optional: fut minden teszt után
}

/**
 * Requirement: handle the  NULL argument normally, return with error.
 */
void test_json_indexlist_init_null(void){
    int ret = 88;
    ret = json_indexlist_init(NULL);
    TEST_ASSERT_NOT_EQUAL(0, ret);
}

/**
 * Requirement: create and destroy works normally, if list pointer provided.
 */
void test_json_indexlist_init_destroy(void){
    int ret = 99;
    json_indexlist_t list;
    sync_mutex_init_IgnoreAndReturn(0);
    ret= json_indexlist_init(&list);
    TEST_ASSERT_EQUAL(0, ret);

    sync_mutex_destroy_IgnoreAndReturn(0);
    ret= json_indexlist_destroy(&list);
    TEST_ASSERT_EQUAL(0, ret);
}

/**
 * Add, Search, Del basic behaviour is working, with success return code.
 */
void test_json_indexlist_add_search_del(void){
    int ret = 99;
    json_indexlist_t list;
    sync_mutex_init_IgnoreAndReturn(0);
    ret= json_indexlist_init(&list);
    TEST_ASSERT_EQUAL(0, ret);

    sync_mutex_lock_IgnoreAndReturn(0);
    sync_mutex_unlock_IgnoreAndReturn(0);
    ret= json_indexlist_add(&list, "apple", 0);
    TEST_ASSERT_EQUAL(0, ret);
    
    size_t index = 88;
    sync_mutex_lock_IgnoreAndReturn(0);
    sync_mutex_unlock_IgnoreAndReturn(0);
    ret= json_indexlist_search(&list, "apple", &index );
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(0, index);
    
    //printf("Before delete: %s\n", json_object_to_json_string(list.list));
    sync_mutex_lock_IgnoreAndReturn(0);
    sync_mutex_unlock_IgnoreAndReturn(0);
    ret= json_indexlist_delete(&list, "apple");
    TEST_ASSERT_EQUAL(0, ret);

    sync_mutex_destroy_IgnoreAndReturn(0);
    ret= json_indexlist_destroy(&list);
    TEST_ASSERT_EQUAL(0, ret);
}

/**
 * Requirement: handle lock timeout, return with error.
 */
void test_json_indexlist_add_search_del_timeout(void){
    int ret = 99;
    json_indexlist_t list;
    sync_mutex_init_IgnoreAndReturn(0);
    ret= json_indexlist_init(&list);
    TEST_ASSERT_EQUAL(0, ret);

    sync_mutex_lock_IgnoreAndReturn(-1);
    ret= json_indexlist_add(&list, "apple", 0);
    TEST_ASSERT_NOT_EQUAL(0, ret);
    
    int index = 88;
    sync_mutex_lock_IgnoreAndReturn(-1);
    ret= json_indexlist_search(&list, "apple", &index );
    TEST_ASSERT_NOT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(88, index); // Doesn not change

    sync_mutex_lock_IgnoreAndReturn(-1);
    ret= json_indexlist_delete(&list, "apple");
    TEST_ASSERT_NOT_EQUAL(0, ret);

    sync_mutex_destroy_IgnoreAndReturn(0);
    ret= json_indexlist_destroy(&list);
    TEST_ASSERT_EQUAL(0, ret);
}

/**
 * Requirement: in case of not found, return with error.
 */
void test_json_indexlist_search_notfound(void){
    int ret = 99;
    json_indexlist_t list;
    sync_mutex_init_IgnoreAndReturn(0);
    ret= json_indexlist_init(&list);
    TEST_ASSERT_EQUAL(0, ret);
    
    size_t index = 88;
    sync_mutex_lock_IgnoreAndReturn(-1);
    ret= json_indexlist_search(&list, "apple", &index );
    TEST_ASSERT_NOT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(88, index); // Doesn not change

    sync_mutex_destroy_IgnoreAndReturn(0);
    ret= json_indexlist_destroy(&list);
    TEST_ASSERT_EQUAL(0, ret);
}