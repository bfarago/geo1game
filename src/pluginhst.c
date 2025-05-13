/*
 * File:    pluginhst.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-05-06
 * 
 * plugin host source file
 * Key features:
 *  Load and unload .so plugins
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <dirent.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <assert.h>

#include "global.h"
#include "sync.h"
#include "plugin.h"
#include "http.h"
#include "config.h"
#include "cache.h"
#include "pluginhst.h"

#define HOUSEKEEPER_LOCK_TIMEOUT_MS (20u) //  20ms
#define PLUGIN_SHUTDOWN_TIMEOUT_MS (100u) // 100ms
#define PLUGIN_SHUTDOWN_MAX_RETRY (5u)    // max 5*100ms

extern MapContext map_context;

typedef struct {
    int interval;
} plugin_timer_t;

char g_geod_plugin_dir[MAX_PATH] = PLUGIN_DIR;
extern volatile sig_atomic_t reload_plugins;

int g_PluginCount = 0;
PluginContext g_Plugins[MAX_PLUGIN];
housekeeper_t g_housekeeper;

// DEveloper's Test interface
typedef enum{
    det_args,
    det_memory,
    det_init, det_destroy,
    det_lock, det_lock_timeout, det_unlock,
    det_signal, det_broadcast,
    det_wait, det_wait_timeout,
    det_join,
    det_file, det_dlopen, det_pluginapi,
    det_max
} detid;

static unsigned char g_dets[det_max];
static unsigned short g_detlines[det_max];

static inline void reportDet(detid id, unsigned short line){
    if (g_dets[id] < 255) g_dets[id]++;
    g_detlines[id] = line;
}
void pluginhst_stat_clear(void){
    sync_det_clear();
    memset(g_dets, 0, sizeof(g_dets));
    memset(g_detlines, 0, sizeof(g_detlines));
}
int pluginhst_det_str_dump(char* buf, int len){
    int o=0;
    buf[0]=0;
    for (int i=0; i<det_max; i++){
        if (g_dets[i]){
            
            o+= snprintf(buf, len - o, "%d:%03d %03d ", i, g_dets[i], g_detlines[i]);
        }
    }
    return o;
}

void plugin_start_timer(PCHANDLER pc, int interval, void (*callback)(PCHANDLER)){
    (void)pc;
    (void)interval;
    (void)callback;
}
void plugin_stop_timer(PCHANDLER pc){
    (void)pc;
    // Stop the timer for the plugin
}

/**
 * Get a Plugin Context by its name
 */
PluginContext* get_plugin_context(const char *name){
    if (!name) return NULL;
    char pname[MAX_PATH];
    snprintf(pname, sizeof(pname), "%s.so", name);
    for (int i=0; i<g_PluginCount; i++){
        if (!strcmp(g_Plugins[i].name, pname)){
            return &g_Plugins[i];
        }
    }
    reportDet(det_args, __LINE__);
    return NULL;
}

int get_plugin_count(void){
    return g_PluginCount;
}

PluginContext* get_plugincontext_by_id(int id){
    return &g_Plugins[id];
}

/**
 * Disable plugin (used internnally, when load was not possible)
 */
void plugin_disable(PluginContext *pc){
    if (pc->handle){
        dlclose(pc->handle);
        pc->handle = NULL;
    }
    pc->state = PLUGIN_STATE_DISABLED;
}
int plugin_ownthread_find(PluginContext *pc, sync_thread_t tid){
    for (int i = 0; i < pc->thread.own_threads_count; ++i) {
        PluginOwnThreadInfo *ti = &pc->thread.own_threads[i];
        if (pthread_equal((pthread_t)ti->thread, (pthread_t)tid)) {
            return i;
        }
    }
    return -1;
}
/**
 * Plugins worker thread shall call this function when the 
 * thread started, to signalize it is running.
 * Shall be called from the worker thread!
 */
