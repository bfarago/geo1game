/*
 * File:    data.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * Data abstraction layer
 * Key features:
 *  init-destroy
 *  load-store : from a local file when service restarts
 *  register class/instance
 */
#ifndef DATA_H
#define DATA_H

#define MAX_DATA_HANDLES (10)

#ifdef __cplusplus
extern "C" {
#endif

/*
struct data_t;

typedef void (*data_init_fn)(data_t* data, const char* name);
typedef void (*data_destroy_fn)(data_t* data);
typedef void (*data_load_fn)(data_t* data);
typedef void (*data_store_fn)(data_t* data);
*/
typedef int (*data_init_fn)();
typedef void (*data_destroy_fn)();
typedef int (*data_load_fn)();
typedef int (*data_store_fn)();
/** data general api descriptor structure.
 * Similar to file handle descriptor, but for data subsystem
 */
typedef struct data_descriptor_class_t {
    int id;
    const char *name;
    data_init_fn init;
    data_destroy_fn destroy;
    data_load_fn load;
    data_store_fn store;
} data_descriptor_class_t;

/** data handle structure.
 * Handles to data descriptors, which are used to access data.
 */
typedef struct {
    const char* name;
    const data_descriptor_class_t *descriptor;  // global api descriptor
    const void *specific_api;             // specific api instance
    void* instance;         // instance specific data
    const char *param;      // instance specific parameter
} data_handle_t;

#if EXPORT_DATA_INTERNALS // you dont need this.
extern int g_data_handles_count;
extern data_handle_t g_data_handles[MAX_DATA_HANDLES];
#endif // EXPORT_DATA_INTERNALS

data_handle_t* data_get_handle_by_name(const char* name);
void* data_get_instance(const char* name);
int data_register_instance(
    const char* name, const char *param,
    const data_descriptor_class_t* descriptor, const void* specific_api,
    void* instance);
void data_unregister_instance(const char* name);
#ifdef __cplusplus
}
#endif

#endif //DATA_H