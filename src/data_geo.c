/*
 * File:    data_geo.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * Data layer, geo app specific part
 * Key features:
 *  session handling, user data handling
 * 
 * Known limitations
 *  user data pointer lifetime is actually longer than a
 * user session, therefore the users array indexed element
 * directly provided for other layers, and they can keep
 * and use the pointer during operation. This design pattern
 * is a little bit risky if users array need to be reordered
 * later. So, be carefull when do a refactoring in these
 * implementations... (but the actual one is fast at least)
 */
#define _GNU_SOURCE
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "global.h"
#include "sync.h"
#include "data.h"
#include "data_geo.h"
#include "json_indexlist.h"

#define DATA_GEO_LOCK_TIMEOUT (1000)

/** internal geo_data instance
 * serializable to a local file (load-store)
 * indexed by session and id , using json hash search */
typedef struct geo_data_t {
    json_indexlist_t session_index;
    json_indexlist_t userid_index;
    size_t users_count;
    user_data_t users[MAX_USERS];
    sync_mutex_t *lock_users;
} geo_data_t;

// file header for load-store format
typedef struct __attribute__((__packed__)) geo_file_header_t {
    unsigned int version;
    size_t user_data_size;
    size_t users_number;
    size_t json_session_len;
    size_t json_id_len;
    uint32_t json_session_crc;
    uint32_t json_id_crc;
    uint32_t head_crc; //(without this)
} geo_file_header_t;

// main module functions

void data_geo_init();
void data_geo_load();
void data_geo_store();
void data_geo_destroy();

/** CRC32 standard (ethernet) polynome
 */
#define CRC32_POLY  (0xedb88320)
/** crc32 function
 */
uint32_t crc32c(uint32_t crc, const unsigned char *buf, size_t len)
{
    int k;
    crc = ~crc;
    while (len--) {
        crc ^= *buf++;
        for (k = 0; k < 8; k++)
            crc = crc & 1 ? (crc >> 1) ^ CRC32_POLY : crc >> 1;
    }
    return ~crc;
}
/** macro, to help file format error handling */
#define GEOFF_STOP_IF(cond) if (cond){ line = __LINE__; goto stop;}

/** store geo instance to a binary file */
int geo_store(geo_data_t *geo_data, const char* fname) {
    int res = -1;
    FILE *fp = fopen(fname, "w");
    int line = __LINE__;
    if (fp){
        geo_file_header_t head;
        head.version = 1;
        head.user_data_size = sizeof(user_data_t);
        head.users_number = geo_data->users_count;
        const char* strss = json_object_to_json_string(geo_data->session_index.list);
        head.json_session_len= strlen(strss);
        const char *strsi = json_object_to_json_string(geo_data->userid_index.list);
        head.json_id_len= strlen(strsi);
        head.json_session_crc = crc32c(0, (const unsigned char*)strss, head.json_session_len);
        head.json_id_crc = crc32c(0, (const unsigned char*)strsi, head.json_id_len);
        head.head_crc = crc32c(0, (const unsigned char*)&head, sizeof(head)-4);
        size_t wn = sizeof(head);
        size_t w = fwrite(&head, 1, wn, fp);
        GEOFF_STOP_IF(w != wn);
        w = fwrite(geo_data->users, head.user_data_size, head.users_number, fp);
        GEOFF_STOP_IF(w != head.users_number);
        w = fwrite(strss, 1, head.json_session_len, fp);
        GEOFF_STOP_IF(w != head.json_session_len);
        w = fwrite(strsi, 1, head.json_id_len, fp);
        GEOFF_STOP_IF(w != head.json_id_len);
        res=0;
    }
stop:
    if (fp) fclose(fp);
    if (res) {
        errormsg("Error during the write operation of data_geo file in line:%d.", line);
    }
    return res;
}