void plugin_ownthread_enter(PluginContext *pc) {
    sync_thread_t tid = (sync_thread_t) pthread_self();
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    int index= plugin_ownthread_find(pc, tid);
    if (index >= 0){
        PluginOwnThreadInfo *ti = &pc->thread.own_threads[index];
        if (ti->running){
            debugmsg("thread already running?");
        }
        ti->running = 1;
    } else {
        debugmsg("thread not found on enter");
    }
}

/**
 * plugin worker thread shall call this function at the end of the
 * thread, to signalize the thread is not running anymore.
 * Shall be called from the worker thread.
 */
void plugin_ownthread_exit(PluginContext *pc) {
    sync_thread_t tid = (sync_thread_t) pthread_self();
    int index= plugin_ownthread_find(pc, tid);
    if (index >= 0){
        PluginOwnThreadInfo *ti = &pc->thread.own_threads[index];
        if (!ti->running){
            debugmsg("thread already stopped?");
        }
        ti->running = 0;
    } else {
        debugmsg("thread not found on exit");
    }
}

/**
 * Plugin can create a worker thread, but host system need to know
 * about it, and also the plugin architecture provides a control
 * structure, to let the worker thread wait/sleep and wakeup when
 * needed (mutex+cond).
 */
int plugin_register_ownthread(PluginContext *pc, pthread_t tid, const char* name){
    // check state here
    int tc= pc->thread.own_threads_count;
    if (tc >= MAX_PLUGIN_OWN_THREADS) {
        reportDet(det_args, __LINE__);
        return -1;
    }
    for (int i = 0; i < tc; ++i) {
        if (pthread_equal((pthread_t)pc->thread.own_threads[i].thread, tid)) return 0; // already registered
    }
    int newindex= pc->thread.own_threads_count++;
    PluginOwnThreadInfo* pt= &pc->thread.own_threads[newindex];
    pt->thread = (sync_thread_t)tid;
    strncpy(pt->name, name, sizeof(pt->name));
    PluginThreadControl *control= &pt->control;
    sync_mutex_init(&control->mutex);
    sync_cond_init(&control->cond);
    control->keep_running = 1;
    pt->running = 1;
    return 0;
}


/**
 * host system helps the plugin to create a thread, it also
 * creates/register some internal datas...
 */
int plugin_create_ownthread(PluginContext *pc, plugin_thread_main_fn fn, const char* name) {
    pthread_t tid;
    if (pthread_create(&tid, NULL, fn, (void*)pc) != 0){
        reportDet(det_args, __LINE__);
        return -1;
    }
    pthread_setname_np(tid, name);
    return plugin_register_ownthread(pc, tid, name);
}

/**
 * in case of a plugin has its own threads, join those in back (kind of destroy it).
 */
int plugin_wait_for_ownthreads(PluginContext *pc){
    int errors = 0;
    int retry = PLUGIN_SHUTDOWN_MAX_RETRY;
    if (pc->thread.own_threads_count){
        do{
            errors=0;
            for (int i=0; i< pc->thread.own_threads_count; i++){
                PluginOwnThreadInfo* pt =  &pc->thread.own_threads[i];
                PluginThreadControl* ct = &pt->control;
                if (ct->mutex){
                    ct->keep_running = 0;
                    if (sync_mutex_lock(ct->mutex, PLUGIN_SHUTDOWN_TIMEOUT_MS)){
                        int ret = sync_cond_broadcast(ct->cond);
                        if (ret){
                            reportDet(det_broadcast, __LINE__);
                            debugmsg("pthread_cond_broadcast failed: %s", strerror(ret));
                            errors++;
                        }
                        sync_mutex_unlock(ct->mutex);
                    } else {
                        reportDet(det_lock, __LINE__);
                        errors++;
                    }
                }
            }
            if (--retry <=0) break;
        }while (errors);
        if (retry <= 0) {
            debugmsg("plugin_wait_for_ownthreads: exceeded max retry, attempting pthread_cancel");
            for (int i = 0; i < pc->thread.own_threads_count; i++) {
                PluginOwnThreadInfo* pt = &pc->thread.own_threads[i];
                if (pt->running && pt->thread != 0) {
                    int cancel_res = pthread_cancel((pthread_t)pt->thread);
                    if (cancel_res != 0) {
                        errormsg("pthread_cancel failed on thread %d: %s", i, strerror(cancel_res));
                    } else {
                        debugmsg("pthread_cancel issued for thread %d", i);
                    }
                }
            }
        }
        for (int i=0; i< pc->thread.own_threads_count; i++){
            PluginOwnThreadInfo* pt =  &pc->thread.own_threads[i];
            PluginThreadControl* ct = &pt->control;
            if (pt->running){
                int res= pthread_join((pthread_t)pt->thread, NULL);
                if (res){
                    debugmsg("pthread_joins failed: %s", strerror(res));
                    reportDet(det_join, __LINE__);
                    errors++;
                }
                pt->thread = (sync_thread_t)0;
                pt->running = 0;
            }
            int res = sync_cond_destroy(ct->cond);
            if (res != 0) {
                reportDet(det_destroy, __LINE__);
                debugmsg("sync_cond_destroy failed: %s", strerror(res));
                errors++;
            }
            res = sync_mutex_destroy(ct->mutex);
            if (res != 0) {
                reportDet(det_destroy, __LINE__);
                debugmsg("sync_mutex_destroy failed: %s", strerror(res));
                errors++;
            }
        }
        pc->thread.own_threads_count = 0;
    }
    return errors;
}
/**
 * loads the plugin, and resolves the function pointers
 */
