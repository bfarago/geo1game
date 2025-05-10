/*
 * File:    global.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-04-10
 * 
 * Global header
 * Key features:
 *  other .c files are usually includes this file, so common things can be here.
 */
#ifndef GLOBAL_H
#define GLOBAL_H

#include <stddef.h>

// Your global declarations and definitions go here.
#define PORT 8008       // http service is listening on this port

#define GEOD_PIDFILE "../var/geod.pid"      // var folder is the best place to put pidfile
//#define GEOD_PIDFILE "/tmp/geod.pid"      // tmp folder is volnumable
//#define GEOD_PIDFILE "/var/run/geod.pid"  //it is only possible for root user to write to /var/run
#define GEOD_LOGFILE "../var/geod.log"
#define REGIONS_FILE "../var/regions.bin"
#define PLUGIN_DIR "../plugins"
#define CACHE_DIR "../var/cache"

#define BUF_SIZE 8192
#define CACHE_TIME 3600 
#define MAX_LINE 256
#define MAX_QUERY_VARS 100
#define MAX_HEADER_LINES 100
#define MAX_PATH 256
#define MAX_PLUGIN 32
#define MAX_PLUGIN_NAME 64
#define MAX_PLUGIN_PATH 512
#define PLUGIN_IDLE_TIMEOUT (60)
#define MAPGEN_IDLE_TIMEOUT (60)


#define GEO_INDEX_INVALID ((size_t)-1)

extern void logmsg(const char *fmt, ...) ;
extern void errormsg(const char *fmt, ...) ;
extern void debugmsg(const char *fmt, ...) ;

extern int file_exists(const char *path);
extern int file_exists_recent(const char *path, int interval);
extern int housekeeper_is_running(void);
extern int housekeeper_is_mapgen_loaded(void);

#endif // GLOBAL_H