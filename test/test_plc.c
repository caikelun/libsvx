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
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/time.h>
#include "svx_auto_config.h"
#include "svx_poller.h"
#include "svx_looper.h"
#include "svx_channel.h"
#include "svx_util.h"

/* test event */

#define TEST_PLC_CLIENTS_CNT 8
#define TEST_PLC_TASK_CNT    1024 /* for each client */

typedef struct
{
    int            ok;
    int            pipefd_read;
    int            pipefd_write;
    svx_channel_t *channel_read;
    svx_channel_t *channel_write;
    uint8_t        one;
} test_plc_connection_t;

typedef struct
{
    pthread_t              thd;
    int                    ok;
    svx_looper_t          *looper;
    test_plc_connection_t  conn[TEST_PLC_CLIENTS_CNT];
} test_plc_server_t;

typedef struct
{
    pthread_t thd;
    int       ok;
    int       pipefd_read;
    int       pipefd_write;
} test_plc_client_t;

static void test_plc_server_send(void *arg)
{
    test_plc_connection_t *conn = (test_plc_connection_t *)arg;
    ssize_t                n    = 0;

    /* write the byte back to the client */
    do n = write(conn->pipefd_write, &(conn->one), 1);
    while(n < 0 && EINTR == errno);
    if(1 != n)
    {
        printf("write() failed 2\n");
        conn->ok = 0;
        return;
    }
}

static void test_plc_server_read_callback(void *arg)
{
    test_plc_connection_t *conn   = (test_plc_connection_t *)arg;
    svx_looper_t          *looper = NULL;

    /* read one byte from client*/
    if(1 != read(conn->pipefd_read, &(conn->one), 1))
    {
        printf("read() failed\n");
        conn->ok = 0;
        return;
    }

    if(0 == conn->one % 2)
    {
        /* delete readable event for the current connection */
        if(0 != svx_channel_del_events(conn->channel_read, SVX_CHANNEL_EVENT_READ))
        {
            printf("svx_channel_events_del() failed\n");
            conn->ok = 0;
            return;
        }

        /* add writable event for the current connection,
           we want to send the byte back in the next round */
        if(0 != svx_channel_add_events(conn->channel_write, SVX_CHANNEL_EVENT_WRITE))
        {
            printf("svx_channel_events_add() failed\n");
            conn->ok = 0;
            return;
        }
    }
    else
    {
        if(0 != svx_channel_get_looper(conn->channel_read, &looper))
        {
            printf("svx_channel_get_looper() failed\n");
            conn->ok = 0;
            return;
        }

        if(0 != svx_looper_dispatch(looper, test_plc_server_send, NULL, conn, sizeof(*conn)))
        {
            printf("svx_looper_dispatch() failed\n");
            conn->ok = 0;
            return;
        }
    }
}

static void test_plc_server_write_callback(void *arg)
{
    test_plc_connection_t *conn = (test_plc_connection_t *)arg;
    ssize_t                n    = 0;

    /* write the byte back to the client */
    do n = write(conn->pipefd_write, &(conn->one), 1);
    while(n < 0 && EINTR == errno);
    if(1 != n)
    {
        printf("write() failed\n");
        conn->ok = 0;
        return;
    }

    /* delete writable event for the current connection */
    if(0 != svx_channel_del_events(conn->channel_write, SVX_CHANNEL_EVENT_WRITE))
    {
        printf("svx_channel_events_del() failed\n");
        conn->ok = 0;
        return;
    }
    
    /* add readable event for the current connection */
    if(0 != svx_channel_add_events(conn->channel_read, SVX_CHANNEL_EVENT_READ))
    {
        printf("svx_channel_events_add() failed\n");
        conn->ok = 0;
        return;
    }
}

static void *test_plc_server_thd_func(void *arg)
{
    test_plc_server_t *server = (test_plc_server_t *)arg;

    if(0 != svx_looper_loop(server->looper))
    {
        printf("svx_looper_loop() failed\n");
        goto end;
    }
    server->ok = 1; /* OK */

 end:
    return NULL;
}