int plugin_load(const char *name, int id){
    PluginContext *pc=&g_Plugins[id];
    
    pc->last_used = time(NULL); // delay the next retry based on time
    
    char filename[MAX_PLUGIN_PATH];
    snprintf(filename, sizeof(filename), "%s/%s", g_geod_plugin_dir, name);

    struct stat st;
    if (stat(filename, &st) != 0) {
        reportDet(det_file, __LINE__);
        logmsg("Failed to stat plugin file: %s", filename);
        plugin_disable(pc);
        return -1;
    }
    unsigned long file_mtime=  (unsigned long)st.st_mtime;
    // TODO: check if there is a problem to load, i.e. reload...
    if (pc->state == PLUGIN_STATE_DISABLED) {
        if (file_mtime == pc->file_mtime ){
            // do not relad, if the .so file is the same...
            return -2;
        }else{
            debugmsg("The file time was changed, retry:%s", filename);
        }
    }
    pc->state = PLUGIN_STATE_NONE;
    pc->file_mtime = (unsigned long)st.st_mtime;
    pc->handle = dlopen(filename, RTLD_LAZY|RTLD_NOLOAD);
    if (!pc->handle) pc->handle = dlopen(filename, RTLD_LAZY);
    if (!pc->handle) {
        reportDet(det_dlopen, __LINE__);
        logmsg("Failed to load plugin: %s", filename);
        plugin_disable(pc);
        return -1;
    }

    pc->state = PLUGIN_STATE_LOADING;
    strncpy(pc->name, name, sizeof(pc->name) - 1);
    pc->id = id;
    pc->plugin_init = (plugin_init_t)dlsym(pc->handle, "plugin_init");
    if (!pc->plugin_init) {
//        reportDet(det_pluginapi, __LINE__);
        debugmsg("Failed to find plugin_init in %s", filename);
        plugin_disable(pc);
        return -1;
    }
    pc->plugin_finish = (plugin_finish_t)dlsym(pc->handle, "plugin_finish");
    if (!pc->plugin_finish) {
        reportDet(det_pluginapi, __LINE__);
        logmsg("Failed to find plugin_finish in %s", filename);
        plugin_disable(pc);
        return -1;
    }
    pc->plugin_register = (plugin_register_t)dlsym(pc->handle, "plugin_register"); 
    if (!pc->plugin_register) {
        reportDet(det_pluginapi, __LINE__);
        logmsg("Failed to find plugin_register in %s", filename);
        plugin_disable(pc);
        return -1;
    }
    
    pc->state = PLUGIN_STATE_LOADED;
    //thread related functions are optional
    pc->thread_init = (plugin_thread_init_t)dlsym(pc->handle, "plugin_thread_init");
    pc->thread_finish = (plugin_thread_finish_t)dlsym(pc->handle, "plugin_thread_finish");
    // Resolve generic plugin event handler (optional)
    pc->plugin_event = (plugin_event_handler_t)dlsym(pc->handle, "plugin_event");
    //Init
    int retInit= pc->plugin_init(pc, &g_plugin_host);
    if (retInit < 0) {
        reportDet(det_pluginapi, __LINE__);
        logmsg("Failed to initialize plugin: %s", filename);
        plugin_disable(pc);
        return -1;
    }
    pc->state = PLUGIN_STATE_INITIALIZED;
    return 0;
}

