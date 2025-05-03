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

#include "data.h"
#include "data.h"
#include "data_geo.h"

typedef struct geo_data_t {
    struct json_object *json_session_user_index_map;
    struct json_object *json_user_id_user_index_map;
    int users_count = 0;
    user_data_t* users[MAX_USERS];
} geo_data_t;

void geo_init(geo_data *geo_data){
    geo_data->json_session_user_index_map = json_object_new_object();
    geo_data->json_user_id_user_index_map = json_object_new_object();
    geo_data->users_count = 0;
}
void geo_destroy(geo_data *geo_data){
    json_object_put(geo_data->json_session_user_index_map);
    json_object_put(geo_data->json_user_id_user_index_map);
}
void geo_store(geo_data *geo_data, const char* fname) {
    char* json_str = json_object_to_json_string(geo_data->json_session_user_index_map);
}
void geo_load(geo_data *geo_data, const char* fname) {
    json_object *obj = json_object_new_object();
    FILE* fp = fopen(fname, "r");
    if (fp) {
        char* json_str = NULL;
        size_t size = fread(json_str, 1, 1000000, fp);
        json_object_from_json_string(json_str, obj);
        json_object_put(geo_data->json_session_user_index_map);
        geo_data->json_session_user_index_map = obj;
    }
}
int geo_user_add_user_id_key(data_handle_t *dh, int user_id, int index){
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    char key[32];
    snprintf(key, sizeof(key), "%d", user_id);
    json_object_object_add(
        geo_data->json_user_id_user_index_map,
        key,
        json_object_new_int(index)
    );
    return 0;
}
int geo_user_add_session_key(data_handle_t *dh, const char* session_key, int index){
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    json_object_object_add(
        geo_data->json_session_user_index_map,
        session_key,
        json_object_new_int(index)
    );
}
int geo_user_delete_user_id_key(data_handle_t *dh, int user_id){
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    char key[32];
    snprintf(key, sizeof(key), "%d", user_id);
    struct json_object *existing_value = NULL;
    if (json_object_object_get_ex(geo_data->json_user_id_user_index_map, key, &existing_value)) {
        json_object_object_del(geo_data->json_user_id_user_index_map, key);
    }
    return 0;
}
int geo_user_delete_session_key(data_handle_t *dh, const char* session_key){
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    struct json_object *existing_value = NULL;
    if (json_object_object_get_ex(geo_data->json_session_user_index_map, session_key, &existing_value)) {
        json_object_object_del(geo_data->json_session_user_index_map, session_key);
    }
    return 0;
}
int geo_add_user(data_handle_t *dh, user_data_t* user) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    if (geo_data->users_count >= MAX_USERS) {
        return -1;
    }
    int index= geo_data->users_count++;
    geo_data->users[index] = user;
    // check , if something need to be delete before this?
    geo_user_add_session_key(dh, user->session_key, index);
    geo_user_add_user_id_key(dh, user->user_id, index);
    return index;
}

user_data_t* geo_get_user(data_handle_t *dh, int idx) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    if (!geo_data || idx < 0 || idx >= geo_data->users_count) return NULL;
    return geo_data->users[idx];
}
int geo_get_max_user(data_handle_t *dh) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    return geo_data ? geo_data->users_count : 0;
}
int geo_find_user_index_by_session(data_handle_t *dh, const char* session_key) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    if (!geo_data || !session_key) return -1;
    if (session_key[0] == '\0') return -1;
    struct json_object *idx_obj = NULL;
    if (json_object_object_get_ex(geo_data->json_session_user_index_map, session_key, &idx_obj)) {
        int idx = json_object_get_int(idx_obj);
        if (idx >= 0 && idx < geo_data->users_count) {
            return idx;
        }
    }
    return -1;
}
int geo_find_user_index_by_user_id(data_handle_t *dh, int user_id) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    if (!geo_data || (user_id < 0))) return -1;
    char key[32];
    snprintf(key, sizeof(key), "%d", user_id);
    struct json_object *idx_obj = NULL;
    if (json_object_object_get_ex(geo_data->json_user_id_user_index_map, key, &idx_obj)) {
        int idx = json_object_get_int(idx_obj);
        if (idx >= 0 && idx < geo_data->users_count) {
            return idx;
        }
    }
    return -1;
}
user_data_t* geo_find_user_by_session(data_handle_t *dh, const char* session_key) {
    int index= geo_find_user_index_by_session(dh, session_key);
    if (index<0) return NULL;
    return geo_data->users[index];
}
user_data_t* geo_find_user_by_user_id(data_handle_t *dh, const char* session_key) {
    int index= geo_find_user_index_by_user_id(dh, session_key);
    if (index<0) return NULL;
    return geo_data->users[index];
}
int geo_user_logout(data_handle_t *dh, user_data_t *user) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    int index= user->index;
    if (index>=0{
        // logout this user.
        return 0;
    })
    return -1;
}