/** Loads the geo instance from a binary file */
int geo_load(geo_data_t *geo_data, const char* fname) {
    int res=-1;
    FILE* fp = fopen(fname, "r");
    int line = __LINE__;
    if (fp) {
        geo_file_header_t head;
        size_t r = fread(&head, 1, sizeof(geo_file_header_t), fp);
        GEOFF_STOP_IF(r != sizeof(geo_file_header_t));
        GEOFF_STOP_IF( head.version != 1);
        GEOFF_STOP_IF( head.user_data_size != sizeof(user_data_t));
        uint32_t crc= crc32c(0, (const unsigned char*)&head, sizeof(head)-4);
        GEOFF_STOP_IF (crc != head.head_crc);
        geo_data->users_count = 0; // because we change the users array now
        r = fread(geo_data->users, head.user_data_size, head.users_number, fp);
        GEOFF_STOP_IF(r != head.users_number);
        char *str = malloc(head.json_session_len);
        GEOFF_STOP_IF(!str);
        r = fread(str, 1, head.json_session_len, fp);
        GEOFF_STOP_IF(r != head.json_session_len);
        crc = crc32c(0, (const unsigned char*)str, head.json_session_len);
        GEOFF_STOP_IF(crc != head.json_session_crc);
        json_object *obj = json_tokener_parse(str);
        json_object_put(geo_data->session_index.list);
        geo_data->session_index.list = obj;
        free(str);
        str= malloc(head.json_id_len);
        GEOFF_STOP_IF(!str);
        r = fread(str, 1, head.json_id_len, fp);
        GEOFF_STOP_IF(r != head.json_id_len);
        crc = crc32c(0, (const unsigned char*)str, head.json_id_len);
        GEOFF_STOP_IF(crc != head.json_id_crc);
        obj = json_tokener_parse(str);
        json_object_put(geo_data->userid_index.list);
        geo_data->userid_index.list = obj;
        free(str);
        geo_data->users_count = head.users_number;
        res=0; // success without an error.
    }
stop:
    if (fp) fclose(fp);
    if (res) {
        errormsg("wrong data_geo file format in line:%d.", line);
    }
    return res;
}

/** init instance */
int geo_init(geo_data_t *geo_data){
    if (geo_data){
        json_indexlist_init(&geo_data->session_index);
        json_indexlist_init(&geo_data->userid_index);
        geo_data->users_count = 0;
        sync_mutex_init(&geo_data->lock_users);
    }else{
        errormsg("Wrong argument in geo_init.");
        return -1;
    }
    return 0;
}

/** destroy instance */
void geo_destroy(geo_data_t *geo_data){
    if (geo_data){
        json_indexlist_destroy(&geo_data->session_index);
        json_indexlist_destroy(&geo_data->userid_index);
        sync_mutex_destroy(geo_data->lock_users);
    }else{
        errormsg("Wrong argument in geo_destroy");
    }
}

/** add user with id and index */
int geo_user_add_user_id_key(data_handle_t *dh, int user_id, size_t index){
    if (!dh) return -1;
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    char key[32];
    snprintf(key, sizeof(key), "%d", user_id);
    return json_indexlist_add(&geo_data->userid_index, key, index );
}

/** add session key hash for a user-index */
int geo_user_add_session_key(data_handle_t *dh, const char* session_key, size_t index){
    if (!dh) return -1;
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    return json_indexlist_add(&geo_data->session_index, session_key, index);
}

/** delete user-id hase using user_id input  */
int geo_user_delete_user_id_key(data_handle_t *dh, int user_id){
    if (!dh) return -1;
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    char key[32];
    snprintf(key, sizeof(key), "%d", user_id);
    return json_indexlist_delete(&geo_data->userid_index, key);
}

/** delete session-key hash using session key */
int geo_user_delete_session_key(data_handle_t *dh, const char* session_key){
    if (!dh) return -1;
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    return json_indexlist_delete(&geo_data->session_index, session_key);
}

