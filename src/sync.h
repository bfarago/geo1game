/*
 * File:    sync.h
 * Author:  Barna Faragó MYND-ideal ltd.
 * Created: 2025-05-02
 * 
 * Synchronization
 */
#ifndef SYNC_H_
#define SYNC_H_

/** sync_thread_t
 * thread type (wrapper for the pthread_t)
 */
typedef void * sync_thread_t;

/** sync_mutex_t
 * mutex type forward declaration (wrapper for the pthread_mutex_t)
 */
typedef struct sync_mutex_t sync_mutex_t;

/** sync_cond_t
 * cond type forward declaration (wrapper for the pthread_cond_t)
 */
typedef struct sync_cond_t sync_cond_t;

/** sync_mutex_init
 * Initialize a mutex.
 * @param[out] out Pointer to the location where the new mutex will be stored.
 * @return 0 on success, non-zero on failure.
 */
int sync_mutex_init(sync_mutex_t **out);

/** sync_mutex_lock
 * Lock the specified mutex, optionally with a timeout.
 * @param[in] m The mutex to lock.
 * @param[in] timeout_ms Timeout in milliseconds. 0 for immediate, ULONG_MAX for infinite.
 * @return 1 on success, 0 on timeout or failure.
 */
int sync_mutex_lock(sync_mutex_t *m, unsigned long timeout_ms);

/** sync_mutex_unlock
 * Unlock the specified mutex.
 * @param[in] m The mutex to unlock.
 * @return 1 on success, 0 on failure.
 */
int sync_mutex_unlock(sync_mutex_t *m);

/** sync_mutex_destroy
 * Destroy and free a mutex.
 * @param[in] m The mutex to destroy.
 * @return 0 on success, non-zero on failure.
 */
int sync_mutex_destroy(sync_mutex_t *m);

/** sync_cond_init
 * Initialize a condition variable.
 * @param[out] out Pointer to the location where the new condition variable will be stored.
 * @return 0 on success, non-zero on failure.
 */
int sync_cond_init(sync_cond_t **out);

/** sync_cond_wait
 * Wait on a condition variable with an associated mutex.
 * @param[in] c The condition variable to wait on.
 * @param[in] m The mutex associated with the condition.
 * @param[in] timeout_ms Timeout in milliseconds. 0 for immediate, ULONG_MAX for infinite.
 * @return 1 on success, 0 on timeout or failure.
 */
int sync_cond_wait(sync_cond_t *c, sync_mutex_t *m, unsigned long timeout_ms);

/** sync_cond_signal
 * Wake one thread waiting on the condition variable.
 * @param[in] c The condition variable.
 * @return 0 on success, non-zero on failure.
 */
int sync_cond_signal(sync_cond_t *c);

/** sync_cond_broadcast
 * Wake all threads waiting on the condition variable.
 * @param[in] c The condition variable.
 * @return 0 on success, non-zero on failure.
 */
int sync_cond_broadcast(sync_cond_t *c);

/** sync_cond_destroy
 * Destroy and free a condition variable.
 * @param[in] c The condition variable to destroy.
 * @return 0 on success, non-zero on failure.
 */
int sync_cond_destroy(sync_cond_t *c);

/** sync_det_str_dump
 * Dump the detection statistics into a buffer.
 * @param[out] buf Buffer to write the output to.
 * @param[in] len Length of the buffer.
 * @return Number of characters written.
 */
int sync_det_str_dump(char* buf, int len);

/** sync_det_clear
 * Clear all internal detection statistics.
 */
void sync_det_clear();

#endif // SYNC_H_

