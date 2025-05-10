/**
 * File: test_data_geo.c
 * 
 * Unittest for the data GEO layer.
 */
#include "unity.h"
#include "data.h"
#include "data_geo.h"
#include "plugin.h"
#include "sync.h"
#include "json_indexlist.h"
#include "mock_sync.h"
#include "mock_json_indexlist.h"

void errormsg(const char *fmt, ...) {
    // no-op, just for testing
}

// #define IN_UNITTEST // vagy milyen define van már készen a unity-ben ?
#include "data_geo.c"


void setUp(void) {
    // Optional: fut minden teszt előtt
}

void tearDown(void) {
    // Optional: fut minden teszt után
}


/** test for geo class/instance of the data API
 *
 */


/**
 * Requirement: The system shall return -1 (error) when wrong ptr provided.
 */
void test_geo_init_wrongptr(void) {
    geo_data_t *p=NULL;
    int ret = geo_init(p);
    TEST_ASSERT_EQUAL(-1 , ret);
}

/**
 * Requirement: The system shall initialize the GEO data internal structure.
 */
void test_geo_init_okgptr(void) {
    geo_data_t d;
    geo_data_t *p = &d;
    json_indexlist_init_IgnoreAndReturn(0);
    json_indexlist_init_IgnoreAndReturn(0);
    sync_mutex_init_IgnoreAndReturn(0);
    sync_mutex_destroy_IgnoreAndReturn(0);
    int ret = geo_init(p);
    TEST_ASSERT_EQUAL(0 , ret);

    json_indexlist_destroy_IgnoreAndReturn(0);
    json_indexlist_destroy_IgnoreAndReturn(0);
    sync_mutex_destroy_IgnoreAndReturn(0);
    geo_destroy(p);
    // TEST_ASSERT_EQUAL(0, ret);
}

/**
 * Requirement: The system shall correctly lock and unlock mutex when adding a user by session key.
 */
void test_geo_user_add_session_key_should_add_entry(void) {
    sync_mutex_t *m= NULL;
    geo_data_t d = {
        .session_index = {
            .list = json_object_new_object(),
            .lock = (sync_mutex_t*)m  // dummy ptr for mock
        }
    };
    data_handle_t dh = { .instance = &d };

    json_indexlist_add_IgnoreAndReturn(0);
    int ret = geo_user_add_session_key(&dh, "abc123", 1);
    TEST_ASSERT_EQUAL(0, ret);

    json_object_put(d.session_index.list);
}

/**
 * Requirement: The system shall correctly lock and unlock mutex when adding a user by user ID.
 */
void test_geo_user_add_user_id_key_should_add_entry(void) {
    sync_mutex_t *m;
    geo_data_t d = {
        .userid_index = {
            .list = json_object_new_object(),
            .lock = (sync_mutex_t*)m
        }
    };
    data_handle_t dh = { .instance = &d };

    json_indexlist_add_IgnoreAndReturn(0);

    int ret = geo_user_add_user_id_key(&dh, 42, 0);
    TEST_ASSERT_EQUAL(0, ret);

    json_object_put(d.userid_index.list);
}

/**
 * Requirement: The system shall delete a session key only if it exists.
 */
void test_geo_user_delete_session_key_should_delete_existing(void) {
    sync_mutex_t *m;
    geo_data_t d = {
        .session_index = {
            .list = json_object_new_object(),
            .lock = (sync_mutex_t*)m
        }
    };
    data_handle_t dh = { .instance = &d };
    json_object_object_add(d.session_index.list, "abc", json_object_new_int(1));

    json_indexlist_delete_IgnoreAndReturn(0);
    int ret = geo_user_delete_session_key(&dh, "abc");
    TEST_ASSERT_EQUAL(0, ret);

    json_object_put(d.session_index.list);
}

/**
 * Requirement: The system shall return -1 if mutex lock fails on session delete.
 */
void test_geo_user_delete_session_key_should_fail_on_lock_timeout(void) {
    sync_mutex_t *m;
    geo_data_t d = {
        .session_index = {
            .list = json_object_new_object(),
            .lock = (sync_mutex_t*)m
        }
    };
    data_handle_t dh = { .instance = &d };

    json_indexlist_delete_IgnoreAndReturn(-1);
    int ret = geo_user_delete_session_key(&dh, "abc");
    TEST_ASSERT_EQUAL(-1, ret);

    json_object_put(d.session_index.list);
}

