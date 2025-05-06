/*
 * File:    data_geo.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * Data layer, geo app specific part
 * Key features:
 *  session handling, user data handling
 */
#define _GNU_SOURCE
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#include <json-c/json.h>

#include "global.h"
#include "data.h"
#include "data_geo.h"

/** internal geo_data instance
 * serializable to a local file (load-store)
 * indexed by session and id , using json hash search */
typedef struct geo_data_t {
    struct json_object *json_session_user_index_map;
    struct json_object *json_user_id_user_index_map;
    size_t users_count;
    user_data_t users[MAX_USERS];
    pthread_mutex_t lock;
} geo_data_t;

// file header for load-store format
typedef struct geo_file_header_t{
    unsigned int version;
    size_t user_data_size;
    size_t users_number;
    size_t json_session_len;
    size_t json_id_len;
    uint32_t json_session_crc;
    uint32_t json_id_crc;
    uint32_t head_crc; //(without this)
}geo_file_header_t;

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
        const char* strss = json_object_to_json_string(geo_data->json_session_user_index_map);
        head.json_session_len= strlen(strss);
        const char *strsi = json_object_to_json_string(geo_data->json_user_id_user_index_map);
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
        json_object_put(geo_data->json_session_user_index_map);
        geo_data->json_session_user_index_map = obj;
        free(str);
        str= malloc(head.json_id_len);
        GEOFF_STOP_IF(!str);
        r = fread(str, 1, head.json_id_len, fp);
        GEOFF_STOP_IF(r != head.json_id_len);
        crc = crc32c(0, (const unsigned char*)str, head.json_id_len);
        GEOFF_STOP_IF(crc != head.json_id_crc);
        obj = json_tokener_parse(str);
        json_object_put(geo_data->json_user_id_user_index_map);
        geo_data->json_user_id_user_index_map = obj;
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
void geo_init(geo_data_t *geo_data){
    geo_data->json_session_user_index_map = json_object_new_object();
    geo_data->json_user_id_user_index_map = json_object_new_object();
    geo_data->users_count = 0;
    pthread_mutex_init(&geo_data->lock, NULL);
}

/** destroy instance */
void geo_destroy(geo_data_t *geo_data){
    json_object_put(geo_data->json_session_user_index_map);
    json_object_put(geo_data->json_user_id_user_index_map);
    pthread_mutex_destroy(&geo_data->lock);
}

/** add user with id and index */
int geo_user_add_user_id_key(data_handle_t *dh, int user_id, int index){
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    pthread_mutex_lock(&geo_data->lock);
    char key[32];
    snprintf(key, sizeof(key), "%d", user_id);
    json_object_object_add(
        geo_data->json_user_id_user_index_map,
        key,
        json_object_new_int(index)
    );
    pthread_mutex_unlock(&geo_data->lock);
    return 0;
}

/** add session key hash for a user-index */
int geo_user_add_session_key(data_handle_t *dh, const char* session_key, int index){
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    pthread_mutex_lock(&geo_data->lock);
    json_object_object_add(
        geo_data->json_session_user_index_map,
        session_key,
        json_object_new_int(index)
    );
    pthread_mutex_unlock(&geo_data->lock);
    return 0;
}

/** delete user-id hase using user_id input  */
int geo_user_delete_user_id_key(data_handle_t *dh, int user_id){
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    pthread_mutex_lock(&geo_data->lock);
    char key[32];
    snprintf(key, sizeof(key), "%d", user_id);
    struct json_object *existing_value = NULL;
    if (json_object_object_get_ex(geo_data->json_user_id_user_index_map, key, &existing_value)) {
        json_object_object_del(geo_data->json_user_id_user_index_map, key);
    }
    pthread_mutex_unlock(&geo_data->lock);
    return 0;
}

/** delete session-key hash using session key */
int geo_user_delete_session_key(data_handle_t *dh, const char* session_key){
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    pthread_mutex_lock(&geo_data->lock);
    struct json_object *existing_value = NULL;
    if (json_object_object_get_ex(geo_data->json_session_user_index_map, session_key, &existing_value)) {
        json_object_object_del(geo_data->json_session_user_index_map, session_key);
    }
    pthread_mutex_unlock(&geo_data->lock);
    return 0;
}

/** add user */
int geo_add_user(data_handle_t *dh, user_data_t* user) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    pthread_mutex_lock(&geo_data->lock);
    if (geo_data->users_count >= MAX_USERS) {
        pthread_mutex_unlock(&geo_data->lock);
        return -1;
    }
    int index= geo_data->users_count++;
    geo_data->users[index] = *user;
    // check , if something need to be delete before this?
    geo_user_add_session_key(dh, user->session_key, index);
    geo_user_add_user_id_key(dh, user->id, index);
    pthread_mutex_unlock(&geo_data->lock);
    return index;
}

