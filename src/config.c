/*
 * File:    config.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * config utility functions
 * Key features:
 *  get string and int config element
 */
#include "global.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

//#define CONFIG_FILE "/etc/myapp/config.ini"
#define CONFIG_FILE "../etc/geod/geod.ini"

#define MAX_GROUPS 64
#define MAX_KEYS_PER_GROUP 128
#define MAX_NAME_LEN 64
#define MAX_VALUE_LEN 256

typedef struct {
    char key[MAX_NAME_LEN];
    char value[MAX_VALUE_LEN];
} KeyValue;

typedef struct {
    char name[MAX_NAME_LEN];
    KeyValue keys[MAX_KEYS_PER_GROUP];
    int key_count;
} ConfigGroup;

static ConfigGroup config_groups[MAX_GROUPS];
static int config_group_count = 0;
static int config_initialized = 0;
char g_config_file[MAX_PATH] = CONFIG_FILE;


int config_search_group(const char *group) {
    for (int i = 0; i < config_group_count; i++) {
        if (strcmp(config_groups[i].name, group) == 0) {
            return i;
        }
    }
    return -1;
}
int config_search_key(int group_index, const char *key) {
    int maxkey=config_groups[group_index].key_count;
    for (int i=0; i<maxkey; i++) {
        const char *fkey = config_groups[group_index].keys[i].key;
        if (strcmp( fkey, key) == 0) {
            return i;
        }
    }
    return -1;
}

static void config_read_all() {
    FILE *f = fopen(g_config_file, "r");
    if (!f) {
        logmsg("Error opening config file: %s\n", strerror(errno));
        return;
    }

    char line[256];
    char current_group[MAX_NAME_LEN] = "";
    ConfigGroup *group = NULL;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '#' || line[0] == ';' || line[0] == 0) continue;

        if (line[0] == '[') {
            sscanf(line, "[%63[^]]", current_group);
            group = NULL;
            for (int i = 0; i < config_group_count; ++i) {
                if (strcmp(config_groups[i].name, current_group) == 0) {
                    group = &config_groups[i];
                    break;
                }
            }
            if (!group && config_group_count < MAX_GROUPS) {
                group = &config_groups[config_group_count++];
                strncpy(group->name, current_group, MAX_NAME_LEN);
                group->key_count = 0;
            }
            continue;
        }

        if (group) {
            char k[MAX_NAME_LEN], v[MAX_VALUE_LEN];
            if (sscanf(line, "%63[^=]=%255[^\n]", k, v) == 2) {
                char *k_trimmed = k;
                char *v_trimmed = v;
                while (isspace((unsigned char)*k_trimmed)) k_trimmed++;
                while (isspace((unsigned char)*v_trimmed)) v_trimmed++;
                if (group->key_count < MAX_KEYS_PER_GROUP) {
                    strncpy(group->keys[group->key_count].key, k_trimmed, MAX_NAME_LEN);
                    strncpy(group->keys[group->key_count].value, v_trimmed, MAX_VALUE_LEN);
                    group->key_count++;
                }
            }
        }
    }

    fclose(f);
    config_initialized = 1;
}

int config_get_string(const char *group, const char *key, char *buf, int buf_size, const char *default_value){
    if (!config_initialized){
        config_read_all();
    }
    if (config_initialized){
        int group_index = config_search_group(group);
        if (group_index >=0 && group_index < config_group_count){
            int key_index = config_search_key(group_index, key);
            if (key_index >= 0 && key_index < config_groups[group_index].key_count){
                strncpy(buf, config_groups[group_index].keys[key_index].value, buf_size);
                buf[buf_size - 1] = '\0';
                return 1;
            } 
        }
    }
    strncpy(buf, default_value, buf_size);
    return 0;
}
int config_get_int(const char *group, const char *key, int default_value) {
    char tmp[32];
    if (config_get_string(group, key, tmp, sizeof(tmp), "") > 0) {
        return atoi(tmp);
    }
    return default_value;
}
/*
int config_get_int(const char *group, const char *key, int default_value) {
    if (!config_initialized){
        config_read_all();
    }
    char buf[32];
    int buf_size = sizeof(buf);
    if (config_initialized){
        int group_index = config_search_group(group);
        if (group_index >=0 && group_index < config_group_count){
            int key_index = config_search_key(group_index, key);
            if (key_index >= 0 && key_index < config_groups[group_index].key_count){
                strncpy(buf, config_groups[group_index].keys[key_index].value, buf_size);
                buf[buf_size - 1] = '\0';
                return atoi(buf);
            } 
        }
    }
    return default_value;
}
*/