static void *test_plc_client_thd_func(void *arg)
{
    test_plc_client_t *client = (test_plc_client_t *)arg;
    uint8_t            one = 0, one_recv = 0;
    size_t             i = 0;
    ssize_t            n = 0;
    
    for(i = 0; i < TEST_PLC_TASK_CNT; i++)
    {
        one = (uint8_t)random();

        /* write a random byte to server */
        do n = write(client->pipefd_write, &one, 1);
        while(n < 0 && EINTR == errno);
        if(1 != n)
        {
            printf("write() failed\n");
            goto end;
        }

        /* read one byte from server */
        if(1 != read(client->pipefd_read, &one_recv, 1))
        {
            printf("read() failed\n");
            goto end;
        }

        /* check */
        if(one != one_recv)
        {
            printf("data check failed\n");
            goto end;
        }
    }
    client->ok = 1; /* OK */

 end:
    return NULL;
}

static int test_plc_event_do()
{
    test_plc_server_t server;
    test_plc_client_t clients[TEST_PLC_CLIENTS_CNT];
    int               fds[2];
    size_t            i = 0;
    int               r = 1;

    /* init server */
    server.ok = 0;
    server.looper  = NULL;
    for(i = 0; i < TEST_PLC_CLIENTS_CNT; i++)
    {
        server.conn[i].ok            = 1;
        server.conn[i].pipefd_read   = -1;
        server.conn[i].pipefd_write  = -1;
        server.conn[i].channel_read  = NULL;
        server.conn[i].channel_write = NULL;
    }

    /* init clients */
    for(i = 0; i < TEST_PLC_CLIENTS_CNT; i++)
    {
        clients[i].ok            = 0;
        clients[i].pipefd_read   = -1;
        clients[i].pipefd_write  = -1;
    }

    /* create pipes */
    for(i = 0; i < TEST_PLC_CLIENTS_CNT; i++)
    {
        if(0 != pipe(fds))
        {
            printf("pipe() failed\n");
            goto end;
        }
        server.conn[i].pipefd_read = fds[0];
        clients[i].pipefd_write    = fds[1];

        if(0 != pipe(fds))
        {
            printf("pipe() failed\n");
            goto end;
        }
        server.conn[i].pipefd_write = fds[1];
        clients[i].pipefd_read      = fds[0];
    }
    
    /* create looper for server */
    if(0 != svx_looper_create(&(server.looper)))
    {
        printf("svx_looper_create() failed\n");
        goto end;
    }

    /* create channels for server */
    for(i = 0; i < TEST_PLC_CLIENTS_CNT; i++)
    {
        /* read channel */
        if(0 != svx_channel_create(&(server.conn[i].channel_read), server.looper, server.conn[i].pipefd_read, 
                                   SVX_CHANNEL_EVENT_READ))
        {
            printf("svx_channel_create() failed\n");
            goto end;
        }
        if(0 != svx_channel_set_read_callback(server.conn[i].channel_read, test_plc_server_read_callback,
                                              &(server.conn[i])))
        {
            printf("svx_channel_set_read_callback() failed\n");
            goto end;
        }

        /* write channel */
        if(0 != svx_channel_create(&(server.conn[i].channel_write), server.looper, server.conn[i].pipefd_write, 
                                   SVX_CHANNEL_EVENT_NULL))
        {
            printf("svx_channel_create() failed\n");
            goto end;
        }
        if(0 != svx_channel_set_write_callback(server.conn[i].channel_write, test_plc_server_write_callback,
                                              &(server.conn[i])))
        {
            printf("svx_channel_set_write_callback() failed\n");
            goto end;
        }
    }

    /* create thread for server */
    if(0 != pthread_create(&(server.thd), NULL, &test_plc_server_thd_func, &server))
    {
        printf("pthread_create() failed\n");
        goto end;
    }

    /* create threads for each clients */
    for(i = 0; i < TEST_PLC_CLIENTS_CNT; i++)
    {
        if(0 != pthread_create(&(clients[i].thd), NULL, &test_plc_client_thd_func, &(clients[i])))
        {
            printf("pthread_create() failed\n");
            while(i)
            {
                pthread_cancel(clients[i].thd);
                pthread_join(clients[i].thd, NULL);
                i--;
            }
            if(0 != svx_looper_quit(server.looper))
            {
                printf("svx_looper_quit() failed\n");
            }
            pthread_join(server.thd, NULL);            
            goto end;
        }
    }

    /* join threads for each clients */
    for(i = 0; i < TEST_PLC_CLIENTS_CNT; i++)
    {
        pthread_join(clients[i].thd, NULL);
    }

    /* join thread for server */
    if(0 != svx_looper_quit(server.looper))
    {
        printf("svx_looper_quit() failed\n");
        goto end;
    }
    pthread_join(server.thd, NULL);

    /* check exit status */
    if(1 != server.ok)
    {
        printf("check server exit status failed\n");
        goto end;
    }
    for(i = 0; i < TEST_PLC_CLIENTS_CNT; i++)
    {
        if(1 != server.conn[i].ok)
        {
            printf("check server connection exit status failed\n");
            goto end;
        }
    }
    for(i = 0; i < TEST_PLC_CLIENTS_CNT; i++)
    {
        if(1 != clients[i].ok)
        {
            printf("check clients exit status failed\n");
            goto end;
        }
    }
    r = 0; /* OK */

 end:
    for(i = 0; i < TEST_PLC_CLIENTS_CNT; i++)
    {
        if(server.conn[i].channel_read)
        {
            if(0 != svx_channel_destroy(&(server.conn[i].channel_read)) || 
               NULL != server.conn[i].channel_read)
            {
                printf("svx_channel_destroy() failed\n");
                r = 1;
            }
        }
        if(server.conn[i].channel_write)
        {
            if(0 != svx_channel_destroy(&(server.conn[i].channel_write)) || 
               NULL != server.conn[i].channel_write)
            {
                printf("svx_channel_destroy() failed\n");
                r = 1;
            }
        }

        if(clients[i].pipefd_read >= 0)      close(clients[i].pipefd_read);
        if(clients[i].pipefd_write >= 0)     close(clients[i].pipefd_write);
        if(server.conn[i].pipefd_read >= 0)  close(server.conn[i].pipefd_read);
        if(server.conn[i].pipefd_write >= 0) close(server.conn[i].pipefd_write);
    }

    if(NULL != server.looper)
    {
        if(0 != svx_looper_destroy(&(server.looper)) || NULL != server.looper)
        {
            printf("svx_looper_destroy() failed\n");
        }
    }

    return r;
}

