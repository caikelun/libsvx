/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include "svx_looper.h"
#include "svx_queue.h"
#include "svx_tree.h"
#include "svx_poller.h"
#include "svx_channel.h"
#include "svx_notifier.h"
#include "svx_errno.h"
#include "svx_log.h"

#define SVX_LOOPER_EVENT_ACTIVE_CHANNELS_SIZE_INIT 16
#define SVX_LOOPER_PENDING_BUF_SIZE_INIT           1024

typedef struct
{
    svx_looper_func_t run;
    svx_looper_func_t clean;
    size_t            arg_block_size;
} svx_looper_pending_t;

/* rb-tree for timer task */
typedef struct svx_looper_timer
{
    svx_looper_func_t           run;
    svx_looper_func_t           clean;
    void                       *arg;
    int64_t                     when_ms; /* milliseconds since epoch */
    int64_t                     interval_ms;
    svx_looper_timer_id_t       id;
    RB_ENTRY(svx_looper_timer)  link_when;
    RB_ENTRY(svx_looper_timer)  link_id;
} svx_looper_timer_t;
/* use when_ms as key */
static __inline__ int svx_looper_timer_cmp_when(svx_looper_timer_t *a, svx_looper_timer_t *b)
{
    if     (a->when_ms > b->when_ms) return 1;
    else if(a->when_ms < b->when_ms) return -1;
    else if(a > b)                   return 1;
    else if(a < b)                   return -1;
    else                             return 0;
}
typedef RB_HEAD(svx_looper_timer_tree_when, svx_looper_timer) svx_looper_timer_tree_when_t;
RB_GENERATE_STATIC(svx_looper_timer_tree_when, svx_looper_timer, link_when, svx_looper_timer_cmp_when);
/* use id as key */
static __inline__ int svx_looper_timer_cmp_id(svx_looper_timer_t *a, svx_looper_timer_t *b)
{
    return memcmp(&(a->id), &(b->id), sizeof(svx_looper_timer_id_t));
}
typedef RB_HEAD(svx_looper_timer_tree_id, svx_looper_timer) svx_looper_timer_tree_id_t;
RB_GENERATE_STATIC(svx_looper_timer_tree_id, svx_looper_timer, link_id, svx_looper_timer_cmp_id);

struct svx_looper
{
    volatile int                   looping;
    pthread_t                      looping_tid;

    svx_poller_t                  *poller;
    int                            poller_timeout_ms;
    svx_notifier_t                *poller_notifier;
    svx_channel_t                 *poller_notifier_channel;

    svx_channel_t                **event_active_channels;
    size_t                         event_active_channels_size;
    size_t                         event_active_channels_used;

    uint8_t                       *pending_buf;
    uint8_t                       *pending_buf_swap;
    size_t                         pending_buf_size;
    size_t                         pending_buf_size_swap;
    size_t                         pending_buf_used;
    pthread_mutex_t                pending_mutex;
    
    svx_looper_timer_tree_when_t   timer_tree_when;
    svx_looper_timer_tree_id_t     timer_tree_id;
    uint64_t                       timer_id_sequence_next;
    pthread_mutex_t                timer_id_sequence_next_mutex;
};

static void svx_looper_reset_timeout(svx_looper_t *self, svx_looper_timer_t *timer_min, int64_t now_ms)
{
    struct timeval now;

    if(NULL == timer_min) timer_min = RB_MIN(svx_looper_timer_tree_when, &(self->timer_tree_when));

    if(NULL != timer_min)
    {
        if(now_ms < 0)
        {
            gettimeofday(&now, NULL);
            now_ms = now.tv_sec * 1000 + now.tv_usec / 1000;
        }
        self->poller_timeout_ms = timer_min->when_ms - now_ms;
    }
    else
    {
        self->poller_timeout_ms = -1;
    }
}

static void svx_looper_handle_timers(svx_looper_t *self)
{
    struct timeval      now;
    int64_t             now_ms;
    svx_looper_timer_t *timer;
    svx_looper_func_t   timer_run;
    void               *timer_arg;

    gettimeofday(&now, NULL);
    now_ms = now.tv_sec * 1000 + now.tv_usec / 1000;

    while(1)
    {
        if(NULL == (timer = RB_MIN(svx_looper_timer_tree_when, &(self->timer_tree_when)))) break;
        if(timer->when_ms > now_ms) break;

        timer_run = timer->run;
        timer_arg = timer->arg;
        
        RB_REMOVE(svx_looper_timer_tree_when, &(self->timer_tree_when), timer);
        if(timer->interval_ms > 0)
        {
            timer->when_ms += timer->interval_ms;
            RB_INSERT(svx_looper_timer_tree_when, &(self->timer_tree_when), timer);
        }
        else
        {
            RB_REMOVE(svx_looper_timer_tree_id, &(self->timer_tree_id), timer);
            free(timer);
        }

        timer_run(timer_arg);
    }

    svx_looper_reset_timeout(self, timer, now_ms);
}

