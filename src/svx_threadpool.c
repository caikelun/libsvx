/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include "svx_threadpool.h"
#include "svx_errno.h"
#include "svx_log.h"
#include "svx_queue.h"

typedef struct svx_threadpool_task
{
    svx_threadpool_func_t  run;
    svx_threadpool_func_t  clean;
    void                  *arg;
    TAILQ_ENTRY(svx_threadpool_task, volatile) link;
} svx_threadpool_task_t;
typedef TAILQ_HEAD(svx_threadpool_task_queue, svx_threadpool_task, volatile) svx_threadpool_task_queue_t;

struct svx_threadpool
{
    volatile int                 running;
    pthread_t                   *volatile threads;
    volatile size_t              threads_cnt;
    svx_threadpool_task_queue_t  task_queue;
    size_t                       task_queue_size_max;
    size_t                       task_queue_size_cur;
    pthread_mutex_t              mutex;
    pthread_cond_t               cond;
};

static void *svx_threadpool_loop_func(void *arg)
{
    svx_threadpool_t      *self   = (svx_threadpool_t *)arg;
    svx_threadpool_task_t *task = NULL;

    while(self->running)
    {
        pthread_mutex_lock(&(self->mutex));

        while(TAILQ_EMPTY(&(self->task_queue)) && self->running)
        {
            pthread_cond_wait(&(self->cond), &(self->mutex));
        }
        if(!self->running)
        {
            pthread_mutex_unlock(&(self->mutex));
            return NULL;
        }
        task = TAILQ_FIRST(&(self->task_queue));
        TAILQ_REMOVE(&(self->task_queue), task, link);
        if(self->task_queue_size_max > 0)
            self->task_queue_size_cur--;

        pthread_mutex_unlock(&(self->mutex));

        task->run(task->arg);
        free(task);
    }

    return NULL;
}

int svx_threadpool_create(svx_threadpool_t **self, size_t threads_cnt, size_t max_task_queue_size)
{
    size_t i = 0;
    int    saved_errno = 0;

    if(NULL == self || 0 == threads_cnt)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, threads_cnt:%zu\n", self, threads_cnt);

    if(NULL == (*self = malloc(sizeof(svx_threadpool_t)))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    if(NULL == ((*self)->threads = malloc(threads_cnt * sizeof(pthread_t))))
    {
        free(*self);
        *self = NULL;
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    }

    (*self)->running = 1;
    (*self)->threads_cnt = threads_cnt;
    TAILQ_INIT(&((*self)->task_queue));
    (*self)->task_queue_size_max = max_task_queue_size;
    (*self)->task_queue_size_cur = 0;
    pthread_mutex_init(&((*self)->mutex), NULL);
    pthread_cond_init(&((*self)->cond), NULL);

    for(i = 0; i < threads_cnt; i++)
    {
        if(0 != (saved_errno = pthread_create(&((*self)->threads[i]), NULL, &svx_threadpool_loop_func, *self)))
        {
            (*self)->running = 0;
            pthread_cond_broadcast(&((*self)->cond));
            while(i > 0)
            {
                pthread_join((*self)->threads[i - 1], NULL);
                i--;
            }
            pthread_mutex_destroy(&((*self)->mutex));
            pthread_cond_destroy(&((*self)->cond));
            free((*self)->threads);
            free(*self);
            *self = NULL;
            SVX_LOG_ERRNO_RETURN_ERR(saved_errno, "One of thread create failed\n");
        }
    }

    return 0;
}

int svx_threadpool_destroy(svx_threadpool_t **self)
{
    svx_threadpool_task_t *task = NULL, *tmp = NULL;

    if(NULL == self)  SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    if(NULL == *self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "*self:%p\n", *self);

    pthread_mutex_lock(&((*self)->mutex));
    (*self)->running = 0;
    pthread_cond_broadcast(&((*self)->cond));
    pthread_mutex_unlock(&((*self)->mutex));

    while((*self)->threads_cnt)
    {
        pthread_join((*self)->threads[(*self)->threads_cnt - 1], NULL);
        (*self)->threads_cnt--;
    }

    /* free all unfinished tasks */
    TAILQ_FOREACH_SAFE(task, &((*self)->task_queue), link, tmp)
    {
        TAILQ_REMOVE(&((*self)->task_queue), task, link);
        if(task->clean) task->clean(task->arg);
        free(task);
    }

    pthread_mutex_destroy(&((*self)->mutex));
    pthread_cond_destroy(&((*self)->cond));
    free((*self)->threads);
    free(*self);
    *self = NULL;

    return 0;
}

int svx_threadpool_dispatch(svx_threadpool_t *self, svx_threadpool_func_t run, svx_threadpool_func_t clean,  void *arg)
{
    int                    r    = 0;
    svx_threadpool_task_t *task = NULL;

    if(NULL == self || NULL == run)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, run:%p\n", self, run);

    pthread_mutex_lock(&(self->mutex));

    if(self->task_queue_size_max > 0 && self->task_queue_size_cur >= self->task_queue_size_max)
    {
        r = SVX_ERRNO_REACH;
        goto end;
    }

    if(NULL == (task = malloc(sizeof(svx_threadpool_task_t))))
        SVX_LOG_ERRNO_GOTO_ERR(end, r = SVX_ERRNO_NOMEM, NULL);
    task->run   = run;
    task->clean = clean;
    task->arg   = arg;

    TAILQ_INSERT_TAIL(&(self->task_queue), task, link);

    if(self->task_queue_size_max > 0)
        self->task_queue_size_cur++;

    pthread_cond_signal(&(self->cond));

 end:
    pthread_mutex_unlock(&(self->mutex));
    return r;
}
