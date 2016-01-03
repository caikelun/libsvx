/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include "svx_poller.h"
#include "svx_poller_poll.h"
#include "svx_errno.h"
#include "svx_log.h"
#include "svx_tree.h"
#include "svx_util.h"

#define SVX_POLLER_POLL_EVENTS_SIZE_INIT 64

/* rbtree for fd to channel ptr */
typedef struct svx_poller_poll_data
{
    int            fd; /* key */
    svx_channel_t *channel;
    RB_ENTRY(svx_poller_poll_data) link;
} svx_poller_poll_data_t;
static __inline__ int svx_poller_poll_data_cmp(svx_poller_poll_data_t *a, svx_poller_poll_data_t *b)
{
    return (a->fd > b->fd) - (a->fd < b->fd);
}
typedef RB_HEAD(svx_poller_poll_data_tree, svx_poller_poll_data) svx_poller_poll_data_tree_t;
RB_GENERATE_STATIC(svx_poller_poll_data_tree, svx_poller_poll_data, link, svx_poller_poll_data_cmp);

typedef struct
{
    struct pollfd               *events;
    nfds_t                       events_size;
    nfds_t                       events_used;
    svx_poller_poll_data_tree_t  data_tree;
} svx_poller_poll_t;

int svx_poller_poll_create(void **self)
{
    svx_poller_poll_t *obj = NULL;
    size_t             i   = 0;

    if(NULL == (obj = malloc(sizeof(svx_poller_poll_t)))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    obj->events      = NULL;
    obj->events_size = SVX_POLLER_POLL_EVENTS_SIZE_INIT;
    obj->events_used = 0;
    RB_INIT(&(obj->data_tree));

    if(NULL == (obj->events = calloc(obj->events_size, sizeof(struct pollfd))))
    {
        SVX_LOG_ERRNO_ERR(SVX_ERRNO_NOMEM, NULL);
        free(obj);
        return SVX_ERRNO_NOMEM;
    }
    for(i = 0; i < obj->events_size; i++)
        obj->events[i].fd = -1;

    *self = (void *)obj;
    return 0;
}

int svx_poller_poll_init_channel(void *self, svx_channel_t *channel)
{
    SVX_UTIL_UNUSED(self);
    
    return svx_channel_set_poller_data(channel, (intmax_t)-1);
}

int svx_poller_poll_update_channel(void *self, svx_channel_t *channel)
{
    svx_poller_poll_t      *obj        = (svx_poller_poll_t *)self;
    int                     r          = 0;
    int                     fd         = -1;
    intmax_t                data       = 0;
    ssize_t                 idx        = -1;
    uint8_t                 events_new = 0;
    nfds_t                  i          = 0;
    struct pollfd          *new_events = NULL;
    svx_poller_poll_data_t *poll_data  = NULL;
    svx_poller_poll_data_t  poll_data_key;
    
    if(0 != (r = svx_channel_get_fd(channel, &fd))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_channel_get_events(channel, &events_new))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_channel_get_poller_data(channel, &data))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    idx = (ssize_t)data;

    if(idx < -1 || idx >= (ssize_t)(obj->events_used))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "idx:%zd, obj->events_used:%ju\n", idx, (uintmax_t)obj->events_used);

    if(-1 == idx) /* new pollfd */
    {
        /* search for a empty hole */
        for(i = 0; i < obj->events_size; i++)
        {
            if(-1 == obj->events[i].fd)
                break;
        }

        if(i >= obj->events_size)
        {
            /* no empty hole, expand the buffer */
            if(NULL == (new_events = realloc(obj->events, sizeof(struct pollfd) * obj->events_size * 2)))
                SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
            obj->events       = new_events;
            obj->events_size *= 2;
            for(i = obj->events_size / 2; i < obj->events_size; i++)
            {
                obj->events[i].fd      = -1;
                obj->events[i].events  = 0;
                obj->events[i].revents = 0;
            }
            idx = obj->events_size / 2;
        }
        else
        {
            /* found a empty hole */
            idx = i;
        }

        /* save new data to data_tree*/
        if(NULL == (poll_data = malloc(sizeof(svx_poller_poll_data_t))))
            SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
        poll_data->fd      = fd;
        poll_data->channel = channel;
        if(NULL != RB_INSERT(svx_poller_poll_data_tree, &(obj->data_tree), poll_data))
        {
            free(poll_data);
            SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_REPEAT, "colliding fd:%d\n", fd);
        }

        obj->events[idx].fd = fd;
        obj->events[idx].events = 0;
        obj->events[idx].revents = 0;

        /* update the events_used */
        if((nfds_t)(idx + 1) > obj->events_used)
            obj->events_used = idx + 1;

        /* save the idx */
        if(0 != (r = svx_channel_set_poller_data(channel, (intmax_t)idx))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    }
    else /* existing pollfd */
    {
        if(fd != obj->events[idx].fd) 
            SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d, obj->events[idx].fd:%d\n", fd, obj->events[idx].fd);
        
        obj->events[idx].events = 0;
    }

    if(events_new & SVX_CHANNEL_EVENT_READ)  obj->events[idx].events |= POLLIN;
    if(events_new & SVX_CHANNEL_EVENT_WRITE) obj->events[idx].events |= POLLOUT;
    
    if(0 == obj->events[idx].events) /* ignore the fd */
    {
        obj->events[idx].fd = -1;
        obj->events[idx].events = 0;
        obj->events[idx].revents = 0;
        if(0 != (r = svx_channel_set_poller_data(channel, (intmax_t)-1))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

        if((nfds_t)(idx + 1) == obj->events_used)
        {
            while(idx >= 0)
            {
                if(-1 == obj->events[idx].fd)
                {
                    obj->events_used--;
                    idx--;
                }
                else break;
            }
        }

        /* delete data from data_tree */
        poll_data_key.fd = fd;
        if(NULL != (poll_data = RB_FIND(svx_poller_poll_data_tree, &(obj->data_tree), &poll_data_key)))
        {
            RB_REMOVE(svx_poller_poll_data_tree, &(obj->data_tree), poll_data);
            free(poll_data);
        }
    }

    return 0;
}