static void svx_looper_handle_pendings(svx_looper_t *self, int run_flag)
{
    uint8_t              *pending_buf;
    size_t                pending_buf_size;
    size_t                pending_buf_used = 0;
    svx_looper_pending_t *pending;
    void                 *arg_block;
    uint8_t              *cur;
    uint8_t              *end;

    if(0 == self->pending_buf_used) return;

    /* swap the pending task info */
    pthread_mutex_lock(&(self->pending_mutex));
    if(self->pending_buf_used > 0)
    {
        pending_buf                 = self->pending_buf;
        self->pending_buf           = self->pending_buf_swap;
        self->pending_buf_swap      = pending_buf;

        pending_buf_size            = self->pending_buf_size;
        self->pending_buf_size      = self->pending_buf_size_swap;
        self->pending_buf_size_swap = pending_buf_size;

        pending_buf_used            = self->pending_buf_used;
        self->pending_buf_used      = 0;
    }
    pthread_mutex_unlock(&(self->pending_mutex));

    if(0 == pending_buf_used) return;

    /* run/clean all pending task */
    cur = pending_buf;
    end = pending_buf + pending_buf_used;
    while(cur < end)
    {
        pending = (svx_looper_pending_t *)cur;
        arg_block = (pending->arg_block_size > 0 ? cur + sizeof(svx_looper_pending_t) : NULL);
        
        if(run_flag)            pending->run(arg_block);
        else if(pending->clean) pending->clean(arg_block);

        cur += (sizeof(svx_looper_pending_t) + pending->arg_block_size);
    }
}

static void svx_looper_handle_events(svx_looper_t *self)
{
    size_t          i;
    svx_channel_t **tmp;

    for(i = 0; i < self->event_active_channels_used; i++)
        svx_channel_handle_events(self->event_active_channels[i]);

    /* We used all of the active_channel space this time. 
       We should be ready for more active_channel space next time. */
    if(self->event_active_channels_used == self->event_active_channels_size)
    {
        if(NULL != (tmp = realloc(self->event_active_channels, sizeof(svx_channel_t *) * self->event_active_channels_size * 2)))
        {
            self->event_active_channels       = tmp;
            self->event_active_channels_size *= 2;
        }
    }
}

static void svx_looper_poller_notifier_read_callback(void *arg)
{
    svx_looper_t *self = (svx_looper_t *)arg;

    svx_notifier_recv(self->poller_notifier);
}