/**
 * sytem calls this, when no more user access to the plugin.
 */
int plugin_unload(int id){
    PluginContext *pc = &g_Plugins[id];
    if (pc->state == PLUGIN_STATE_DISABLED) return 0;
    if (pc->state == PLUGIN_STATE_RUNNING) {
        if (pc->plugin_event) {
            Dl_info info;
            int ev = 0;
            PluginEventContext evctx = {0};
            if (dladdr((void*)pc->plugin_event, &info) && info.dli_fname && strstr(info.dli_fname, pc->name)) {
                if (pc->tried_to_shutdown > 3) {
                    ev = pc->plugin_event(pc, PLUGIN_EVENT_TERMINATE, &evctx);
                } else {
                    ev = pc->plugin_event(pc, PLUGIN_EVENT_STANDBY, &evctx);
                }
            } else {
                reportDet(det_args, __LINE__);
                logmsg("plugin_event pointer invalid or already dlclose()-d at %d:%s", id, pc->name);
            }
            if (ev != 0) {
                debugmsg("Plugin is requesting a delay for standby %d:%s", id, pc->name);
                pc->tried_to_shutdown++;
                return 2;
            }
        }
        return 0; // only sent event in running state
    } else if (pc->state == PLUGIN_STATE_INITIALIZED) {
        // all the plugin_stop finished, and there is no main task loop running
        pc->state = PLUGIN_STATE_SHUTTING_DOWN;
        if (pc->handle) {
            //todo: the thread may stucked in sleep state !
            plugin_wait_for_ownthreads(pc);
            if (pc->plugin_finish) {
                pc->plugin_finish(pc);
            }
            dlclose(pc->handle);
            pc->handle = NULL;
            pc->plugin_init = NULL;
            pc->plugin_finish = NULL;
            pc->plugin_register = NULL;
            pc->http.request_handler = NULL;
            pc->plugin_event = NULL;
            pc->state = PLUGIN_STATE_UNLOADED;
        } else {
            reportDet(det_args, __LINE__);
            logmsg("Plugin is not loaded, un-reachable code?: %d:%s", id, pc->name);
            pc->state = PLUGIN_STATE_NONE; // this would be a disabled state rather
            return -1;
        }
        return 0;
    } else {
        reportDet(det_args, __LINE__);
        logmsg("Plugin not in a state to unload: %d:%s state:%d", id, pc->name, pc->state);
        return -1;
    }
}

/**
 * User thread calls this, when starts operating on / use this plugin.
 */
int plugin_start(int id) {
    PluginContext *pc = &g_Plugins[id];
    if (pc->state == PLUGIN_STATE_DISABLED) return -1;

    int res = sync_mutex_lock(g_housekeeper.lock, HOUSEKEEPER_LOCK_TIMEOUT_MS);
    if (res != 0) {
        reportDet(det_lock_timeout, __LINE__);
        logmsg("plugin_start housekeeper lock timeout r:%s", res);
        return -1;
    }
    pc->last_used = time(NULL);
    if (pc->state != PLUGIN_STATE_RUNNING){
        if (pc->state < PLUGIN_STATE_INITIALIZED){
            if (!pc->handle) {
                if (plugin_load(pc->name, id)){
                    logmsg("Plugin not loaded: %s", pc->name);
                    sync_mutex_unlock(g_housekeeper.lock);
                    return -1;
                }
            }
            if (pc->state != PLUGIN_STATE_INITIALIZED){
                reportDet(det_init, __LINE__);
                errormsg("load error");
                sync_mutex_unlock(g_housekeeper.lock);
                return -1;
            }
        }
        
        if (pc->thread_init){
            if (pc->thread_init(pc)) {
                reportDet(det_init, __LINE__);
                logmsg("Failed to initialize plugin thread: %s", pc->name);
                sync_mutex_unlock(g_housekeeper.lock);
                return -1;
            }
        }
        pc->state = PLUGIN_STATE_RUNNING;
    }
    pc->used_count++;
    sync_mutex_unlock(g_housekeeper.lock);
    return 0;
}

