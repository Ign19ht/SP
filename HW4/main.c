#include <stdio.h>
#include "thread_pool.h"
#include <pthread.h>

static void *
task_incr_f(void *arg)
{
    ++*((int *) arg);
    return arg;
}

static void *
task_lock_unlock_f(void *arg)
{
    pthread_mutex_t *m = (pthread_mutex_t *) arg;
    pthread_mutex_lock(m);
    pthread_mutex_unlock(m);
    return arg;
}

int main(void) {
    struct thread_pool *p;
    struct thread_task *t;
    void *result;
    pthread_mutex_t m = PTHREAD_MUTEX_DEFAULT;

    thread_pool_new(3, &p);
    thread_task_new(&t, task_lock_unlock_f, &m);

    pthread_mutex_lock(&m);
    thread_pool_push_task(p, t);
    printf("%d", thread_task_join(t, 1, &result) == TPOOL_ERR_TIMEOUT);

    pthread_mutex_unlock(&m);

    thread_task_delete(t);
    thread_pool_delete(p);
}