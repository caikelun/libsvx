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
#include "svx_channel.h"
#include "svx_looper.h"
#include "svx_log.h"
#include "svx_errno.h"

struct svx_channel
{
    svx_looper_t           *looper;
    int                     fd;
    intmax_t                poller_data;
    uint8_t                 events;
    uint8_t                 revents;
    svx_channel_callback_t  read_cb;
    void                   *read_cb_arg;
    svx_channel_callback_t  write_cb;
    void                   *write_cb_arg;
};

int svx_channel_create(svx_channel_t **self, svx_looper_t *looper, int fd, uint8_t events)
{
    int r = 0;

    if(NULL == self || fd < 0 || NULL == looper || SVX_CHANNEL_EVENT_NULL != (events & ~SVX_CHANNEL_EVENT_ALL))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, looper:%p, fd:%d, events:%"PRIu8"\n", self, looper, fd, events);

    if(NULL == (*self = malloc(sizeof(svx_channel_t)))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    (*self)->looper       = looper;
    (*self)->fd           = fd;
    (*self)->poller_data  = 0;
    (*self)->events       = events;
    (*self)->revents      = SVX_CHANNEL_EVENT_NULL;
    (*self)->read_cb      = NULL;
    (*self)->read_cb_arg  = NULL;
    (*self)->write_cb     = NULL;
    (*self)->write_cb_arg = NULL;

    if(0 != (r = svx_looper_init_channel(looper, *self))) SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    if(SVX_CHANNEL_EVENT_NULL != events)
        if(0 != (r = svx_looper_update_channel(looper, *self))) SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    return 0;

 err:
    if(*self)
    {
        free(*self);
        *self = NULL;
    }
    return r;
}

int svx_channel_destroy(svx_channel_t **self)
{
    int r;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    if(NULL == *self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "*self:%p\n", *self);

    if(0 != (r = svx_channel_del_events(*self, SVX_CHANNEL_EVENT_ALL))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    free(*self);
    *self = NULL;
    
    return 0;
}

int svx_channel_add_events(svx_channel_t *self, uint8_t events)
{
    int r = 0;

    if(NULL == self || SVX_CHANNEL_EVENT_NULL == events || SVX_CHANNEL_EVENT_NULL != (events & ~SVX_CHANNEL_EVENT_ALL))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, events:%"PRIu8"\n", self, events);

    self->events |= events;
    if(0 != (r = svx_looper_update_channel(self->looper, self))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    
    return 0;
}

int svx_channel_del_events(svx_channel_t *self, uint8_t events)
{
    int r = 0;

    if(NULL == self || SVX_CHANNEL_EVENT_NULL == events || SVX_CHANNEL_EVENT_NULL != (events & ~SVX_CHANNEL_EVENT_ALL))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, events:%"PRIu8"\n", self, events);
    
    self->events &= ~events;
    if(0 != (r = svx_looper_update_channel(self->looper, self))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    
    return 0;
}

int svx_channel_set_read_callback(svx_channel_t *self, svx_channel_callback_t cb, void *cb_arg)
{
    if(NULL == self || NULL == cb) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, cb:%p\n", self, cb);

    self->read_cb     = cb;
    self->read_cb_arg = cb_arg;

    return 0;
}

int svx_channel_set_write_callback(svx_channel_t *self, svx_channel_callback_t cb, void *cb_arg)
{
    if(NULL == self || NULL == cb) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, cb:%p\n", self, cb);

    self->write_cb     = cb;
    self->write_cb_arg = cb_arg;

    return 0;
}

int svx_channel_set_revents(svx_channel_t *self, uint8_t revents)
{
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    self->revents = revents;

    return 0;
}

int svx_channel_set_poller_data(svx_channel_t *self, intmax_t poller_data)
{
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    self->poller_data = poller_data;

    return 0;
}

int svx_channel_get_looper(svx_channel_t *self, svx_looper_t **looper)
{
    if(NULL == self || NULL == looper)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, looper:%p\n", self, looper);

    *looper = self->looper;

    return 0;
}

int svx_channel_get_fd(svx_channel_t *self, int *fd)
{
    if(NULL == self || NULL == fd) 
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, fd:%p\n", self, fd);

    *fd = self->fd;

    return 0;
}

int svx_channel_get_events(svx_channel_t *self, uint8_t *events)
{
    if(NULL == self || NULL == events) 
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, events:%p\n", self, events);

    *events = self->events;

    return 0;
}

int svx_channel_get_poller_data(svx_channel_t *self, intmax_t *poller_data)
{
    if(NULL == self || NULL == poller_data)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, poller_data:%p\n", self, poller_data);

    *poller_data = self->poller_data;

    return 0;
}

int svx_channel_handle_events(svx_channel_t *self)
{
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    
    if((self->revents & SVX_CHANNEL_EVENT_READ) && self->read_cb)   self->read_cb(self->read_cb_arg);
    if((self->revents & SVX_CHANNEL_EVENT_WRITE) && self->write_cb) self->write_cb(self->write_cb_arg);

    return 0;
}