int svx_poller_poll_poll(void *self, svx_channel_t **active_channels, size_t active_channels_size, 
                         size_t *active_channels_used, int timeout_ms)
{
    svx_poller_poll_t      *obj       = (svx_poller_poll_t *)self;
    uint8_t                 revents   = 0;
    int                     nfds      = 0;
    unsigned int            i         = 0;
    size_t                  cnt       = 0;
    svx_poller_poll_data_t *poll_data = NULL;
    svx_poller_poll_data_t  poll_data_key;

    *active_channels_used = 0;

    if((nfds = poll(obj->events, obj->events_used, timeout_ms)) < 0)
    {
        if(EINTR == errno) return 0;
        else SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    }

    for(i = 0, cnt = 0; nfds > 0 && i < obj->events_used && cnt < active_channels_size; i++)
    {
        if(obj->events[i].fd < 0) continue;
        if(0 == obj->events[i].revents) continue;

        /* find the channel ptr */
        poll_data_key.fd = obj->events[i].fd;
        if(NULL == (poll_data = RB_FIND(svx_poller_poll_data_tree, &(obj->data_tree), &poll_data_key)))
            SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTFND, "i:%u, fd:%d", i, obj->events[i].fd);

        active_channels[cnt] = poll_data->channel;
        revents = SVX_CHANNEL_EVENT_NULL;
        if(obj->events[i].revents & (POLLIN  | POLLERR | POLLHUP | POLLNVAL)) revents |= SVX_CHANNEL_EVENT_READ;
        if(obj->events[i].revents & (POLLOUT | POLLERR | POLLHUP | POLLNVAL)) revents |= SVX_CHANNEL_EVENT_WRITE;
        svx_channel_set_revents(active_channels[cnt], revents);

        cnt++;
        nfds--;
    }
    *active_channels_used = cnt;

    return 0;
}

int svx_poller_poll_destroy(void **self)
{
    svx_poller_poll_t      *obj           = (svx_poller_poll_t *)(*self);
    svx_poller_poll_data_t *poll_data     = NULL;
    svx_poller_poll_data_t *poll_data_tmp = NULL;

    RB_FOREACH_SAFE(poll_data, svx_poller_poll_data_tree, &(obj->data_tree), poll_data_tmp)
    {
        RB_REMOVE(svx_poller_poll_data_tree, &(obj->data_tree), poll_data);
        free(poll_data);
    }
    free(obj->events);
    free(obj);
    *self = NULL;
    return 0;
}

const svx_poller_handlers_t svx_poller_poll_handlers = {
    svx_poller_poll_create,
    svx_poller_poll_init_channel,
    svx_poller_poll_update_channel,
    svx_poller_poll_poll,
    svx_poller_poll_destroy
};
