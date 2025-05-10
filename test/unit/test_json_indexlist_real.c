/**
 * Quick test (integration) for the json at the local build environment.
 */
#include "unity.h"
#include "json_indexlist.h"
#include <json-c/json.h>

void errormsg(const char *fmt, ...) {
    // no-op, just for testing
}
void debugmsg(const char *fmt, ...) {
    // no-op, just for testing
}
#include "sync.c"

TEST_SOURCE_FILE("src/json_indexlist.c")
// TEST_SOURCE_FILE("src/sync.c")



void setUp(void) {}
void tearDown(void) {}

/**
 *  Requirement: Integrator shall verify if the used external API works...
 */
void test_json_indexlist_add_and_search_should_work(void) {
    json_indexlist_t map = {0};
    int ret = json_indexlist_init(&map);
    TEST_ASSERT_EQUAL(0, ret);

    ret = json_indexlist_add(&map, "abc", 42);
    TEST_ASSERT_EQUAL(0, ret);

    size_t index = GEO_INDEX_INVALID;
    ret = json_indexlist_search(&map, "abc", &index);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(42, index);

    json_indexlist_destroy(&map);
}

/**
 * Requirement: The API overhad shall below the acceptable limit..
 */
void test_json_indexlist_many_entries_should_be_fast(void) {
    json_indexlist_t map = {0};
    int ret = json_indexlist_init(&map);
    TEST_ASSERT_EQUAL(0, ret);

    const int COUNT = 100000;
    char key[32];
    int i;

    for (i = 0; i < COUNT; ++i) {
        snprintf(key, sizeof(key), "key%d", i);
        ret = json_indexlist_add(&map, key, i);
        TEST_ASSERT_EQUAL(0, ret);
    }

    // Optional: measure time
    clock_t start = clock();
    for (i = 0; i < COUNT; ++i) {
        snprintf(key, sizeof(key), "key%d", i);
        size_t idx = GEO_INDEX_INVALID;
        ret = json_indexlist_search(&map, key, &idx);
        TEST_ASSERT_EQUAL(0, ret);
        TEST_ASSERT_EQUAL(i, idx);
    }
    clock_t end = clock();

    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    printf("Lookup time for %d entries: %.3f sec\n", COUNT, elapsed);

    json_indexlist_destroy(&map);
}