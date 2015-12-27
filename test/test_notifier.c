/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include "svx_notifier.h"

#define TEST_NOTIFIER_NOTIFY_CNT 1024

static pthread_mutex_t test_notifier_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  test_notifier_cond  = PTHREAD_COND_INITIALIZER;
static svx_notifier_t *test_notifier       = NULL;
static int             test_notifier_fd    = -1;
static int             test_notifier_seed  = 0;

static void *test_notifier_thd_func(void *arg)
{
    struct pollfd event = {.fd = test_notifier_fd, .events = POLLIN, .revents = 0};
    int           nfds  = -1;

    while(1)
    {
        /* select/poll/epoll wait here */
        if((nfds = poll(&event, 1, -1)) < 0)
        {
            if(EINTR == errno)
            {
                printf("poll() interrupted by signal\n");
                usleep(10 * 1000);
                continue;
            }
            else
            {
                printf("poll() failed\n");
                break;                
            }
        }
        if(1 != nfds)
        {
            printf("1 != nfds. nfds:%d\n", nfds);
            break;
        }

        /* recevie notify */
        if(0 != svx_notifier_recv(test_notifier))
        {
            printf("poll() failed\n");
            break;                
        }

        /* do some job */
        pthread_mutex_lock(&test_notifier_mutex);
        test_notifier_seed++;
        pthread_cond_signal(&test_notifier_cond);
        pthread_mutex_unlock(&test_notifier_mutex);

        /* time to exit */
        if(test_notifier_seed >= TEST_NOTIFIER_NOTIFY_CNT)
            break;
    }

    return NULL;
}

int test_notifier_runner()
{
    pthread_t thd;
    int       i = 0;
    int       r = 0;
    
    /* create notifier */
    if(0 != (r = svx_notifier_create(&test_notifier, &test_notifier_fd) || test_notifier_fd < 0))
    {
        printf("svx_notifier_create() failed\n");
        goto end;
    }

    /* create thread for calc seed */
    if(0 != (r = pthread_create(&thd, NULL, &test_notifier_thd_func, NULL)))
    {
        printf("pthread_create() failed\n");
        goto end;
    }

    for(i = 0; i < TEST_NOTIFIER_NOTIFY_CNT; i++)
    {
        /* send a notice to thread for seed++ */
        if(0 != (r = svx_notifier_send(test_notifier)))
        {
            printf("svx_notifier_send() failed\n");
            goto end;
        }

        /* wait for the seed++ finished */
        pthread_mutex_lock(&test_notifier_mutex);
        while(test_notifier_seed != i + 1)
            pthread_cond_wait(&test_notifier_cond, &test_notifier_mutex);
        pthread_mutex_unlock(&test_notifier_mutex);
    }

    /* join thread */
    pthread_join(thd, NULL);

    /* check */
    if(test_notifier_seed != TEST_NOTIFIER_NOTIFY_CNT)
    {
        printf("seed:%d, notify_cnt:%d\n", test_notifier_seed, TEST_NOTIFIER_NOTIFY_CNT);
        r = 1;
    }

 end:
    if(test_notifier)
    {
        if(0 != svx_notifier_destroy(&test_notifier) || NULL != test_notifier)
        {
            printf("svx_notifier_destroy() failed\n");
            r = 1;
        }
    }
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    return r;
}
