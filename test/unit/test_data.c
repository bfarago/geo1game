#include "unity.h"
#include "data.h"

void errormsg(const char *fmt, ...) {
    // no-op, just for testing
}

// #define IN_UNITTEST // vagy milyen define van már készen a unity-ben ?
#include "data.c"

void setUp(void) {
    // Optional: fut minden teszt előtt
}

void tearDown(void) {
    // Optional: fut minden teszt után
}


/** test for descriptor_class type function
 * TODO: clarify how the class will work, instance is better implemented.
 */

/**
 * Requirement: The system shall store a descriptor when registered, and reject registration if the maximum is exceeded.
 */
void test_data_register_descriptor_should_store_descriptor(void) {
    g_data_descriptors[0].id = 1;
    data_descriptor_class_t desc = {0};
    desc.id = 33;
    g_data_descriptors_count = 0;
    int index = data_register_descriptor(&desc);
    TEST_ASSERT_GREATER_OR_EQUAL(0, index);
    TEST_ASSERT_EQUAL(33, g_data_descriptors[0].id);

    g_data_descriptors_count = 10;
    index = data_register_descriptor(&desc);
    TEST_ASSERT_EQUAL(-1, index);
}

/**
 * Requirement: The system shall return a valid handle for a valid index, and NULL for out-of-range indices.
 */
void test_data_get_handle_by_index(void){
    g_data_descriptors[0].id=1;
    g_data_handles[0].descriptor= &g_data_descriptors[0];
    g_data_handles_count = 1;
    data_handle_t *d = data_get_handle_by_index(0);
    TEST_ASSERT_NOT_NULL(d);
    if (d){
        TEST_ASSERT_NOT_NULL(d->descriptor);
        if (d->descriptor){
            TEST_ASSERT_EQUAL(1, d->descriptor->id);
        }
    }

    d=data_get_handle_by_index(1);
    TEST_ASSERT_EQUAL(NULL, d);
}

/**
 * Requirement: The system shall retrieve a data handle by matching its registered name.
 */
void test_data_get_handle_by_name(void){
    g_data_handles[0].name = "TEST";
    g_data_handles_count = 1;
    data_handle_t *d = data_get_handle_by_name("TEST");
    TEST_ASSERT_NOT_NULL(d);
    if (d){
        if (d->name) {
            TEST_ASSERT_EQUAL_CHAR_ARRAY("TEST", d->name, 4);
        }
    }

}

/**
 * Requirement: The system shall return NULL when index is below zero or exceeds the count.
 */
void test_data_get_handle_by_index_out_of_bounds(void) {
    g_data_handles_count = 1;
    data_handle_t* d = data_get_handle_by_index(-1);
    TEST_ASSERT_NULL(d);
    d = data_get_handle_by_index(1);
    TEST_ASSERT_NULL(d);
}

/**
 * Requirement: The system shall return NULL if no data handle with the given name exists.
 */
void test_data_get_handle_by_name_not_found(void) {
    g_data_handles_count = 1;
    g_data_handles[0].name = "SOMETHING";
    data_handle_t* d = data_get_handle_by_name("UNKNOWN");
    TEST_ASSERT_NULL(d);
}

/**
 * Requirement: The system shall return the instance pointer of a registered name, or NULL if not found.
 */
void test_data_get_instance_found_and_not_found(void) {
    g_data_handles_count = 1;
    g_data_handles[0].name = "MYDATA";
    g_data_handles[0].instance = (void*)0xdeadbeef;
    void* inst = data_get_instance("MYDATA");
    TEST_ASSERT_EQUAL_PTR((void*)0xdeadbeef, inst);

    inst = data_get_instance("OTHERDATA");
    TEST_ASSERT_NULL(inst);
}

/**
 * Requirement: The system shall register a new data instance and reject registration if the maximum is reached.
 */
void test_data_register_instance_success_and_overflow(void) {
    g_data_handles_count = 0;
    data_descriptor_class_t desc = {0};
    int ret = data_register_instance("A", "param", &desc, NULL, (void*)0x1234);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL_STRING("A", g_data_handles[0].name);
    TEST_ASSERT_EQUAL_PTR(&desc, g_data_handles[0].descriptor);
    TEST_ASSERT_EQUAL_PTR((void*)0x1234, g_data_handles[0].instance);
    TEST_ASSERT_EQUAL_STRING("param", g_data_handles[0].param);

    g_data_handles_count = MAX_DATA_HANDLES;
    ret = data_register_instance("B", "param", &desc, NULL, (void*)0x1234);
    TEST_ASSERT_EQUAL(-1, ret);
}

/**
 * Requirement: The system shall allow calling unregister on a name, even if the implementation is not complete.
 */
void test_data_unregister_instance_placeholder(void) {
    // Only checks that function can be called without crashing
    data_unregister_instance("dummy");
}