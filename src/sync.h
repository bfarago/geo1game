#ifndef SYNC_H_
#define SYNC_H_

typedef void * sync_thread_t;

typedef struct sync_mutex_t sync_mutex_t;
typedef struct sync_cond_t sync_cond_t;

int sync_mutex_init(sync_mutex_t **out);
int sync_mutex_lock(sync_mutex_t *m, unsigned long timeout_ms);
int sync_mutex_unlock(sync_mutex_t *m);
int sync_mutex_destroy(sync_mutex_t *m);

int sync_cond_init(sync_cond_t **out);
int sync_cond_wait(sync_cond_t *c, sync_mutex_t *m, unsigned long timeout_ms);
int sync_cond_signal(sync_cond_t *c);
int sync_cond_broadcast(sync_cond_t *c);
int sync_cond_destroy(sync_cond_t *c);

int sync_det_str_dump(char* buf, int len);

#endif // SYNC_H_