/**
 * user thread calls this, when no longer needs this plugin
 */
void plugin_stop(int id) {
    PluginContext *pc = &g_Plugins[id];
    if (pc->handle) {
        if (pc->thread_finish){
            if (pc->thread_finish(pc)) {
                reportDet(det_destroy, __LINE__);
                logmsg("Failed to finish plugin thread: %s", pc->name);
                //return;
            }
        }
        int res= sync_mutex_lock(g_housekeeper.lock, HOUSEKEEPER_LOCK_TIMEOUT_MS);
        if (res == 0) {
            pc->last_used = time(NULL);
            pc->used_count--;
            if (pc->used_count < 1){
                pc->state = PLUGIN_STATE_INITIALIZED; // TODO: is it good, if there is a main thread ? or shall we keep the running state ?
            }
            sync_mutex_unlock(g_housekeeper.lock);
            //pthread_mutex_unlock(&plugin_housekeeper_mutex);
        }else{
            reportDet(det_lock_timeout, __LINE__);
            logmsg("plugin_stop housekeeper lock timeout");
            //return -1; //but not used...
        }
    }
}

void plugin_scan_and_register() {
    DIR *dir = opendir(g_geod_plugin_dir);
    if (!dir) {
        reportDet(det_file, __LINE__);
        errormsg("Cannot open plugin directory: %s", g_geod_plugin_dir);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strstr(entry->d_name, ".so")) continue;
        // Ellenőrizzük, hogy már ismerjük-e
        int known = 0;
        for (int i = 0; i < g_PluginCount; i++) {
            if (strcmp(g_Plugins[i].name, entry->d_name) == 0) {
                known = 1;
                break;
            }
        }

        if (!known && g_PluginCount < MAX_PLUGIN) {
            int newid= g_PluginCount++;
            PluginContext *pc = &g_Plugins[newid];
            // ensure the registration process starts from the begining
            pc->http_caps.http_route_count=0;
            pc->control_caps.route_count=0;
            pc->ws_caps.ws_route_count=0;
            // try to load
            pc->state = PLUGIN_STATE_NONE;
            int res = plugin_load(entry->d_name, newid);
            if (res == 0) {
                // load and init was successful, register it
                int rr = pc->plugin_register(pc, &g_plugin_host);
                if (rr == 0) {
                    debugmsg("Plugin registered: %d:%s", newid, pc->name);
                } else {
                    reportDet(det_pluginapi, __LINE__);
                    errormsg("Plugin registration failed: %d:%s", newid, pc->name);
                }
            } else {
                errormsg("Plugin load failed: %d:%s", newid, pc->name);
            }
        }
    }
    closedir(dir);
}
int housekeeper_is_running(void){
    return g_housekeeper.running;
}
int housekeeper_is_mapgen_loaded(void){
    return g_housekeeper.mapgen_loaded;
}
void housekeeper_plugins(time_t now ){
    for (int i = 0; i < g_PluginCount; i++) {
        PluginContext *pc = &g_Plugins[i];
        if (pc->handle) {
            if (pc->used_count <= 0) {
                time_t last = (time_t)pc->last_used;
                if (difftime(now, last) > PLUGIN_IDLE_TIMEOUT) {
                    int res= plugin_unload(i);
                    if (0 == res){
                        debugmsg("Plugin unloaded: %s", pc->name);
                    }else if (-1 == res) {
                        reportDet(det_pluginapi, __LINE__);
                        errormsg("Plugin unload failed: %s", pc->name);
                    }else if (2 == res) {
                        debugmsg("Kept running, due to the plugin requested more time...");
                    }
                }
            }
        }
    }
    if (reload_plugins) {
        logmsg("Received SIGUSR1 — rescanning plugins...");
        plugin_scan_and_register();
        reload_plugins = 0;
    }
}
void housekeeper_mapgen(time_t now ){
    if (g_housekeeper.mapgen_loaded) {
        if (g_housekeeper.last_mapgen_use + MAPGEN_IDLE_TIMEOUT < now) {
            debugmsg("Mapgen idle timeout. Unloading mapgen");
            if (map_context.mapgen_finish) {
                map_context.mapgen_finish();
                closeSo();
            }
        }
    }
}

