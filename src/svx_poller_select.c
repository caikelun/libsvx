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
#include <string.h>
#include <sys/time.h>
#include <sys/select.h>
#include "svx_poller.h"
#include "svx_poller_select.h"
#include "svx_errno.h"
#include "svx_log.h"
#include "svx_util.h"

typedef struct
{
    int            maxfd;
    fd_set         fdset_read;
    fd_set         fdset_write;
    fd_set         fdset_read_bak;
    fd_set         fdset_write_bak;
    svx_channel_t *data_map[FD_SETSIZE];
} svx_poller_select_t;

int svx_poller_select_create(void **self)
{
    svx_poller_select_t *obj = NULL;

    if(NULL == (obj = malloc(sizeof(svx_poller_select_t)))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    obj->maxfd = 0;
    FD_ZERO(&(obj->fdset_read));
    FD_ZERO(&(obj->fdset_write));
    FD_ZERO(&(obj->fdset_read_bak));
    FD_ZERO(&(obj->fdset_write_bak));
    memset(obj->data_map, 0, sizeof(obj->data_map));
    
    *self = (void *)obj;
    return 0;
}

int svx_poller_select_init_channel(void *self, svx_channel_t *channel)
{
    SVX_UTIL_UNUSED(self);
    SVX_UTIL_UNUSED(channel);

    /* init nothing */
    return 0;
}

int svx_poller_select_update_channel(void *self, svx_channel_t *channel)
{
    svx_poller_select_t *obj        = (svx_poller_select_t *)self;
    int                  fd         = -1;
    uint8_t              events_new = 0;
    int                  r          = 0;

    if(0 != (r = svx_channel_get_fd(channel, &fd))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_channel_get_events(channel, &events_new))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    if(fd < 0 || fd >= FD_SETSIZE) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d\n", fd);

    FD_CLR(fd, &(obj->fdset_read_bak));
    FD_CLR(fd, &(obj->fdset_write_bak));
    if(events_new & SVX_CHANNEL_EVENT_READ)  FD_SET(fd, &(obj->fdset_read_bak));
    if(events_new & SVX_CHANNEL_EVENT_WRITE) FD_SET(fd, &(obj->fdset_write_bak));

    if(FD_ISSET(fd, &(obj->fdset_read_bak)) || FD_ISSET(fd, &(obj->fdset_write_bak)))
    {
        /* add or modify */
        obj->data_map[fd] = channel;
        if(fd > obj->maxfd) obj->maxfd = fd;
    }    
    else
    {
        /* remove */
        obj->data_map[fd] = NULL;
        if(obj->maxfd == fd)
        {
            while(fd >= 0)
            {
                if(FD_ISSET(fd, &(obj->fdset_read_bak)) || FD_ISSET(fd, &(obj->fdset_write_bak))) break;
                fd--;
            }
            obj->maxfd = (fd >= 0 ? fd : 0);
        }
    }

    return 0;
}

int svx_poller_select_poll(void *self, svx_channel_t **active_channels, size_t active_channels_size, 
                           size_t *active_channels_used, int timeout_ms)
{
    svx_poller_select_t *obj     = (svx_poller_select_t *)self;
    struct timeval      *timeout = NULL;
    struct timeval       tv;
    uint8_t              revents = 0;
    int                  nfds    = 0;
    int                  fd      = 0;
    size_t               cnt     = 0;

    timerclear(&tv);
    *active_channels_used = 0;

    if(timeout_ms < 0)
    {
        timeout = NULL;
    }
    else if(0 == timeout_ms)
    {
        tv.tv_sec  = 0;
        tv.tv_usec = 0;
        timeout = &tv;
    }
    else
    {
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        timeout    = &tv;
    }

    memcpy(&(obj->fdset_read),  &(obj->fdset_read_bak),  sizeof(fd_set));
    memcpy(&(obj->fdset_write), &(obj->fdset_write_bak), sizeof(fd_set));
    
    if((nfds = select(obj->maxfd + 1, &(obj->fdset_read), &(obj->fdset_write), NULL, timeout)) < 0)
    {
        if(EINTR == errno) return 0;
        else SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    }

    for(fd = 0, cnt = 0; nfds > 0 && fd <= obj->maxfd && cnt < active_channels_size; fd++)
    {
        revents = SVX_CHANNEL_EVENT_NULL;
        if(FD_ISSET(fd, &(obj->fdset_read)))  revents |= SVX_CHANNEL_EVENT_READ;
        if(FD_ISSET(fd, &(obj->fdset_write))) revents |= SVX_CHANNEL_EVENT_WRITE;
        if(SVX_CHANNEL_EVENT_NULL != revents)
        {
            active_channels[cnt] = obj->data_map[fd];
            svx_channel_set_revents(active_channels[cnt], revents);
            cnt++;
            nfds--;
        }
    }
    *active_channels_used = cnt;
    
    return 0;
}

int svx_poller_select_destroy(void **self)
{
    svx_poller_select_t *obj = (svx_poller_select_t *)(*self);

    free(obj);
    *self = NULL;
    return 0;
}

const svx_poller_handlers_t svx_poller_select_handlers = {
    svx_poller_select_create,
    svx_poller_select_init_channel,
    svx_poller_select_update_channel,
    svx_poller_select_poll,
    svx_poller_select_destroy
};
