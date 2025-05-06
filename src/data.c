/*
 * File:    data.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * Data abstraction layer
 * Key features:
 *  init-destroy
 *  load-store : from a local file when service restarts
 *  register class/instance
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

#define MAX_DATA_DESCRIPTORS (10)

int g_data_descriptors_count = 0;
data_descriptor_class_t g_data_descriptors[MAX_DATA_DESCRIPTORS];

int data_register_descriptor(data_descriptor_class_t *descriptor){
    if(g_data_descriptors_count >= MAX_DATA_DESCRIPTORS){
        return -1;
    }
    int index = g_data_descriptors_count++;
    g_data_descriptors[index] = *descriptor;
    return index;
}

// --- Data handle API ---
int g_data_handles_count = 0;
data_handle_t g_data_handles[MAX_DATA_HANDLES];

data_handle_t* data_get_handle_by_index(int index) {
    if (index < 0 || index >= g_data_handles_count) {
        return NULL;
    }
    return &g_data_handles[index];
}
data_handle_t* data_get_handle_by_name(const char* name) {
    for (int i = 0; i < g_data_handles_count; ++i) {
        if (strcmp(g_data_handles[i].name, name) == 0) {
            return &g_data_handles[i];
        }
    }
    return NULL;
}
void* data_get_instance(const char* name) {
    data_handle_t* handle = data_get_handle_by_name(name);
    if (handle) return handle->instance;
    return NULL;
}

int data_register_instance(
    const char* name, const char *param,
    const data_descriptor_class_t* descriptor, const void* specific_api,
    void* instance)
{
    if (g_data_handles_count >= MAX_DATA_HANDLES) {
        return -1;
    }
    g_data_handles[g_data_handles_count].name = name;
    g_data_handles[g_data_handles_count].descriptor = descriptor;
    g_data_handles[g_data_handles_count].instance = instance;
    g_data_handles[g_data_handles_count].param = param;
    g_data_handles[g_data_handles_count].specific_api = specific_api;
    return g_data_handles_count++;
}

void data_unregister_instance(const char* name){
    (void)name;
    errormsg("data_unregister_instance is not implemented");
}