void *housekeeper_thread(void *arg) {
    (void)arg; // suppress unused parameter warning
    // pthread_setname_np("housekeeper");
    sync_mutex_init(&g_housekeeper.lock);
    while (g_housekeeper.running) {
        time_t now = time(NULL);
        housekeeper_plugins(now);
        housekeeper_mapgen(now);
        housekeeper_server_clients(now);
        for (int i = 0; i<5; i++) {
            if (g_housekeeper.running) {
                sleep(1);
            } else {
                break;
            }
        }
    }
    sync_mutex_destroy(g_housekeeper.lock);
    return NULL;
}
void start_housekeeper() {
    g_housekeeper.running = 1U;
    g_housekeeper.mapgen_loaded = 0U;
    if (pthread_create(&g_housekeeper.thread_id, NULL, housekeeper_thread, NULL) != 0) {
        perror("Failed to create housekeeper thread");
        reportDet(det_init, __LINE__);
        exit(1);
    }
    pthread_setname_np(g_housekeeper.thread_id, "housekeeper");
}

void stop_housekeeper() {
    g_housekeeper.running = 0U;
    pthread_join(g_housekeeper.thread_id, NULL);
}

int server_dump_stat(char *buf, int len);
void server_stat_clear(void);
/**
 * Plugins can access to the host through this fn pointer collection
 */
const PluginHostInterface g_plugin_host = {
    .get_plugin_context = get_plugin_context,
    .start_timer = (void*)plugin_start_timer,
    .stop_timer = (void*)plugin_stop_timer,
    .logmsg = logmsg,
    .errormsg = errormsg,
    .debugmsg = debugmsg,
    .file_exists = file_exists,
    .file_exists_recent = file_exists_recent,
    .config_get_string = config_get_string,
    .config_get_int = config_get_int,
    .register_db_queue = register_db_queue,

    .thread = {
        .create_own = plugin_create_ownthread,
        .wait_for_own = plugin_wait_for_ownthreads,
        .enter_own = plugin_ownthread_enter,
        .exit_own = plugin_ownthread_exit,
    },
    .server = {
        .register_http_route = register_http_routes,
        .register_control_route = register_control_routes,
        .get_plugin_count = get_plugin_count,
        .get_plugin = get_plugincontext_by_id,
        .server_dump_stat = server_dump_stat,
        .server_det_str_dump = pluginhst_det_str_dump,
        .server_stat_clear = server_stat_clear
    },
    .http = {
        .send_response = send_response,
        .send_file = send_file,
        .send_chunk_head = send_chunk_head,
        .send_chunk_end = send_chunk_end,
        .send_chunks = send_chunks
    },
    .ws = {
        .handshake = ws_hostside_handshake,
    },
    .map = {
        .start_map_context= start_map_context,
        .stop_map_context = stop_map_context,
        .get_map_info = get_map_info
    },
    .image = {
        .context_start = image_context_start,
        .context_stop = image_context_stop,
        .create = image_create,
        .destroy = image_destroy,
        .get_buffer = image_get_buffer,
        .write_row = image_write_row
    },
    .cache = {
        .get_dir = cache_get_dir,
        .file_init = cache_file_init,
        .file_create = cache_file_create,
        .file_close = cache_file_close,
        .file_remove = cache_file_remove,
        .file_rename = cache_file_rename,
        .file_exists_recent = cache_file_exists_recent,
        .file_write = cache_file_write,
    }
};