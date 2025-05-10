/*
 * File:    data_geo.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * Data layer, geo app specific part
 * Key features:
 *  session handling, user data handling
 */
#ifndef DATA_GEO_H
#define DATA_GEO_H
#include "data.h"

#define MAX_USERS (100)
#define MAX_SESSION_KEY_SIZE (64)
#define MAX_NICK_SIZE (64)

/** user data record type
 * holds one user: session_key, internal index, user_id
 * lat, lon, alt, version, nick name.
 */
typedef struct {
    char session_key[MAX_SESSION_KEY_SIZE];
    size_t index;
    int id;
    double lat, lon, alt;
    unsigned int version;
    char nick[MAX_NICK_SIZE];
} user_data_t;

//forward declaration of the geo_data type
struct  geo_data_t;

/** These are the driver specific api functions */
/** get user by internal index (not user_id !) */
typedef int (*geo_get_user_fn)(data_handle_t *dh, int idx, user_data_t **out_user);
/** get internally stored max user index */
typedef int (*geo_get_max_user_fn)(data_handle_t *dh, int *out_count);
typedef int (*geo_add_user_fn)(data_handle_t *dh, user_data_t* user_data);

/** find user index by session key */
typedef int (*geo_find_user_index_by_session_fn)(data_handle_t *dh, const char* session_key);
/** find user index by user_id */
typedef int (*geo_find_user_index_by_user_id_fn)(data_handle_t *dh, int user_id);

/** find user by session key */
typedef user_data_t* (*geo_find_user_by_session_fn)(data_handle_t *dh, const char* session_key);
/** find user by session key */
typedef user_data_t* (*geo_find_user_by_user_id_fn)(data_handle_t *dh, int user_id);
/** set user */
typedef int (*geo_set_user_fn)(data_handle_t *dh, user_data_t *user_data);
//typedef int (*set_user_session_key_fn)(data_handle_t *dh, int user_id, const char* session_key);

/** you have to cast the general driver provided api fn to this. */
typedef struct data_api_geo_t {
    geo_get_user_fn get_user; // get user by internal index (not user_id !)
    geo_get_max_user_fn get_max_user; // get internally stored max user index
    geo_add_user_fn add_user; // add user
    geo_set_user_fn set_user;
    geo_find_user_index_by_session_fn find_user_index_by_session;
    geo_find_user_index_by_user_id_fn find_user_index_by_user_id;
    geo_find_user_by_session_fn find_user_by_session;
    geo_find_user_by_user_id_fn find_user_by_user_id;

    //set_user_session_key_fn set_user_session_key; // set user session key
} data_api_geo_t;

#endif //DATA_GEO_H