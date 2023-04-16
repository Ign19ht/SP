#include "thread_pool.h"
#include <pthread.h>
#include "stdlib.h"
#include <stdatomic.h>
#include <math.h>

enum {
    TPOOL_STATUS_IN_POOL = 1,
    TPOOL_STATUS_RUNNING = 2,
    TPOOL_STATUS_FINISHED = 3
};

struct thread_task {
    thread_task_f function;
    void *arg;

    /* PUT HERE OTHER MEMBERS */
    atomic_int status;
    atomic_bool detach;
    pthread_mutex_t mutex;
    pthread_cond_t is_finish;
    struct thread_task *next;
    void *result;
};

struct queue {
    struct thread_task *head;
    struct thread_task *tail;
    int size;
};

struct thread_pool {
    pthread_t *threads;

    /* PUT HERE OTHER MEMBERS */
    int max_thread_count;
    struct queue *task_queue;
    int thread_count;
    int *threads_status;
    atomic_int task_count;
    pthread_mutex_t queue_lock;
    pthread_mutex_t status_lock;
};

void queue_push(struct queue *queue, struct thread_task *task) {
    if (queue->size == 0) {
        queue->head = task;
    } else {
        queue->tail->next = task;
    }
    queue->tail = task;
    queue->size++;
}

struct thread_task *queue_pop(struct queue *queue) {
    if (queue->size == 0) return NULL;
    struct thread_task *task = queue->head;
    queue->head = task->next;
    queue->size--;
    return task;
}

struct thread_args {
    struct thread_pool *pool;
    int thread_id;
};

void *thread_func(void *arguments) {
    struct thread_args *args = (struct thread_args *) arguments;

    pthread_mutex_lock(&args->pool->status_lock);
    args->pool->threads_status[args->thread_id] = 2;
    pthread_mutex_unlock(&args->pool->status_lock);

    for (;;) {
        pthread_mutex_lock(&args->pool->queue_lock);
        if (args->pool->task_queue->size == 0) {
            pthread_mutex_unlock(&args->pool->queue_lock);
            break;
        }
        struct thread_task *task = queue_pop(args->pool->task_queue);
        pthread_mutex_unlock(&args->pool->queue_lock);

        atomic_store(&task->status, TPOOL_STATUS_RUNNING);
        task->result = task->function(task->arg);
        atomic_store(&task->status, TPOOL_STATUS_FINISHED);
        pthread_cond_broadcast(&task->is_finish);
        atomic_fetch_sub(&args->pool->task_count, 1);

        if (atomic_load(&task->detach)) {
            atomic_store(&task->status, 0);
            thread_task_delete(task);
        }
    }

    pthread_mutex_lock(&args->pool->status_lock);
    args->pool->threads_status[args->thread_id] = 1;
    pthread_mutex_unlock(&args->pool->status_lock);

    free(arguments);
    return NULL;
}

int
thread_pool_new(int max_thread_count, struct thread_pool **pool) {
    if (max_thread_count <= 0 || max_thread_count > TPOOL_MAX_THREADS) return TPOOL_ERR_INVALID_ARGUMENT;
    *pool = malloc(sizeof(struct thread_pool));
    (*pool)->max_thread_count = max_thread_count;
    (*pool)->task_queue = malloc(sizeof(struct queue));
    (*pool)->task_queue->size = 0;
    (*pool)->thread_count = 0;
    (*pool)->threads = calloc(max_thread_count, sizeof(pthread_t));
    (*pool)->threads_status = calloc(max_thread_count, sizeof(int));
    pthread_mutex_init(&(*pool)->queue_lock, NULL);
    pthread_mutex_init(&(*pool)->status_lock, NULL);
    atomic_init(&(*pool)->task_count, 0);
    for (int i = 0; i < max_thread_count; i++) (*pool)->threads_status[i] = 0;
    return 0;
}

int
thread_pool_thread_count(const struct thread_pool *pool) {
    return pool->thread_count;
}