/** add user internal */
int geo_add_user_locked(geo_data_t *geo_data, data_handle_t *dh, user_data_t* user, size_t *index){
    if (geo_data->users_count >= MAX_USERS) {
        return -1;
    }
    *index= geo_data->users_count++;
    geo_data->users[*index] = *user;
    geo_user_add_session_key(dh, user->session_key, *index);
    geo_user_add_user_id_key(dh, user->id, *index);
    return 0;
}
/** add user */
int geo_add_user(data_handle_t *dh, user_data_t* user) {
    if(!dh) return -1;
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    
    if (sync_mutex_lock(geo_data->lock_users, DATA_GEO_LOCK_TIMEOUT)) return -1;
    if (geo_data->users_count >= MAX_USERS) {
        sync_mutex_unlock(geo_data->lock_users);
        return -1;
    }
    int index= geo_data->users_count++;
    geo_data->users[index] = *user;
    // check , if something need to be delete before this?
    geo_user_add_session_key(dh, user->session_key, index);
    geo_user_add_user_id_key(dh, user->id, index);
    sync_mutex_unlock(geo_data->lock_users);
    return index;
}

int geo_get_user(data_handle_t *dh, int idx, user_data_t **out_user) {
    if (!dh || !out_user) return -1;
    geo_data_t* geo_data = (geo_data_t*)dh->instance;
    if (sync_mutex_lock(geo_data->lock_users, DATA_GEO_LOCK_TIMEOUT)) return -1;
    if (!geo_data || idx < 0 || idx >= (int)geo_data->users_count) {
        sync_mutex_unlock(geo_data->lock_users);
        *out_user = NULL;
        return -1;
    }
    *out_user = &geo_data->users[idx];
    sync_mutex_unlock(geo_data->lock_users);
    return 0;
}
int geo_get_max_user(data_handle_t *dh, int *out_count) {
    if(!dh) return -1;
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    if (!geo_data) return -1;
    if (sync_mutex_lock(geo_data->lock_users, DATA_GEO_LOCK_TIMEOUT)) return -1;
    *out_count = geo_data->users_count;
    sync_mutex_unlock(geo_data->lock_users);
    return 0;
}
int geo_find_user_index_by_session(data_handle_t *dh, const char* session_key, size_t *index) {
    if(!dh) return -1;
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    if (!geo_data || !session_key) return -1;
    if (session_key[0] == '\0') return -1;
    return json_indexlist_search(&geo_data->session_index, session_key, index);
}
int geo_find_user_index_by_user_id(data_handle_t *dh, int user_id, size_t *index) {
    if (!dh) return -1;
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    if (!geo_data || (user_id < 0)) return -1;
    char key[32];
    snprintf(key, sizeof(key), "%d", user_id);
    return json_indexlist_search(&geo_data->userid_index, key, index);
}
user_data_t* geo_find_user_by_session(data_handle_t *dh, const char* session_key) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    size_t index = GEO_INDEX_INVALID;
    if (geo_find_user_index_by_session(dh, session_key, &index)) return NULL;
    if (index == GEO_INDEX_INVALID) return NULL;
    if (sync_mutex_lock(geo_data->lock_users, DATA_GEO_LOCK_TIMEOUT)) return NULL;
    user_data_t* user_ptr = NULL;
    if (index < geo_data->users_count){
        user_ptr = &geo_data->users[index];
    }
    sync_mutex_unlock(geo_data->lock_users);
    return user_ptr;
}
user_data_t* geo_find_user_by_user_id(data_handle_t *dh, int id) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    size_t index = GEO_INDEX_INVALID;
    if (geo_find_user_index_by_user_id(dh, id, &index)) return NULL;
    if (index == GEO_INDEX_INVALID) return NULL;
    if (sync_mutex_lock(geo_data->lock_users, DATA_GEO_LOCK_TIMEOUT)) return NULL;
    user_data_t* user_ptr = NULL;
    if (index < geo_data->users_count){
        user_ptr = &geo_data->users[index];
    }
    sync_mutex_unlock(geo_data->lock_users);
    return user_ptr;
}
int geo_user_logout(data_handle_t *dh, user_data_t *user) {
    (void)dh;
    //geo_data_t* geo_data = (geo_data_t* )dh->instance;
    int index= user->index;
    if (index>=0){
        // logout this user.
        return 0;
    }
    return -1;
}

/** set User record in memory as the provided argument
 * 
 */
