/*
 * File:    sync.c
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-05-02
 * 
 * Synchronization
 */
#define _POSIX_C_SOURCE 200112L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "sync.h"
#include "global.h"

#define errormsg(...)
#define logmsg(...)
#define debugmsg(...)

typedef enum{
    det_args,
    det_memory,
    det_init,
    det_destroy,
    det_lock,
    det_lock_timeout,
    det_unlock,
    det_signal,
    det_broadcast,
    det_wait,
    det_wait_timeout,
    det_max
} detid;

static unsigned char g_dets[det_max];
static unsigned short g_detlines[det_max];

static inline void reportDet(detid id, unsigned short line){
    if (g_dets[id] < 255) g_dets[id]++;
    g_detlines[id] = line;
}
int sync_det_str_dump(char* buf, int len){
    int o=0;
    buf[0]=0;
    for (int i=0; i<det_max; i++){
        if (g_dets[i]){
            
            o+= snprintf(buf, len - o, "%d:%03d %03d ", i, g_dets[i], g_detlines[i]);
        }
    }
    return o;
}
void sync_det_clear(){
    memset(g_dets, 0, sizeof(g_dets));
    memset(g_detlines, 0, sizeof(g_detlines));
}
struct sync_mutex_t {
    pthread_mutex_t native;
};

struct sync_cond_t {
    pthread_cond_t native;
};

int sync_mutex_init(sync_mutex_t **out) {
    if (!out) {
        reportDet(det_args, __LINE__);
        return EINVAL;
    }
    *out = malloc(sizeof(sync_mutex_t));
    if (!*out) {
        reportDet(det_memory, __LINE__);
        return ENOMEM;
    }
    int res = pthread_mutex_init(&(*out)->native, NULL);
    if (res != 0) {
        reportDet(det_init, __LINE__);
        free(*out);
        *out = NULL;
    }
    return res;
}

int sync_mutex_destroy(sync_mutex_t *m) {
    int res=0;
    if (m) {
        res=pthread_mutex_destroy(&m->native);
        if (res != 0) {
            reportDet(det_destroy, __LINE__);
        }
        //m->native = NULL;
        free(m);
    }else{
        reportDet(det_args, __LINE__);
    }
    return res;
}

int sync_cond_init(sync_cond_t **out) {
    if (!out) {
        reportDet(det_args, __LINE__);
        return EINVAL;
    }
    *out = malloc(sizeof(sync_cond_t));
    if (!*out) {
        reportDet(det_memory, __LINE__);
        return ENOMEM;
    }
    int res = pthread_cond_init(&(*out)->native, NULL);
    if (res != 0) {
        reportDet(det_init, __LINE__);
        free(*out);
        *out = NULL;
    }
    return res;
}

int sync_cond_signal(sync_cond_t *c) {
    if (!c) {
        reportDet(det_args, __LINE__);
        return EINVAL;
    }
    int ret = pthread_cond_signal(&c->native);
    if (ret){
        reportDet(det_signal, __LINE__);
    }
    return ret;
}

int sync_cond_broadcast(sync_cond_t *c) {
    if (!c) {
        reportDet(det_args, __LINE__);
        return EINVAL;
    }
    int ret= pthread_cond_broadcast((pthread_cond_t *)c);
    if (ret){
        reportDet(det_broadcast, __LINE__);
    }
    return ret;
}

int sync_cond_destroy(sync_cond_t *c) {
    int res=0;
    if (c) {
        //if (c->native){
            res=pthread_cond_destroy(&c->native);
            if (res){
                reportDet(det_destroy, __LINE__);
            }
            //c->native=0;
            free(c);
        //}
    }else{
        reportDet(det_args, __LINE__);
    }
    return res;
}

#ifdef __APPLE__
int sync_mutex_lock(sync_mutex_t *m, const unsigned long timeout_ms) {
    if (!m) {
        reportDet(det_args, __LINE__);
        return EINVAL;
    }
    const unsigned int sleep_ns = 1000000; // 1 ms
    const int max_tries = timeout_ms;

    for (int i = 0; i < max_tries; ++i) {
        int res = pthread_mutex_trylock(&m->native);
        if (res == 0) return 0;
        if (res != EBUSY) {
            errormsg("Mutex lock failed: %d", res);
            return res;
        }
        struct timespec ts = {0, sleep_ns};
        nanosleep(&ts, NULL);
    }

    errormsg("Mutex lock timed out (emulated)");
    return ETIMEDOUT;
}
#else
int sync_mutex_lock(sync_mutex_t *m, const unsigned long timeout_ms) {
    if (!m) {
        reportDet(det_args, __LINE__);
        return EINVAL;
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    int res= pthread_mutex_timedlock(&m->native, &ts);
    if (res == ETIMEDOUT) {
        reportDet(det_lock_timeout, __LINE__);
        errormsg("Mutex lock timed out");
    } else if (res != 0) {
        reportDet(det_lock, __LINE__);
        errormsg("Mutex lock failed: %d", res);
    }   
    return res;
}
#endif

int sync_mutex_unlock(sync_mutex_t *m){
    if (!m) {
        reportDet(det_args, __LINE__);
        return EINVAL;
    }
    int res = pthread_mutex_unlock(&m->native);
    if (res != 0) {
        reportDet(det_unlock, __LINE__);
    }
    return res;
}

int sync_cond_wait(sync_cond_t *c, sync_mutex_t *m, const unsigned long timeout_ms) {
    if (!c || !m) {
        reportDet(det_args, __LINE__);
        return EINVAL;
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    int res= pthread_cond_timedwait(&c->native, &m->native, &ts);
    if (res == ETIMEDOUT) {
        reportDet(det_wait_timeout, __LINE__);
        debugmsg("Condition wait timed out");
    } else if (res != 0) {
        reportDet(det_wait, __LINE__);
        errormsg("Condition wait failed: %d", res);
    }
    return res;
}