int svx_looper_create(svx_looper_t **self)
{
    int r  = 0;
    int fd = -1;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    
    if(NULL == (*self = malloc(sizeof(svx_looper_t)))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    (*self)->looping                    = 0;
    (*self)->looping_tid                = pthread_self();
    (*self)->poller                     = NULL;
    (*self)->poller_timeout_ms          = -1;
    (*self)->poller_notifier            = NULL;
    (*self)->poller_notifier_channel    = NULL;
    (*self)->event_active_channels      = NULL;
    (*self)->event_active_channels_size = SVX_LOOPER_EVENT_ACTIVE_CHANNELS_SIZE_INIT;
    (*self)->event_active_channels_used = 0;
    (*self)->pending_buf                = NULL;
    (*self)->pending_buf_swap           = NULL;
    (*self)->pending_buf_size           = SVX_LOOPER_PENDING_BUF_SIZE_INIT;
    (*self)->pending_buf_size_swap      = SVX_LOOPER_PENDING_BUF_SIZE_INIT;
    (*self)->pending_buf_used           = 0;
    RB_INIT(&((*self)->timer_tree_when));
    RB_INIT(&((*self)->timer_tree_id));
    (*self)->timer_id_sequence_next     = 0;

    if(0 != (r = svx_poller_create(&((*self)->poller)))) SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    if(0 != (r = svx_notifier_create(&((*self)->poller_notifier), &fd))) SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    if(0 != (r = svx_channel_create(&((*self)->poller_notifier_channel), *self, fd, SVX_CHANNEL_EVENT_READ))) SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    if(0 != (r = svx_channel_set_read_callback((*self)->poller_notifier_channel, svx_looper_poller_notifier_read_callback, *self))) SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    if(NULL == ((*self)->event_active_channels = malloc(sizeof(svx_channel_t *) * (*self)->event_active_channels_size))) SVX_LOG_ERRNO_GOTO_ERR(err, r = SVX_ERRNO_NOMEM, NULL);
    if(NULL == ((*self)->pending_buf = malloc((*self)->pending_buf_size))) SVX_LOG_ERRNO_GOTO_ERR(err, r = SVX_ERRNO_NOMEM, NULL);
    if(NULL == ((*self)->pending_buf_swap = malloc((*self)->pending_buf_size))) SVX_LOG_ERRNO_GOTO_ERR(err, r = SVX_ERRNO_NOMEM, NULL);
    pthread_mutex_init(&((*self)->pending_mutex), NULL);
    pthread_mutex_init(&((*self)->timer_id_sequence_next_mutex), NULL);
    return 0;
    
 err:
    if(*self)
    {
        if((*self)->poller_notifier_channel) if(0 != (r = svx_channel_destroy(&((*self)->poller_notifier_channel)))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
        if((*self)->poller_notifier)         if(0 != (r = svx_notifier_destroy(&((*self)->poller_notifier)))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
        if((*self)->poller)                  if(0 != (r = svx_poller_destroy(&((*self)->poller)))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
        if((*self)->event_active_channels)   free((*self)->event_active_channels);
        if((*self)->pending_buf)             free((*self)->pending_buf);
        if((*self)->pending_buf_swap)        free((*self)->pending_buf_swap);
        free(*self);
        *self = NULL;
    }
    return r;
}

int svx_looper_destroy(svx_looper_t **self)
{
    int r = 0;
    svx_looper_timer_t *timer = NULL, *timer_tmp = NULL;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    if(NULL == *self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "*self:%p\n", *self);

    /* clean() and free() all timers */
    RB_FOREACH_SAFE(timer, svx_looper_timer_tree_when, &((*self)->timer_tree_when), timer_tmp)
    {
        RB_REMOVE(svx_looper_timer_tree_when, &((*self)->timer_tree_when), timer);
        if(timer->clean) timer->clean(timer->arg);
        free(timer);
    }

    /* clean() all pending task */
    while((*self)->pending_buf_used > 0)
        svx_looper_handle_pendings(*self, 0);

    pthread_mutex_destroy(&((*self)->pending_mutex));
    pthread_mutex_destroy(&((*self)->timer_id_sequence_next_mutex));
    if(0 != (r = svx_channel_destroy(&((*self)->poller_notifier_channel)))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_notifier_destroy(&((*self)->poller_notifier)))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_poller_destroy(&((*self)->poller)))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    free((*self)->event_active_channels);
    free((*self)->pending_buf);
    free((*self)->pending_buf_swap);
    free(*self);
    *self = NULL;

    return r;
}

int svx_looper_init_channel(svx_looper_t *self, svx_channel_t *channel)
{
    int r = 0;

    if(NULL == self || NULL == channel) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, channel:%p\n", self, channel);

    if(0 != (r = svx_poller_init_channel(self->poller, channel))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    
    return 0;
}

int svx_looper_update_channel(svx_looper_t *self, svx_channel_t *channel)
{
    int r = 0;

    if(NULL == self || NULL == channel) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, channel:%p\n", self, channel);

    if(0 != (r = svx_poller_update_channel(self->poller, channel))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    
    return 0;
}

int svx_looper_loop(svx_looper_t *self)
{
    int r;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    self->looping = 1;
    self->looping_tid = pthread_self(); /* reset the looping thread's ID */

    while(self->looping)
    {
        /* poll */
        if(0 != (r = svx_poller_poll(self->poller,
                                     self->event_active_channels, 
                                     self->event_active_channels_size, 
                                     &(self->event_active_channels_used),
                                     self->poller_timeout_ms)))
            SVX_LOG_ERRNO_RETURN_ERR(r, "svx_poller_poll() failed\n");

        /* handle event task */
        if(self->event_active_channels_used > 0)
            svx_looper_handle_events(self);

        /* handle timer task */
        if(!RB_EMPTY(&(self->timer_tree_when)))
            svx_looper_handle_timers(self);

        /* handle pending task */
        if(self->pending_buf_used > 0)
            svx_looper_handle_pendings(self, 1);
    }

    /* give the last chance to run all pending task recursively */
    while(self->pending_buf_used > 0)
        svx_looper_handle_pendings(self, 1);

    return 0;
}

/* this is an async-signal-safe function */
int svx_looper_quit(svx_looper_t *self)
{
    if(NULL == self) return SVX_ERRNO_INVAL;

    self->looping = 0;
    svx_notifier_send(self->poller_notifier);

    return 0;
}

/* this is an async-signal-safe function */
int svx_looper_wakeup(svx_looper_t *self)
{
    if(NULL == self) return SVX_ERRNO_INVAL;

    svx_notifier_send(self->poller_notifier);

    return 0;
}

static uint64_t svx_looper_get_timer_seq(svx_looper_t *self)
{
    uint64_t seq;

    pthread_mutex_lock(&(self->timer_id_sequence_next_mutex));
    seq = self->timer_id_sequence_next++;
    pthread_mutex_unlock(&(self->timer_id_sequence_next_mutex));

    return seq;
}

static int svx_looper_run(svx_looper_t *self, svx_looper_func_t run, svx_looper_func_t clean, void *arg, 
                          int64_t when_ms, int64_t interval_ms, svx_looper_timer_id_t timer_id, int64_t now_ms);
SVX_LOOPER_GENERATE_RUN_8(svx_looper_run, svx_looper_t *, self, svx_looper_func_t, run, svx_looper_func_t, clean, void *, arg,
                          int64_t, when_ms, int64_t, interval_ms, svx_looper_timer_id_t, timer_id, int64_t, now_ms)
static int svx_looper_run(svx_looper_t *self, svx_looper_func_t run, svx_looper_func_t clean, void *arg, 
                          int64_t when_ms, int64_t interval_ms, svx_looper_timer_id_t timer_id, int64_t now_ms)
{
    svx_looper_timer_t *timer     = NULL;
    svx_looper_timer_t *timer_min = NULL;

    SVX_LOOPER_CHECK_DISPATCH_HELPER_8(self, svx_looper_run, self, run, clean, arg, when_ms, interval_ms, timer_id, now_ms);

    if(NULL == (timer = malloc(sizeof(svx_looper_timer_t)))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    timer->run         = run;
    timer->clean       = clean;
    timer->arg         = arg;
    timer->when_ms     = when_ms;
    timer->interval_ms = interval_ms;
    timer->id          = timer_id;

    timer_min = RB_MIN(svx_looper_timer_tree_when, &(self->timer_tree_when));

    RB_INSERT(svx_looper_timer_tree_when, &(self->timer_tree_when), timer);
    RB_INSERT(svx_looper_timer_tree_id, &(self->timer_tree_id), timer);
    
    if(NULL == timer_min || timer->when_ms < timer_min->when_ms)
        svx_looper_reset_timeout(self, timer_min, now_ms);

    return 0;
}

int svx_looper_run_at(svx_looper_t *self, svx_looper_func_t run, svx_looper_func_t clean, 
                      void *arg, int64_t when_ms, svx_looper_timer_id_t *timer_id)
{
    svx_looper_timer_id_t timer_id_internal;

    if(NULL == self || NULL == run || when_ms < 0)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, run:%p\n", self, run);

    timer_id_internal.create_time = time(NULL);
    timer_id_internal.sequence    = svx_looper_get_timer_seq(self);

    if(timer_id) *timer_id = timer_id_internal;

    return svx_looper_run(self, run, clean, arg, when_ms, 0, timer_id_internal, -1);
}

int svx_looper_run_after(svx_looper_t *self, svx_looper_func_t run, svx_looper_func_t clean, 
                         void *arg, int64_t delay_ms, svx_looper_timer_id_t *timer_id)
{
    svx_looper_timer_id_t timer_id_internal;
    struct timeval        now;
    int64_t               now_ms, when_ms;

    if(NULL == self || NULL == run || delay_ms < 0) 
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, run:%p, delay_ms:%d\n", self, run, delay_ms);

    gettimeofday(&now, NULL);
    now_ms = (int64_t)now.tv_sec * 1000 + now.tv_usec / 1000;
    when_ms = now_ms + delay_ms;

    timer_id_internal.create_time = now.tv_sec;
    timer_id_internal.sequence    = svx_looper_get_timer_seq(self);

    if(timer_id) *timer_id = timer_id_internal;

    return svx_looper_run(self, run, clean, arg, when_ms, 0, timer_id_internal, now_ms);
}

int svx_looper_run_every(svx_looper_t *self, svx_looper_func_t run, svx_looper_func_t clean, 
                         void *arg, int64_t delay_ms, int64_t interval_ms, svx_looper_timer_id_t *timer_id)
{
    svx_looper_timer_id_t timer_id_internal;
    struct timeval        now;
    int64_t               now_ms, when_ms;

    if(NULL == self || NULL == run || delay_ms < 0 || interval_ms <= 0)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, run:%p, delay_ms:%d, interval_ms:%d\n", 
                                 self, run, delay_ms, interval_ms);

    gettimeofday(&now, NULL);
    now_ms = (int64_t)now.tv_sec * 1000 + now.tv_usec / 1000;
    when_ms = now_ms + delay_ms;

    timer_id_internal.create_time = now.tv_sec;
    timer_id_internal.sequence    = svx_looper_get_timer_seq(self);

    if(timer_id) *timer_id = timer_id_internal;

    return svx_looper_run(self, run, clean, arg, when_ms, interval_ms, timer_id_internal, now_ms);
}

SVX_LOOPER_GENERATE_RUN_2(svx_looper_cancel, svx_looper_t *, self, svx_looper_timer_id_t, timer_id)
int svx_looper_cancel(svx_looper_t *self, svx_looper_timer_id_t timer_id)
{
    svx_looper_timer_t  timer_key = {.id = timer_id};
    svx_looper_timer_t *timer;
    svx_looper_timer_t *timer_min;
    
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_2(self, svx_looper_cancel, self, timer_id);

    if(NULL == (timer = RB_FIND(svx_looper_timer_tree_id, &(self->timer_tree_id), &timer_key))) return 0;

    timer_min = RB_MIN(svx_looper_timer_tree_when, &(self->timer_tree_when));

    RB_REMOVE(svx_looper_timer_tree_when, &(self->timer_tree_when), timer);
    RB_REMOVE(svx_looper_timer_tree_id, &(self->timer_tree_id), timer);
    
    if(timer == timer_min) svx_looper_reset_timeout(self, timer_min, -1);

    free(timer);
    return 0;
}

int svx_looper_is_loop_thread(svx_looper_t *self)
{
    if(NULL == self) return 0;

    return pthread_equal(self->looping_tid, pthread_self()) ? 1 : 0;
}

int svx_looper_dispatch(svx_looper_t *self, svx_looper_func_t run, svx_looper_func_t clean,
                        void *arg_block, size_t arg_block_size)
{
    int                   r                    = 0;
    void                 *new_pending_buf      = NULL;
    size_t                new_pending_buf_size = 0;
    size_t                new_pending_size     = sizeof(svx_looper_pending_t) + arg_block_size;
    svx_looper_pending_t  new_pending;

    if(NULL == self || NULL == run) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, run:%p\n", self, run);
    if((NULL == arg_block && arg_block_size > 0) || (NULL != arg_block && 0 == arg_block_size))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "arg_block:%p, arg_block_size:%zu\n", arg_block, arg_block_size);

    pthread_mutex_lock(&(self->pending_mutex));

    /* expand pending_buf */
    if(self->pending_buf_size - self->pending_buf_used < new_pending_size)
    {
        /* calculate new_pending_buf_size */
        new_pending_buf_size = self->pending_buf_size;
        do new_pending_buf_size *= 2;
        while(new_pending_buf_size - self->pending_buf_used < new_pending_size);

        /* realloc */
        if(NULL == (new_pending_buf = realloc(self->pending_buf, new_pending_buf_size)))
            SVX_LOG_ERRNO_GOTO_ERR(end, r = SVX_ERRNO_NOMEM, NULL);
        self->pending_buf      = new_pending_buf;
        self->pending_buf_size = new_pending_buf_size;
    }

    /* save new pending task and it's arguments */
    new_pending.run            = run;
    new_pending.clean          = clean;
    new_pending.arg_block_size = arg_block_size;
    memcpy(self->pending_buf + self->pending_buf_used, &new_pending, sizeof(svx_looper_pending_t));
    if(arg_block_size > 0)
        memcpy(self->pending_buf + self->pending_buf_used + sizeof(svx_looper_pending_t), arg_block, arg_block_size);

    self->pending_buf_used += new_pending_size;

 end:
    pthread_mutex_unlock(&(self->pending_mutex));

    svx_notifier_send(self->poller_notifier);
    return r;
}