int geo_set_user(data_handle_t *dh, user_data_t *user) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    if (user == NULL) return -1;
    if (user->session_key[0] == 0) return -1;
    if (user->id <= 0) return -1;
    if (sync_mutex_lock(geo_data->lock_users, DATA_GEO_LOCK_TIMEOUT)) return -1;
    size_t anindex_by_session = GEO_INDEX_INVALID;
    size_t anindex_by_user_id = GEO_INDEX_INVALID;
    geo_find_user_index_by_session(dh, user->session_key, &anindex_by_session);
    geo_find_user_index_by_user_id(dh, user->id, &anindex_by_user_id);
    if (user->index == GEO_INDEX_INVALID){
        if(anindex_by_user_id != GEO_INDEX_INVALID) {
            if (anindex_by_session != GEO_INDEX_INVALID) {
                user_data_t* existing_user_by_session = &geo_data->users[anindex_by_session];
                if (existing_user_by_session->id == user->id) {
                    user->index = anindex_by_session;
                    *existing_user_by_session=*user;
                }else{
                    int old_id = existing_user_by_session->id;
                    geo_user_logout(dh, existing_user_by_session);
                    user->index = anindex_by_session;
                    *existing_user_by_session = *user;
                    geo_user_delete_user_id_key(dh, old_id);
                    geo_user_add_user_id_key(dh, user->id, user->index);
                }
                sync_mutex_unlock(geo_data->lock_users);
                return 0; // ok
            }    
        }
        if (user->index != GEO_INDEX_INVALID) {
            sync_mutex_unlock(geo_data->lock_users);
            return 0; // ok
        }
    }
    if (user->index != GEO_INDEX_INVALID) {
        geo_data->users[user->index] = *user;
    }else{
        geo_add_user_locked(geo_data, dh, user, &user->index);
        sync_mutex_unlock(geo_data->lock_users);
        return 0;
    }
    sync_mutex_unlock(geo_data->lock_users);
    return 0;
}

/** data descriptor class
 * base class default initialization data, the function ptrs.
 */
const data_descriptor_class_t g_data_descriptor_geo = {
    .id = 0,    // run-time type identifier
    .name = "geo",
    .init = geo_init,
    .destroy = geo_destroy,
    .load = geo_load,
    .store = geo_store,
};

/** specific api descriptor */
const data_api_geo_t g_data_api_geo = {
    .get_user = geo_get_user,   // get user data by index
    .get_max_user = geo_get_max_user, // get max user index (stored in memory)
    .add_user = geo_add_user,   // add user data
    .set_user = geo_set_user,
    .find_user_index_by_session = (geo_find_user_index_by_session_fn)geo_find_user_index_by_session,
    .find_user_index_by_user_id = (geo_find_user_index_by_user_id_fn)geo_find_user_index_by_user_id,
    .find_user_by_session = geo_find_user_by_session,
    .find_user_by_user_id = geo_find_user_by_user_id,
    
   // .set_user_session_key_fn = geo_set_user_session_key,
};

#if 0
int g_data_descriptor_geo_id = 0;

/**
 * initialize the data_geo subsystem, by registration.
 */

void data_geo_init() {
    geo_data_t* geo_data = malloc(sizeof(geo_data_t));
    if (!geo_data) {
        fprintf(stderr, "Failed to allocate geo_data\n");
        return;
    }

    // g_data_descriptor_geo.init(geo_data, g_data_descriptor_geo.name);

    int result = data_register_instance(
        g_data_descriptor_geo.name,  // instance name
        NULL,                        // optional parameter
        &g_data_descriptor_geo,      // descriptor
        &g_data_api_geo,             // api descriptor
        geo_data                     // the instance
    );

    if (result < 0) {
        fprintf(stderr, "Failed to register geo_data_t instance\n");
        free(geo_data);
    }
}

void data_geo_destroy() {
    geo_data_t* geo_data = data_get_instance(g_data_descriptor_geo.name);
    if (geo_data) {
        data_unregister_instance(g_data_descriptor_geo.name);
        free(geo_data);
    }
}
void data_geo_load() {
    geo_data_t* geo_data = data_get_instance(g_data_descriptor_geo.name);
    if (geo_data) {
        geo_load(geo_data, "data_geo");
    }
}
void data_geo_store() {
    geo_data_t* geo_data = data_get_instance(g_data_descriptor_geo.name);
    if (geo_data) {
        geo_store(geo_data, "data_geo");
    }
}
#endif