/* Test timer */
#define TEST_LOOPERTIMER_ERROR_RANGE_US  (1 * 1000 * 1000)
#define TEST_LOOPERTIMER_TIME_AT_1_MS    1000
#define TEST_LOOPERTIMER_TIME_AT_2_MS    2000
#define TEST_LOOPERTIMER_TIME_AT_3_MS    3000
#define TEST_LOOPERTIMER_TIME_EVERY_1_MS 1000
#define TEST_LOOPERTIMER_TIME_EVERY_2_MS 900
#define TEST_LOOPERTIMER_TIME_EVERY_3_MS 800

typedef struct
{
    int64_t *times; /* us */
    size_t   times_size;
    size_t   times_i;
    svx_looper_timer_id_t timer_id;
} test_loopertimer_every_info_t;

static svx_looper_t *test_loopertimer_looper      = NULL;
static int64_t       test_loopertimer_time_at_1   = 0;
static int64_t       test_loopertimer_time_at_2   = 0;
static int64_t       test_loopertimer_time_after  = 0;
static int64_t       test_loopertimer_time_every_1[2]; /* run 2 times */
static int64_t       test_loopertimer_time_every_2[4]; /* run 4 times */
static int64_t       test_loopertimer_time_every_3[6]; /* run 6 times */

static test_loopertimer_every_info_t test_loopertimer_every_info_1 = {
    .times      = test_loopertimer_time_every_1,
    .times_size = sizeof(test_loopertimer_time_every_1) / sizeof(int64_t),
    .times_i    = 0,
    .timer_id    = {0, 0}
};
static test_loopertimer_every_info_t test_loopertimer_every_info_2 = {
    .times      = test_loopertimer_time_every_2,
    .times_size = sizeof(test_loopertimer_time_every_2) / sizeof(int64_t),
    .times_i    = 0,
    .timer_id    = {0, 0}
};
static test_loopertimer_every_info_t test_loopertimer_every_info_3 = {
    .times      = test_loopertimer_time_every_3,
    .times_size = sizeof(test_loopertimer_time_every_3) / sizeof(int64_t),
    .times_i    = 0,
    .timer_id    = {0, 0}
};

