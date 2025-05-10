

#include <json-c/json.h>
#include "global.h"
#include "sync.h"
#include "json_indexlist.h"

#define JSON_INDEXLIST_LOCK_TIMEOUT (1000)

int json_indexlist_init(json_indexlist_t *list){
    int ret;
    if (list){
        list->list = json_object_new_object();
        sync_mutex_init(&list->lock);
        ret = 0;
    }else{
        ret = -1;
    }
    return ret;
}

int json_indexlist_destroy(json_indexlist_t *list){
    int ret;
    if (list){
        json_object_put(list->list);
        sync_mutex_destroy(list->lock);
        ret = 0;
    }else{
        ret = -1;
    }
    return ret;
}

/** safe_size_to_json_int
 * Converts index uint value to json int value, with boundarz check.
 */
static inline int safe_size_to_json_int(size_t index) {
    return (index == GEO_INDEX_INVALID) ? -1 :
           (index < GEO_INDEX_INVALID) && (index <= INT32_MAX) ? (int)index : -2;
}
static inline size_t safe_json_int_to_size(int value){
    return  (value >= 0)? (size_t)value : GEO_INDEX_INVALID;
}
int json_indexlist_add(json_indexlist_t *list, const char* key, size_t index){
    if (!list) return -2;
    if (!key) return -2;
    if (sync_mutex_lock(list->lock, JSON_INDEXLIST_LOCK_TIMEOUT)){
        errormsg("Lock timeout");
        return -3;
    }
    int value = safe_size_to_json_int(index);
    json_object_object_add(list->list, key, json_object_new_int(value));
    sync_mutex_unlock(list->lock);
    return 0;
}

int json_indexlist_delete(json_indexlist_t *list, const char * key){
    int ret = -1;
    if (!list || !key) return -2;

    if (sync_mutex_lock(list->lock, JSON_INDEXLIST_LOCK_TIMEOUT)) {
        errormsg("Lock timeout");
        return -3;
    }

    struct json_object *existing_value = NULL;
    if (json_object_object_get_ex(list->list, key, &existing_value)) {
        json_object_object_del(list->list, key);
        ret = 0;
    }

    sync_mutex_unlock(list->lock);
    return ret;
}

int  json_indexlist_search(json_indexlist_t *list, const char * key, size_t *index){
    struct json_object *idx_obj = NULL;
    int ret = -1;
    if (!list || !key) return -2;
    if (sync_mutex_lock(list->lock, JSON_INDEXLIST_LOCK_TIMEOUT)) return -3;
    if (json_object_object_get_ex(list->list, key, &idx_obj)) {
        int idx = json_object_get_int(idx_obj);
        *index = safe_json_int_to_size(idx);
        ret = 0;
    }
    sync_mutex_unlock(list->lock);
    return ret;
}