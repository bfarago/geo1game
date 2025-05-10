/*
 * File:    json_indexlist.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * Part of the Data layer, we can use json implementation for
 * creating and maintaining a hash searchable list.
 * Key features:
 *  lock, search, add, delete, init, destroy
 */
#ifndef JSON_INDEXLIST_H_
#define JSON_INDEXLIST_H_

#include "sync.h"
#include <json-c/json.h>

typedef struct {
    struct json_object *list;
    sync_mutex_t *lock;
} json_indexlist_t;

int json_indexlist_init(json_indexlist_t *list);
int json_indexlist_destroy(json_indexlist_t *list);
int json_indexlist_add(json_indexlist_t *liar, const char* key, size_t index);
int json_indexlist_delete(json_indexlist_t *list, const char * key);
int json_indexlist_search(json_indexlist_t *list, const char * key, size_t *index);

#endif // JSON_INDEXLIST_H_