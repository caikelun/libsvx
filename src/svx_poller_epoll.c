/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#include "svx_auto_config.h"
#if SVX_HAVE_EPOLL

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/epoll.h>
#include "svx_poller.h"
#include "svx_poller_epoll.h"
#include "svx_errno.h"
#include "svx_log.h"
#include "svx_inetaddr.h"
#include "svx_util.h"

#define SVX_POLLER_EPOLL_EVENTS_SIZE_INIT 16

typedef struct
{
    int                 epfd;
    struct epoll_event *events;
    int                 events_size;
} svx_poller_epoll_t;

int svx_poller_epoll_create(void **self)
{
    svx_poller_epoll_t *obj = NULL;
    int                 r   = 0;

    if(NULL == (obj = malloc(sizeof(svx_poller_epoll_t)))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    obj->epfd        = -1;
    obj->events      = NULL;
    obj->events_size = SVX_POLLER_EPOLL_EVENTS_SIZE_INIT;

    if((obj->epfd = epoll_create(obj->events_size)) < 0) SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);
    if(NULL == (obj->events = calloc(obj->events_size, sizeof(struct epoll_event))))
        SVX_LOG_ERRNO_GOTO_ERR(err, r = SVX_ERRNO_NOMEM, NULL);

    *self = (void *)obj;
    return 0;

 err:
    if(NULL != obj)
    {
        if(obj->epfd >= 0)      close(obj->epfd);
        if(NULL != obj->events) free(obj->events);
        free(obj);
    }
    *self = NULL;
    return r;
}

int svx_poller_epoll_init_channel(void *self, svx_channel_t *channel)
{
    SVX_UTIL_UNUSED(self);
    
    return svx_channel_set_poller_data(channel, (intmax_t)SVX_CHANNEL_EVENT_NULL);
}

int svx_poller_epoll_update_channel(void *self, svx_channel_t *channel)
{
    svx_poller_epoll_t *obj        = (svx_poller_epoll_t *)self;
    int                 fd         = -1;
    intmax_t            data       = 0;
    uint8_t             events_old = 0;
    uint8_t             events_new = 0;
    int                 op         = 0;
    struct epoll_event  event      = {.events = 0, .data.ptr = channel};
    int                 r          = 0;

    if(0 != (r = svx_channel_get_fd(channel, &fd))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_channel_get_events(channel, &events_new))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_channel_get_poller_data(channel, &data))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    events_old = (uint8_t)data;

    if(events_new == events_old) return 0;

    if(SVX_CHANNEL_EVENT_NULL == events_old)
        op = EPOLL_CTL_ADD;
    else if(SVX_CHANNEL_EVENT_NULL == events_new)
        op = EPOLL_CTL_DEL;
    else
        op = EPOLL_CTL_MOD;
    
    if(events_new & SVX_CHANNEL_EVENT_READ)  event.events |= EPOLLIN;
    if(events_new & SVX_CHANNEL_EVENT_WRITE) event.events |= EPOLLOUT;
    
    if(0 != epoll_ctl(obj->epfd, op, fd, &event)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

    if(0 != (r = svx_channel_set_poller_data(channel, (intmax_t)events_new)))
        SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    return 0;        
}

int svx_poller_epoll_poll(void *self, svx_channel_t **active_channels, size_t active_channels_size, 
                          size_t *active_channels_used, int timeout_ms)
{
    svx_poller_epoll_t *obj        = (svx_poller_epoll_t *)self;
    uint8_t             revents    = 0;
    int                 nfds       = 0;
    int                 i          = 0;
    struct epoll_event *new_events = NULL;

    *active_channels_used = 0;

    if((nfds = epoll_wait(obj->epfd, obj->events, obj->events_size, timeout_ms)) < 0)
    {
        if(EINTR == errno) return 0;
        else SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    }

    for(i = 0; i < nfds && i < (int)active_channels_size; i++)
    {
        active_channels[i] = (svx_channel_t *)obj->events[i].data.ptr;
        revents = SVX_CHANNEL_EVENT_NULL;

        if(obj->events[i].events & (EPOLLIN  | EPOLLERR | EPOLLHUP)) revents |= SVX_CHANNEL_EVENT_READ;
        if(obj->events[i].events & (EPOLLOUT | EPOLLERR | EPOLLHUP)) revents |= SVX_CHANNEL_EVENT_WRITE;
        svx_channel_set_revents(active_channels[i], revents);
    }
    *active_channels_used = i;
    
    if(nfds == obj->events_size)
    {
        /* We used all of the event space this time.  We should be ready for more events next time. */
        if(NULL != (new_events = realloc(obj->events, sizeof(struct epoll_event) * obj->events_size * 2)))
        {
            obj->events       = new_events;
            obj->events_size *= 2;
        }
    }
    
    return 0;
}

int svx_poller_epoll_destroy(void **self)
{
    svx_poller_epoll_t *obj = (svx_poller_epoll_t *)(*self);

    close(obj->epfd);
    free(obj->events);
    free(obj);
    *self = NULL;
    return 0;
}

const svx_poller_handlers_t svx_poller_epoll_handlers = {
    svx_poller_epoll_create,
    svx_poller_epoll_init_channel,
    svx_poller_epoll_update_channel,
    svx_poller_epoll_poll,
    svx_poller_epoll_destroy
};

#endif
