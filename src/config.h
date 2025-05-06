/*
 * File:    config.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * config utility functions
 * Key features:
 *  get string and int config element
 */
#ifndef _CONFIG_H
#define _CONFIG_H

extern char g_config_file[MAX_PATH];

int config_get_string(const char *group, const char *key, char *buf, int buf_size, const char *default_value);
int config_get_int(const char *group, const char *key, int default_value);

#endif // _CONFIG_H
