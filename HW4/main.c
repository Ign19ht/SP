#include <stdio.h>
#include "thread_pool.h"

static void *
task_incr_f(void *arg)
{
    ++*((int *) arg);
    return arg;
}

int main(void) {
    struct thread_pool *p;
    struct thread_task *t;
    thread_pool_new(3, &p);
    int arg = 0;
    void *result;
    thread_task_new(&t, task_incr_f, &arg);
//    printf("%d\n", thread_pool_thread_count(p));
    thread_pool_push_task(p, t);
    thread_task_join(t, &result);
    printf("%d %d\n", result == &arg, arg);
//    printf("%d\n", thread_pool_thread_count(p));
    thread_task_delete(t);
    thread_pool_delete(p);
}