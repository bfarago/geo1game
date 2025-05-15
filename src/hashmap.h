

/*
 * File:    hashmap.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-05-14
 *
 * Generic hash map implementation using string keys.
 * Supports: init, destroy, add, delete, search
 */

#ifndef HASHMAP_H_
#define HASHMAP_H_

#include <stddef.h>
#include "sync.h"

/**
 * @brief Represents a key-value pair in the hashmap.
 */
typedef struct hashmap_entry {
    const char *key;
    size_t value;
    struct hashmap_entry *next;
} hashmap_entry_t;

/**
 * @brief Hashmap structure containing buckets and metadata.
 */
typedef struct {
    hashmap_entry_t **buckets;
    size_t capacity;
    size_t size;
    sync_mutex_t *lock;
} hashmap_t;


/**
 * @brief Initializes a hashmap.
 * @param map Pointer to hashmap structure to initialize.
 * @param capacity Number of buckets to allocate.
 * @return 0 on success, -1 on failure.
 */
int hashmap_init(hashmap_t *map, size_t capacity);

/**
 * @brief Destroys a hashmap and frees all resources.
 * @param map Pointer to hashmap to destroy.
 * @return 0 on success, -1 on failure.
 */
int hashmap_destroy(hashmap_t *map);

/**
 * @brief Adds or updates a key-value pair in the hashmap.
 * @param map Pointer to hashmap.
 * @param key Key string (will be duplicated).
 * @param value Value to store.
 * @return 0 on success, -1 on allocation error, -3 on lock failure.
 */
int hashmap_add(hashmap_t *map, const char *key, size_t value);

/**
 * @brief Deletes a key-value pair from the hashmap.
 * @param map Pointer to hashmap.
 * @param key Key to delete.
 * @return 0 on success, -1 on not found or error, -3 on lock failure.
 */
int hashmap_delete(hashmap_t *map, const char *key);

/**
 * @brief Searches for a value by key in the hashmap.
 * @param map Pointer to hashmap.
 * @param key Key to search for.
 * @param value Output pointer to store found value.
 * @return 0 on success, -1 if not found or error, -3 on lock failure.
 */
int hashmap_search(hashmap_t *map, const char *key, size_t *value);

#endif // HASHMAP_H_