int geo_set_user(data_handle_t *dh, user_data_t *user) {
    geo_data_t* geo_data = (geo_data_t* )dh->instance;
    // check if the provided user record contains the mandatory fields
    if (user == NULL) return -1;
    if (user->session_key == NULL) return -1;
    if (user->user_id <= 0) return -1;
    // check if the provided session was occupied before
    int anindex_by_session = geo_find_user_index_by_session(dh, user->session_key);
    int anindex_by_user_id = geo_find_user_index_by_user_id(dh, user->user_id);
    // the provided user record may have an earlier index position in case of refering to an earlier user record
    // if the previous index was not know by the caller, the index should be -1 !
    if (user->index < 0) {
        // index was not provided by caller.
        if(anindex_by_user_id >= 0) {
            // the database user_id is unique and there was an index earlier for this user_id,
            // but a session key may change.
            if (anindex_by_session >= 0) {
                user_data_t* existing_user_by_session = geo_data->users[anindex_by_session];
                // is it the same user. based on the user id vs session key.
                if (existing_user_by_session->id == user->id) {
                    // user_id is the same, session key found, then just update the index in the record.
                    user->index = anindex_by_session;
                    // we dont know which field should be updated. just update the existing user record completely now.
                    *existing_user_by_session=*user;
                }else{
                    // the provided key belongs to differnt user.
                    // todo: what to do with the existing user? cannot have two users with the same session key..
                    // the previous user should be logged out any way...
                    geo_user_logout(db, existing_user_by_session);
                    // in this way, we can overwrite the existing user record.
                    user->index = anindex_by_session;
                    *existing_user_by_session = *user;
                    // due to the index came from the same session key, we can keep the session hash, but user_id is different.
                    geo_user_delete_user_id_key(dh, existing_user_by_session->user_id); // maybe this would be part of logout?
                    geo_user_add_user_id_key(dh, user->user_id, user->index);
                }
            }    
        }
        if (user->index >= 0) {
            // updated.
            return 0;
        }
    }
    if (user->index >= 0) {
        // overwrite
        geo_data->users[user->index] = user;
        // todo: update the two json objects, according to the changed index or session key
    }else{
        // never seen, add a new one
        geo_add_user(dh, user);
    }
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
    .find_user_index_by_session = geo_find_user_index_by_session,
    .find_user_index_by_user_id = geo_find_user_index_by_user_id,
    .find_user_by_session = geo_find_user_by_session,
    .find_user_by_user_id = geo_find_user_by_user_id,

    .set_user_session_key = geo_set_user_session_key,
};

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

    g_data_descriptor_geo.init(geo_data, g_data_descriptor_geo.name);

    int result = data_register_instance(
        g_data_descriptor_geo.name,  // instance name
        NULL,                        // optional parameter
        &g_data_descriptor_geo,      // descriptor
        &g_data_api_geo,             // api descriptor
        geo_data                     // the instance
    );

    if (result < 0) {
        fprintf(stderr, "Failed to register geo_data instance\n");
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
        geo_load(geo_data);
    }
}
void data_geo_store() {
    geo_data_t* geo_data = data_get_instance(g_data_descriptor_geo.name);
    if (geo_data) {
        geo_store(geo_data);
    }
}