static void *test_loopertimer_loop_thread(void *arg)
{
    SVX_UTIL_UNUSED(arg);
    
    if(0 != svx_looper_loop(test_loopertimer_looper))
    {
        printf("svx_looper_loop() failed\n");
        exit(1);
    }

    return NULL;
}

/* cancel timer "every_1" in this thread */
static void *test_loopertimer_cancel_timer_thread(void *arg)
{
    SVX_UTIL_UNUSED(arg);
    
    if(0 != svx_looper_cancel(test_loopertimer_looper, test_loopertimer_every_info_1.timer_id))
    {
        printf("svx_loopertimer_cancel() failed\n");
        exit(1);
    }

    return NULL;
}

static void test_loopertimer_task_single(void *arg)
{
    int64_t        *now_us = (int64_t *)arg;
    struct timeval  now;

    gettimeofday(&now, NULL);
    *now_us = now.tv_sec * 1000 * 1000 + now.tv_usec;
}

static void test_loopertimer_task_every(void *arg)
{
    test_loopertimer_every_info_t *info = (test_loopertimer_every_info_t *)arg;
    struct timeval                 now;
    pthread_t                      thd_cancel_timer;

    gettimeofday(&now, NULL);
    info->times[info->times_i++] = now.tv_sec * 1000 * 1000 + now.tv_usec;
    
    if(info->times_i == info->times_size)
    {
        if(info == &test_loopertimer_every_info_1)
        {
            /* cancel timer "every_1" in another thread */
            if(0 != pthread_create(&thd_cancel_timer, NULL, &test_loopertimer_cancel_timer_thread, NULL))
            {
                printf("pthread_create() failed\n");
                exit(1);
            }
            pthread_join(thd_cancel_timer, NULL);
        }
        else
        {
            if(0 != svx_looper_cancel(test_loopertimer_looper, info->timer_id))
            {
                printf("svx_loopertimer_cancel() failed\n");
                exit(1);
            }
        }

        if(test_loopertimer_every_info_1.times_i == test_loopertimer_every_info_1.times_size &&
           test_loopertimer_every_info_2.times_i == test_loopertimer_every_info_2.times_size &&
           test_loopertimer_every_info_3.times_i == test_loopertimer_every_info_3.times_size)
        {
            /* all finished */
            if(0 != svx_looper_quit(test_loopertimer_looper))
            {
                printf("svx_looper_quit() failed\n");
                exit(1);
            }
        }
    }
}

/* add timer "at_2" in this thread */
static void *test_loopertimer_add_timer_thread(void *arg)
{
    int64_t *now_ms = (int64_t *)arg;

    /* run after */
    if(0 != svx_looper_run_at(test_loopertimer_looper, test_loopertimer_task_single, NULL,
                              &test_loopertimer_time_at_2, *now_ms + TEST_LOOPERTIMER_TIME_AT_2_MS, NULL))
    {
        printf("svx_looper_run_at() failed\n");
        exit(1);
    }

    return NULL;
}

static int test_loopertimer_check(int64_t base_us, int64_t do_us, int64_t delay_ms)
{
    int64_t delay_us = delay_ms * 1000;
    int64_t delay_real_us = do_us - base_us;

    if(base_us >= do_us) return 1;
    return llabs(delay_real_us - delay_us) <= TEST_LOOPERTIMER_ERROR_RANGE_US ? 0 : 1;
}

