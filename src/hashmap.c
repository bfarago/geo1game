/*
 * File:    hashmap.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-05-14
 *
 * Generic hash map implementation using string keys.
 * Supports: init, destroy, add, delete, search
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "hashmap.h"
#include "sync.h"

#define HASH_SEED (5381)
#define HASHMAP_LOCK_TIMEOUT (1000)

static size_t hash_string(const char *str, size_t capacity) {
    size_t hash = HASH_SEED;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash % capacity;
}

int hashmap_init(hashmap_t *map, size_t capacity) {
    if (!map || capacity == 0) return -1;
    map->buckets = (hashmap_entry_t **)calloc(capacity, sizeof(hashmap_entry_t *));
    if (!map->buckets) return -1;
    map->capacity = capacity;
    map->size = 0;
    sync_mutex_init(&map->lock);
    return 0;
}

int hashmap_destroy(hashmap_t *map) {
    if (!map || !map->buckets) return -1;
    for (size_t i = 0; i < map->capacity; ++i) {
        hashmap_entry_t *entry = map->buckets[i];
        while (entry) {
            hashmap_entry_t *next = entry->next;
            free((char *)entry->key);
            free(entry);
            entry = next;
        }
    }
    free(map->buckets);
    map->buckets = NULL;
    sync_mutex_destroy(map->lock);
    map->capacity = 0;
    map->size = 0;
    return 0;
}

int hashmap_add(hashmap_t *map, const char *key, size_t value) {
    if (!map || !key) return -1;
    if (sync_mutex_lock(map->lock, HASHMAP_LOCK_TIMEOUT)) {
        errormsg("Hashmap lock timeout");
        return -3;
    }
    size_t idx = hash_string(key, map->capacity);
    hashmap_entry_t *entry = map->buckets[idx];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            entry->value = value;
            sync_mutex_unlock(map->lock);
            return 0;
        }
        entry = entry->next;
    }
    entry = (hashmap_entry_t *)malloc(sizeof(hashmap_entry_t));
    if (!entry) {
        sync_mutex_unlock(map->lock);
        return -1;
    }
    entry->key = strdup(key);
    entry->value = value;
    entry->next = map->buckets[idx];
    map->buckets[idx] = entry;
    map->size++;
    sync_mutex_unlock(map->lock);
    return 0;
}

int hashmap_delete(hashmap_t *map, const char *key) {
    if (!map || !key) return -1;
    if (sync_mutex_lock(map->lock, HASHMAP_LOCK_TIMEOUT)) {
        errormsg("Hashmap lock timeout");
        return -3;
    }
    size_t idx = hash_string(key, map->capacity);
    hashmap_entry_t *entry = map->buckets[idx], *prev = NULL;
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            if (prev) prev->next = entry->next;
            else map->buckets[idx] = entry->next;
            free((char *)entry->key);
            free(entry);
            map->size--;
            sync_mutex_unlock(map->lock);
            return 0;
        }
        prev = entry;
        entry = entry->next;
    }
    sync_mutex_unlock(map->lock);
    return -1;
}

int hashmap_search(hashmap_t *map, const char *key, size_t *value) {
    if (!map || !key || !value) return -1;
    if (sync_mutex_lock(map->lock, HASHMAP_LOCK_TIMEOUT)) {
        errormsg("Hashmap lock timeout");
        return -3;
    }
    size_t idx = hash_string(key, map->capacity);
    hashmap_entry_t *entry = map->buckets[idx];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            *value = entry->value;
            sync_mutex_unlock(map->lock);
            return 0;
        }
        entry = entry->next;
    }
    sync_mutex_unlock(map->lock);
    return -1;
}