/**
 * Requirement: The system shall return the number of users stored in memory.
 */
void test_geo_get_max_user_should_return_users_count(void) {
    sync_mutex_t *m;
    geo_data_t d = {
        .users_count = 5,
        .lock_users = (sync_mutex_t*)m
    };
    data_handle_t dh = { .instance = &d };

    json_indexlist_search_IgnoreAndReturn(0);
    sync_mutex_lock_IgnoreAndReturn(0);
    sync_mutex_unlock_IgnoreAndReturn(0);
    
    int count = 0;
    int ret = geo_get_max_user(&dh, &count);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(5, count);
}

/**
 * Requirement: The system shall return a user pointer if the index is valid.
 */
void test_geo_get_user_should_return_user_pointer(void) {
    sync_mutex_t *m;
    geo_data_t d = {
        .users_count = 2,
        .lock_users = (sync_mutex_t*)m
    };
    user_data_t u = { .id = 42 };
    d.users[1] = u;
    data_handle_t dh = { .instance = &d };

    sync_mutex_lock_ExpectAndReturn(d.lock_users, DATA_GEO_LOCK_TIMEOUT, 0);
    sync_mutex_unlock_ExpectAndReturn(d.lock_users, 0);

    user_data_t* ptr = NULL;
    int ret = geo_get_user(&dh, 1, &ptr);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL(42, ptr->id);
}

/**
 * Requirement: The system shall delete a user ID key only if it exists.
 */
void test_geo_user_delete_user_id_key_should_delete_existing(void) {
    sync_mutex_t *m;
    geo_data_t d = {
        .userid_index = {
            .list = json_object_new_object(),
            .lock = (sync_mutex_t*)m
        }
    };
    data_handle_t dh = { .instance = &d };
    json_object_object_add(d.userid_index.list, "42", json_object_new_int(1));

    json_indexlist_delete_IgnoreAndReturn(0);
    
    int ret = geo_user_delete_user_id_key(&dh, 42);
    TEST_ASSERT_EQUAL(0, ret);

    json_object_put(d.userid_index.list);
}

/**
 * Requirement: The system shall add a new user and increase users_count.
 */
void test_geo_add_user_should_increase_user_count(void) {
    geo_data_t d = {
        .users_count = 0,
        .lock_users = (sync_mutex_t*)0x1234  // dummy non-null for mock
    };
    data_handle_t dh = { .instance = &d };
    user_data_t u = {
        .id = 10,
        .index = -1,
        .lat = 0.0,
        .lon = 0.0,
        .alt = 0.0,
        .version = 1
    };
    strcpy(u.session_key, "abc123");

    sync_mutex_lock_ExpectAndReturn(d.lock_users, DATA_GEO_LOCK_TIMEOUT, 0);
    sync_mutex_unlock_ExpectAndReturn(d.lock_users, 0);

    json_indexlist_add_IgnoreAndReturn(0);
    json_indexlist_add_IgnoreAndReturn(0);

    int index = geo_add_user(&dh, &u);
    TEST_ASSERT_EQUAL(0, index);
    TEST_ASSERT_EQUAL(1, d.users_count);
    TEST_ASSERT_EQUAL_STRING("abc123", d.users[0].session_key);
    TEST_ASSERT_EQUAL(10, d.users[0].id);
}

/**
 * Requirement: geo_set_user shall add a new user if none exists yet.
 */
void test_geo_set_user_should_add_new_user(void) {
    geo_data_t d = {
        .users_count = 0,
        .lock_users = (sync_mutex_t*)0x1234
    };
    data_handle_t dh = { .instance = &d };
    user_data_t u = {
        .id = 1,
        .index = -1
    };
    strcpy(u.session_key, "s1");

    sync_mutex_lock_ExpectAndReturn(d.lock_users, DATA_GEO_LOCK_TIMEOUT, 0);
    
    json_indexlist_search_IgnoreAndReturn(-1);
    json_indexlist_search_IgnoreAndReturn(-1);
    json_indexlist_add_IgnoreAndReturn(0);
    json_indexlist_add_IgnoreAndReturn(0);

    sync_mutex_unlock_ExpectAndReturn(d.lock_users, 0);

    int r = geo_set_user(&dh, &u);
    TEST_ASSERT_EQUAL(0, r);
    TEST_ASSERT_EQUAL(1, d.users_count);
}