static int test_plc_timer_do()
{
    int            r = 1;
    size_t         i = 0;
    pthread_t      thd_loop, thd_add_timer = 0;
    struct timeval now;
    int64_t        now_ms;

    /* reset data */
    test_loopertimer_looper      = NULL;
    test_loopertimer_time_at_1   = 0;
    test_loopertimer_time_at_2   = 0;
    test_loopertimer_time_after  = 0;
    test_loopertimer_every_info_1.times_i = 0;
    test_loopertimer_every_info_1.timer_id.create_time = 0;
    test_loopertimer_every_info_1.timer_id.sequence = 0;
    test_loopertimer_every_info_2.times_i = 0;
    test_loopertimer_every_info_2.timer_id.create_time = 0;
    test_loopertimer_every_info_2.timer_id.sequence = 0;
    test_loopertimer_every_info_3.times_i = 0;
    test_loopertimer_every_info_3.timer_id.create_time = 0;
    test_loopertimer_every_info_3.timer_id.sequence = 0;
    for(i = 0; i < test_loopertimer_every_info_1.times_size; i++)
        test_loopertimer_every_info_1.times[i] = 0;
    for(i = 0; i < test_loopertimer_every_info_2.times_size; i++)
        test_loopertimer_every_info_2.times[i] = 0;
    for(i = 0; i < test_loopertimer_every_info_3.times_size; i++)
        test_loopertimer_every_info_3.times[i] = 0;

    /* create looper */
    if(0 != svx_looper_create(&test_loopertimer_looper))
    {
        printf("svx_looper_create() failed\n");
        goto end;
    }

    /* start time */
    gettimeofday(&now, NULL);
    now_ms = now.tv_sec * 1000 + now.tv_usec / 1000;

    /* run at */
    if(0 != svx_looper_run_at(test_loopertimer_looper, test_loopertimer_task_single, NULL,
                              &test_loopertimer_time_at_1, now_ms + TEST_LOOPERTIMER_TIME_AT_1_MS, NULL))
    {
        printf("svx_looper_run_at() failed\n");
        goto end;
    }

    /* run at (add timer "at_2" in another thread) */
    if(0 != pthread_create(&thd_add_timer, NULL, &test_loopertimer_add_timer_thread, &now_ms))
    {
        printf("pthread_create() failed\n");
        goto end;
    }

    /* run after */
    if(0 != svx_looper_run_after(test_loopertimer_looper, test_loopertimer_task_single, NULL,
                                 &test_loopertimer_time_after, TEST_LOOPERTIMER_TIME_AT_3_MS, NULL))
    {
        printf("svx_loopertimer_run_at() failed\n");
        goto end;
    }

    /* run every */
    if(0 != svx_looper_run_every(test_loopertimer_looper, test_loopertimer_task_every, NULL,
                                 &test_loopertimer_every_info_1, TEST_LOOPERTIMER_TIME_EVERY_1_MS,
                                 TEST_LOOPERTIMER_TIME_EVERY_1_MS,
                                 &(test_loopertimer_every_info_1.timer_id)))
    {
        printf("svx_loopertimer_run_every() failed\n");
        goto end;
    }

    /* run every */
    if(0 != svx_looper_run_every(test_loopertimer_looper, test_loopertimer_task_every, NULL,
                                 &test_loopertimer_every_info_2, TEST_LOOPERTIMER_TIME_EVERY_2_MS,
                                 TEST_LOOPERTIMER_TIME_EVERY_2_MS, 
                                 &(test_loopertimer_every_info_2.timer_id)))
    {
        printf("svx_loopertimer_run_every() failed\n");
        goto end;
    }

    /* run every */
    if(0 != svx_looper_run_every(test_loopertimer_looper, test_loopertimer_task_every, NULL,
                                 &test_loopertimer_every_info_3, TEST_LOOPERTIMER_TIME_EVERY_3_MS,
                                 TEST_LOOPERTIMER_TIME_EVERY_3_MS, 
                                 &(test_loopertimer_every_info_3.timer_id)))
    {
        printf("svx_loopertimer_run_every() failed\n");
        goto end;
    }

    /* create thread for looper */
    if(0 != pthread_create(&thd_loop, NULL, &test_loopertimer_loop_thread, NULL))
    {
        printf("pthread_create() failed\n");
        goto end;
    }

    /* join thread */
    pthread_join(thd_loop, NULL);

#if 0
    /* dump */
    printf("base   : %"PRIi64"\n", now_ms * 1000);
    printf("at 1   : %"PRIi64"\n", test_loopertimer_time_at_1);
    printf("at 2   : %"PRIi64"\n", test_loopertimer_time_at_2);
    printf("after  : %"PRIi64"\n", test_loopertimer_time_after);
    for(i = 0; i < test_loopertimer_every_info_1.times_size; i++)
        printf("every 1: %"PRIi64"\n", test_loopertimer_every_info_1.times[i]);
    for(i = 0; i < test_loopertimer_every_info_2.times_size; i++)
        printf("every 2: %"PRIi64"\n", test_loopertimer_every_info_2.times[i]);
    for(i = 0; i < test_loopertimer_every_info_3.times_size; i++)
        printf("every 3: %"PRIi64"\n", test_loopertimer_every_info_3.times[i]);
    printf("\n");
#endif

    /* check */
    if(0 != test_loopertimer_check(now_ms * 1000, test_loopertimer_time_at_1,
                                   TEST_LOOPERTIMER_TIME_AT_1_MS))
    {
        printf("check test_loopertimer_time_at_1 failed\n");
        goto end;
    }
    if(0 != test_loopertimer_check(now_ms * 1000, test_loopertimer_time_at_2,
                                   TEST_LOOPERTIMER_TIME_AT_2_MS))
    {
        printf("check test_loopertimer_time_at_2 failed\n");
        goto end;
    }
    if(0 != test_loopertimer_check(now_ms * 1000, test_loopertimer_time_after,
                                   TEST_LOOPERTIMER_TIME_AT_3_MS))
    {
        printf("check test_loopertimer_time_after failed\n");
        goto end;
    }
    for(i = 0; i < test_loopertimer_every_info_1.times_size; i++)
    {
        if(0 != test_loopertimer_check(0 == i ? now_ms * 1000 : test_loopertimer_every_info_1.times[i - 1],
                                       test_loopertimer_every_info_1.times[i],
                                       TEST_LOOPERTIMER_TIME_EVERY_1_MS))
        {
            printf("check test_loopertimer_time_every_1[%zu] failed\n", i);
            goto end;
        }
    }
    for(i = 0; i < test_loopertimer_every_info_2.times_size; i++)
    {
        if(0 != test_loopertimer_check(0 == i ? now_ms * 1000 : test_loopertimer_every_info_2.times[i - 1],
                                       test_loopertimer_every_info_2.times[i],
                                       TEST_LOOPERTIMER_TIME_EVERY_2_MS))
        {
            printf("check test_loopertimer_time_every_2[%zu] failed\n", i);
            goto end;
        }
    }
    for(i = 0; i < test_loopertimer_every_info_3.times_size; i++)
    {
        if(0 != test_loopertimer_check(0 == i ? now_ms * 1000 : test_loopertimer_every_info_3.times[i - 1],
                                       test_loopertimer_every_info_3.times[i],
                                       TEST_LOOPERTIMER_TIME_EVERY_3_MS))
        {
            printf("check test_loopertimer_time_every_3[%zu] failed\n", i);
            goto end;
        }
    }

    r = 0; /* OK */

 end:
    if(thd_add_timer)
    {
	    pthread_join(thd_add_timer, NULL);
    }
    if(test_loopertimer_looper)
    {
        if(0 != svx_looper_destroy(&test_loopertimer_looper) || NULL != test_loopertimer_looper)
        {
            printf("svx_looper_destroy() failed\n");
        }
    }

    return r;
}

/* test event & timer */
static int test_plc_do()
{
    int r = 0;

    if(0 != (r = test_plc_event_do())) return r;
    if(0 != (r = test_plc_timer_do())) return r;

    return 0;
}

int test_plc_runner()
{
    int r = 0;
    svx_poller_fixed_t svx_poller_fixed_saved = svx_poller_fixed;

#if SVX_HAVE_EPOLL
    svx_poller_fixed = SVX_POLLER_FIXED_EPOLL;
    if(0 != (r = test_plc_do()))
    {
        printf("mode: FIX_EPOLL. failed\n");
        goto end;
    }
#endif

    svx_poller_fixed = SVX_POLLER_FIXED_POLL;
    if(0 != (r = test_plc_do()))
    {
        printf("mode: FIX_POLL. failed\n");
        goto end;
    }

    svx_poller_fixed = SVX_POLLER_FIXED_SELECT;
    if(0 != (r = test_plc_do()))
    {
        printf("mode: FIX_SELECT. failed\n");
        goto end;
    }

 end:
    svx_poller_fixed = svx_poller_fixed_saved;
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    return r;
}