user_data_t* geo_get_user(data_handle_t *dh, int idx) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    pthread_mutex_lock(&geo_data->lock);
    if (!geo_data || idx < 0 || idx >= (int)geo_data->users_count) {
        pthread_mutex_unlock(&geo_data->lock);
        return NULL;
    }
    user_data_t* user_ptr = &geo_data->users[idx];
    pthread_mutex_unlock(&geo_data->lock);
    return user_ptr;
}
int geo_get_max_user(data_handle_t *dh) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    if (!geo_data) return 0;
    pthread_mutex_lock(&geo_data->lock);
    int count = geo_data->users_count;
    pthread_mutex_unlock(&geo_data->lock);
    return count;
}
int geo_find_user_index_by_session(data_handle_t *dh, const char* session_key) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    if (!geo_data || !session_key) return -1;
    if (session_key[0] == '\0') return -1;
    pthread_mutex_lock(&geo_data->lock);
    struct json_object *idx_obj = NULL;
    if (json_object_object_get_ex(geo_data->json_session_user_index_map, session_key, &idx_obj)) {
        int idx = json_object_get_int(idx_obj);
        if (idx >= 0 && idx < (int)geo_data->users_count) {
            pthread_mutex_unlock(&geo_data->lock);
            return idx;
        }
    }
    pthread_mutex_unlock(&geo_data->lock);
    return -1;
}
int geo_find_user_index_by_user_id(data_handle_t *dh, int user_id) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    if (!geo_data || (user_id < 0)) return -1;
    char key[32];
    snprintf(key, sizeof(key), "%d", user_id);
    pthread_mutex_lock(&geo_data->lock);
    struct json_object *idx_obj = NULL;
    if (json_object_object_get_ex(geo_data->json_user_id_user_index_map, key, &idx_obj)) {
        int idx = json_object_get_int(idx_obj);
        if (idx >= 0 && idx < (int)geo_data->users_count) {
            pthread_mutex_unlock(&geo_data->lock);
            return idx;
        }
    }
    pthread_mutex_unlock(&geo_data->lock);
    return -1;
}
user_data_t* geo_find_user_by_session(data_handle_t *dh, const char* session_key) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    int index= geo_find_user_index_by_session(dh, session_key);
    if (index<0) return NULL;
    pthread_mutex_lock(&geo_data->lock);
    user_data_t* user_ptr = &geo_data->users[index];
    pthread_mutex_unlock(&geo_data->lock);
    return user_ptr;
}
user_data_t* geo_find_user_by_user_id(data_handle_t *dh, int id) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    int index= geo_find_user_index_by_user_id(dh, id);
    if (index<0) return NULL;
    pthread_mutex_lock(&geo_data->lock);
    user_data_t* user_ptr = &geo_data->users[index];
    pthread_mutex_unlock(&geo_data->lock);
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

int geo_set_user(data_handle_t *dh, user_data_t *user) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    if (user == NULL) return -1;
    if (user->session_key == NULL) return -1;
    if (user->id <= 0) return -1;
    pthread_mutex_lock(&geo_data->lock);
    int anindex_by_session = geo_find_user_index_by_session(dh, user->session_key);
    int anindex_by_user_id = geo_find_user_index_by_user_id(dh, user->id);
    if (user->index < 0) {
        if(anindex_by_user_id >= 0) {
            if (anindex_by_session >= 0) {
                user_data_t* existing_user_by_session = &geo_data->users[anindex_by_session];
                if (existing_user_by_session->id == user->id) {
                    user->index = anindex_by_session;
                    *existing_user_by_session=*user;
                }else{
                    geo_user_logout(dh, existing_user_by_session);
                    user->index = anindex_by_session;
                    *existing_user_by_session = *user;
                    geo_user_delete_user_id_key(dh, existing_user_by_session->id);
                    geo_user_add_user_id_key(dh, user->id, user->index);
                }
            }    
        }
        if (user->index >= 0) {
            pthread_mutex_unlock(&geo_data->lock);
            return 0;
        }
    }
    if (user->index >= 0) {
        geo_data->users[user->index] = *user;
    }else{
        pthread_mutex_unlock(&geo_data->lock);
        geo_add_user(dh, user);
        return 0;
    }
    pthread_mutex_unlock(&geo_data->lock);
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
    .find_user_index_by_session = geo_find_user_index_by_session,
    .find_user_index_by_user_id = geo_find_user_index_by_user_id,
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
