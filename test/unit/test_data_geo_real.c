/**
 * File: test_data_geo_real.c
 * 
 * Integration test for the data GEO layer.
 */
#include <stdarg.h>
#include "unity.h"
#include "data.h"
#include "data_geo.h"
#include "plugin.h"
#include "sync.h"
#include "json_indexlist.h"

void errormsg(const char *fmt, ...) {
    // no-op, just for testing
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
void debugmsg(const char *fmt, ...) {
    // no-op, just for testing
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
#include "sync.c"
#include "json_indexlist.c"
#include "data_geo.c"

//TEST_SOURCE_FILE("src/data_geo.c")


#define MAX_TEST_USER (MAX_USERS)
/**
 * Integration test for GEO, load-store
 * Add N records, store it to a temporary file
 * Measure something, close
 */
void test_data_geo_load_store(void){
    //init, and fillup N recor
    data_handle_t dh;
    struct geo_data_t geo_data;
    dh.instance = &geo_data;
    geo_init(&geo_data);
    for (int i=0 ; i< MAX_TEST_USER; i++){
        user_data_t u;
        u.id = i;
        u.lat = (180.0 * random() / (double)RAND_MAX) - 90.0;
        u.lon = (360.0 * random() / (double)RAND_MAX) - 180.0;
        u.alt = (1.8 * random() / (double)RAND_MAX);
        u.version = 100 * random() / RAND_MAX;
        snprintf(u.nick, sizeof(u.nick), "user%d", i);
        geo_add_user( &dh, &u);    
    }
    int numbers = 0;
    int ret= geo_get_max_user(&dh, &numbers);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(MAX_TEST_USER, numbers);

    ret=geo_store(&geo_data, "test.bin");
    TEST_ASSERT_EQUAL(0, ret);

    geo_destroy(&geo_data);
    ret= geo_init(&geo_data);
    TEST_ASSERT_EQUAL(0, ret);
    ret=geo_load(&geo_data, "test.bin");
    TEST_ASSERT_EQUAL(0, ret);
    ret= geo_get_max_user(&dh, &numbers);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(MAX_TEST_USER, numbers);
    geo_destroy(&geo_data);
    unlink("test.bin");
}
