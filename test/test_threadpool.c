/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "svx_threadpool.h"
#include "svx_errno.h"

#define TEST_THREADPOOL_THD_CNT  8
#define TEST_THREADPOOL_TASK_CNT 1024

static pthread_mutex_t test_threadpool_mutex           = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  test_threadpool_cond            = PTHREAD_COND_INITIALIZER;
static int             test_threadpool_errno_reach_cnt = 0;

static void test_threadpool_task_run(void *arg)
{
    int *seed_p = (int *)arg;

    pthread_mutex_lock(&test_threadpool_mutex);
    (*seed_p)++;
    if(TEST_THREADPOOL_TASK_CNT == *seed_p + test_threadpool_errno_reach_cnt)
        pthread_cond_signal(&test_threadpool_cond);
    pthread_mutex_unlock(&test_threadpool_mutex);
}

static int test_threadpool_do(size_t max_task_queue_size)
{
    svx_threadpool_t *threadpool = NULL;
    int r = 0;
    int i = 0;
    int seed = 0;

    test_threadpool_errno_reach_cnt = 0;

    /* create threadpool */
    if(0 != svx_threadpool_create(&threadpool, TEST_THREADPOOL_THD_CNT, max_task_queue_size))
    {
        printf("svx_threadpool_create() failed\n");
        return 1;
    }

    /* dispatch tasks */
    for(i = 0; i < TEST_THREADPOOL_TASK_CNT; i++)
    {
        if(0 != (r = svx_threadpool_dispatch(threadpool, test_threadpool_task_run, NULL, &seed)))
        {
            if(max_task_queue_size > 0 && SVX_ERRNO_REACH == r)
            {
                pthread_mutex_lock(&test_threadpool_mutex);
                test_threadpool_errno_reach_cnt++;
                pthread_mutex_unlock(&test_threadpool_mutex);
            }
            else
            {
                printf("svx_threadpool_dispatch() failed\n");
                return 1;
            }
        }
    }

    /* wait for completion */
    pthread_mutex_lock(&test_threadpool_mutex);
    while(seed + test_threadpool_errno_reach_cnt < TEST_THREADPOOL_TASK_CNT)
        pthread_cond_wait(&test_threadpool_cond, &test_threadpool_mutex);
    pthread_mutex_unlock(&test_threadpool_mutex);

    /* destroy threadpool */
    if(0 != svx_threadpool_destroy(&threadpool) || NULL != threadpool)
    {
        printf("svx_threadpool_destroy() failed\n");
        return 1;
    }

    /* check result */
    if(seed + test_threadpool_errno_reach_cnt != TEST_THREADPOOL_TASK_CNT)
    {
        printf("check failed. seed:%d, failed_cnt:%d, task_cnt:%d\n", seed, 
               test_threadpool_errno_reach_cnt, TEST_THREADPOOL_TASK_CNT);
        return 1;
    }
    if(max_task_queue_size > 0 && 0 == test_threadpool_errno_reach_cnt)
    {
        printf("Max task queue size:%zu. Dispath failed not happen, "
               "try to reduced the max task queue size and run again\n",
               max_task_queue_size);
        return 1;
    }

    return 0;
}

int test_threadpool_runner()
{
    int r = 0;

    /* test for unlimited task queue size */
    if(0 != (r = test_threadpool_do(0))) goto end;

    /* test for limited task queue size */
    if(0 != (r = test_threadpool_do(32))) goto end;

 end:
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    return r;
}