/**
 * Requirement: geo_set_user shall update existing user if session and id match.
 */
void test_geo_set_user_should_update_existing_user_with_matching_keys(void) {
    geo_data_t d = {
        .users_count = 1,
        .lock_users = (sync_mutex_t*)0x1234
    };
    strcpy(d.users[0].session_key, "s1");
    d.users[0].id = 1;

    data_handle_t dh = { .instance = &d };

    user_data_t u = {
        .id = 1,
        .index = -1
    };
    strcpy(u.session_key, "s1");

    sync_mutex_lock_ExpectAndReturn(d.lock_users, DATA_GEO_LOCK_TIMEOUT, 0);
    json_indexlist_search_IgnoreAndReturn(0);
    json_indexlist_search_IgnoreAndReturn(0);
    json_indexlist_add_IgnoreAndReturn(0);
    sync_mutex_unlock_ExpectAndReturn(d.lock_users, 0);
    int r = geo_set_user(&dh, &u);
    TEST_ASSERT_EQUAL(0, r);
    TEST_ASSERT_EQUAL(1, d.users[0].id);
}

/**
 * Requirement: geo_set_user shall logout old user and replace it on conflict.
 */
void test_geo_set_user_should_replace_user_on_id_conflict(void) {
    geo_data_t d = {
        .users_count = 1,
        .lock_users = (sync_mutex_t*)0x1234
    };
    strcpy(d.users[0].session_key, "s1");
    d.users[0].id = 10;

    data_handle_t dh = { .instance = &d };

    user_data_t u = {
        .id = 42,
        .index = -1
    };
    strcpy(u.session_key, "s1");

    sync_mutex_lock_ExpectAndReturn(d.lock_users, DATA_GEO_LOCK_TIMEOUT, 0);
    size_t session_index = GEO_INDEX_INVALID; // default in the implementation due to error ?
    size_t expected_session_index = 0; // fake result
    size_t id_index = GEO_INDEX_INVALID;
    size_t expected_id_index = GEO_INDEX_INVALID;
    json_indexlist_search_ExpectAndReturn(&d.session_index, "s1", &session_index, 0); // session exists
    json_indexlist_search_ReturnThruPtr_index(&expected_session_index);
    
    json_indexlist_search_ExpectAndReturn(&d.userid_index, "42", &id_index, -1); // id does NOT exist
    json_indexlist_search_ReturnThruPtr_index(&expected_id_index);
    
    //new index is 1 (so the second record) Is this correct ???
    json_indexlist_add_ExpectAndReturn(&d.session_index, "s1", 1, 0);
    json_indexlist_add_ExpectAndReturn(&d.userid_index, "42", 1, 0);
    sync_mutex_unlock_ExpectAndReturn(d.lock_users, 0);
    int r = geo_set_user(&dh, &u);
    TEST_ASSERT_EQUAL(0, r);
    //TEST_ASSERT_EQUAL(42, d.users[0].id); // json mocked, so nobody do it here.
}

/**
 * Requirement: geo_set_user shall update user by index if already known.
 */
void test_geo_set_user_should_update_existing_user_by_index(void) {
    geo_data_t d = {
        .users_count = 1,
        .lock_users = (sync_mutex_t*)0x1234
    };
    d.users[0].id = 10;

    data_handle_t dh = { .instance = &d };

    user_data_t u = {
        .id = 42,
        .index = 0
    };
    strcpy(u.session_key, "s1");

    sync_mutex_lock_ExpectAndReturn(d.lock_users, DATA_GEO_LOCK_TIMEOUT, 0);
    json_indexlist_search_IgnoreAndReturn(0);
    json_indexlist_search_IgnoreAndReturn(0);
    json_indexlist_add_IgnoreAndReturn(0);
    json_indexlist_add_IgnoreAndReturn(0);
    sync_mutex_unlock_ExpectAndReturn(d.lock_users, 0);

    int r = geo_set_user(&dh, &u);
    TEST_ASSERT_EQUAL(0, r);
    TEST_ASSERT_EQUAL(42, d.users[0].id);
}

