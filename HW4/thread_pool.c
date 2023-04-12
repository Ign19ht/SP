#include "thread_pool.h"
#include <pthread.h>
#include "stdlib.h"
#include <stdatomic.h>

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
    pthread_mutex_t queue_lock;
    pthread_mutex_t status_lock;
    int *threads_status;
};

double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec/1000000000;
}

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
    struct thread_args *args = (struct thread_args*) arguments;

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

        task->status = TPOOL_STATUS_RUNNING;
        task->result = task->function(task->arg);
        task->status = TPOOL_STATUS_FINISHED;
        if (task->detach) free(task);
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
    for (int i = 0; i < max_thread_count; i++) (*pool)->threads_status[i] = 0;
    return 0;
}

int
thread_pool_thread_count(const struct thread_pool *pool) {
    return pool->thread_count;
}

int
thread_pool_delete(struct thread_pool *pool) {
    if (pool->task_queue->size > 0) return TPOOL_ERR_HAS_TASKS;
    for (int i = 0; i < pool->max_thread_count; i++) {
        if (pool->threads_status[i] == 2) return TPOOL_ERR_HAS_TASKS;
    }
    free(pool->task_queue);
    free(pool->threads_status);
    free(pool->threads);
    pthread_mutex_destroy(&pool->queue_lock);
    pthread_mutex_destroy(&pool->status_lock);
    free(pool);
    return 0;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task) {
    if (pool->task_queue->size == TPOOL_MAX_TASKS) return TPOOL_ERR_TOO_MANY_TASKS;
    task->status = TPOOL_STATUS_IN_POOL;
    pthread_mutex_lock(&pool->queue_lock);
    queue_push(pool->task_queue, task);
    pthread_mutex_unlock(&pool->queue_lock);
    for (int i = 0; i < pool->max_thread_count; i++) {
        if (pool->threads_status[i] < 2) {
            if (pool->threads_status[i] == 0) pool->thread_count++;
            struct thread_args *args = malloc(sizeof(struct thread_args));
            args->pool = pool;
            args->thread_id = i;
            pthread_create(&pool->threads[i], NULL, thread_func, (void *)args);
            break;
        }
    }
    return 0;
}

int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg) {
    (*task) = malloc(sizeof(struct thread_task));
    (*task)->function = function;
    (*task)->arg = arg;
    (*task)->status = 0;
    (*task)->detach = false;
    return 0;
}

bool
thread_task_is_finished(const struct thread_task *task) {
    return task->status == TPOOL_STATUS_FINISHED;
}

bool
thread_task_is_running(const struct thread_task *task) {
    return task->status == TPOOL_STATUS_RUNNING;
}

int
thread_task_join(struct thread_task *task, double timeout, void **result) {
    if (task->status == 0) return TPOOL_ERR_TASK_NOT_PUSHED;
    double start = get_time();
    for (;;) {
        if (get_time() - start > timeout) return TPOOL_ERR_TIMEOUT;
        if (task->status == TPOOL_STATUS_FINISHED) {
            task->status = 0;
            *result = task->result;
            return 0;
        }
    }
}

int
thread_task_delete(struct thread_task *task) {
    if (task->status != 0) return TPOOL_ERR_TASK_IN_POOL;
    free(task);
    return 0;
}

#ifdef NEED_DETACH

int
thread_task_detach(struct thread_task *task)
{
    if (task->status == 0) return TPOOL_ERR_TASK_NOT_PUSHED;
    if (task->status == TPOOL_STATUS_FINISHED) free(task);
    else task->detach = true;
    return 0;
}

#endif
