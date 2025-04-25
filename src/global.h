#ifndef GLOBAL_H
#define GLOBAL_H

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
#define MAX_QUERY_VARS 32
#define MAX_HEADER_LINES 32
#define MAX_PATH 256
#define MAX_PLUGIN 10
#define MAX_PLUGIN_NAME 64
#define MAX_PLUGIN_PATH 512
#define PLUGIN_IDLE_TIMEOUT (60)
#define MAPGEN_IDLE_TIMEOUT (60)

#endif // GLOBAL_H