int
thread_pool_delete(struct thread_pool *pool) {
    if (atomic_load(&pool->task_count) > 0) return TPOOL_ERR_HAS_TASKS;
    free(pool->threads);
    free(pool->task_queue);
    free(pool->threads_status);
    pthread_mutex_destroy(&pool->queue_lock);
    pthread_mutex_destroy(&pool->status_lock);
    free(pool);
    return 0;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task) {
    if (atomic_load(&pool->task_count) == TPOOL_MAX_TASKS) return TPOOL_ERR_TOO_MANY_TASKS;
    atomic_fetch_add(&pool->task_count, 1);
    atomic_store(&task->status, TPOOL_STATUS_IN_POOL);
    pthread_mutex_lock(&pool->queue_lock);
    queue_push(pool->task_queue, task);
    pthread_mutex_unlock(&pool->queue_lock);

    pthread_mutex_lock(&pool->status_lock);
    for (int i = 0; i < pool->max_thread_count; i++) {
        if (pool->threads_status[i] < 2) {
            if (pool->threads_status[i] == 0) pool->thread_count++;
            struct thread_args *args = malloc(sizeof(struct thread_args));
            args->pool = pool;
            args->thread_id = i;
            pthread_create(&pool->threads[i], NULL, thread_func, (void *) args);
            break;
        }
    }
    pthread_mutex_unlock(&pool->status_lock);
    return 0;
}

int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg) {
    (*task) = malloc(sizeof(struct thread_task));
    (*task)->function = function;
    (*task)->arg = arg;
    atomic_init(&(*task)->status, 0);
    atomic_init(&(*task)->detach, false);
    pthread_mutex_init(&(*task)->mutex, NULL);
    pthread_cond_init(&(*task)->is_finish, NULL);
    return 0;
}

bool
thread_task_is_finished(const struct thread_task *task) {
    return atomic_load(&task->status) == TPOOL_STATUS_FINISHED;
}

bool
thread_task_is_running(const struct thread_task *task) {
    return atomic_load(&task->status) == TPOOL_STATUS_RUNNING;
}

int
thread_task_join(struct thread_task *task, void **result) {
    if (atomic_load(&task->status) == 0) return TPOOL_ERR_TASK_NOT_PUSHED;

    pthread_mutex_lock(&task->mutex);
    if (atomic_load(&task->status) != TPOOL_STATUS_FINISHED)
        pthread_cond_wait(&task->is_finish, &task->mutex);
    atomic_store(&task->status, 0);
    *result = task->result;
    pthread_mutex_unlock(&task->mutex);
    return 0;
}

int
thread_task_delete(struct thread_task *task) {
    if (atomic_load(&task->status) != 0) return TPOOL_ERR_TASK_IN_POOL;
    pthread_cond_destroy(&task->is_finish);
    pthread_mutex_destroy(&task->mutex);
    free(task);
    return 0;
}

#ifdef NEED_TIMED_JOIN

int
thread_task_timed_join(struct thread_task *task, double timeout, void **result) {
    if (atomic_load(&task->status) == 0) return TPOOL_ERR_TASK_NOT_PUSHED;
    if (timeout <= 0) return TPOOL_ERR_TIMEOUT;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    time_t int_part;
    long nano_part = modf(timeout, &int_part) * 1000000000;
    ts.tv_sec += int_part;
    ts.tv_nsec += nano_part;

    pthread_mutex_lock(&task->mutex);
    int rc;
    if (atomic_load(&task->status) != TPOOL_STATUS_FINISHED)
        rc = pthread_cond_timedwait(&task->is_finish, &task->mutex, &ts);
    if (rc == ETIMEDOUT) {
        pthread_mutex_unlock(&task->mutex);
        return TPOOL_ERR_TIMEOUT;
    }
    atomic_store(&task->status, 0);
    *result = task->result;
    pthread_mutex_unlock(&task->mutex);

    return 0;
}

#endif

#ifdef NEED_DETACH

int
thread_task_detach(struct thread_task *task) {
    if (atomic_load(&task->status) == 0) return TPOOL_ERR_TASK_NOT_PUSHED;
    if (atomic_load(&task->status) == TPOOL_STATUS_FINISHED) {
        atomic_store(&task->status, 0);
        thread_task_delete(task);
    }
    else atomic_store(&task->detach, true);
    return 0;
}

#endif
