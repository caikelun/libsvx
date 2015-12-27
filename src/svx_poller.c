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
#include "svx_auto_config.h"
#include "svx_poller.h"
#include "svx_errno.h"
#include "svx_log.h"

svx_poller_fixed_t svx_poller_fixed = SVX_POLLER_FIXED_NONE;

#if SVX_HAVE_EPOLL
extern const svx_poller_handlers_t svx_poller_epoll_handlers;
#endif
extern const svx_poller_handlers_t svx_poller_poll_handlers;
extern const svx_poller_handlers_t svx_poller_select_handlers;

static const svx_poller_handlers_t *svx_poller_handlers_array[] = 
{
#if SVX_HAVE_EPOLL
    &svx_poller_epoll_handlers,
#endif
    &svx_poller_poll_handlers,
    &svx_poller_select_handlers
};

struct svx_poller
{
    void                        *obj;
    const svx_poller_handlers_t *handlers;
};

int svx_poller_create(svx_poller_t **self)
{
    int                          r        = 0;
    const svx_poller_handlers_t *handlers = NULL;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    /* choose a poller */
    switch(svx_poller_fixed)
    {
    case SVX_POLLER_FIXED_EPOLL:
#if SVX_HAVE_EPOLL
        handlers = &svx_poller_epoll_handlers;
#else
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "You fixed poller to EPOLL, but the system does not support it.\n");
#endif
        break;
    case SVX_POLLER_FIXED_POLL:
        handlers = &svx_poller_poll_handlers;
        break;
    case SVX_POLLER_FIXED_SELECT:
        handlers = &svx_poller_select_handlers;
        break;
    case SVX_POLLER_FIXED_NONE:
    default:
        handlers = svx_poller_handlers_array[0];
        break;
    }

    if(NULL == (*self = malloc(sizeof(svx_poller_t)))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    (*self)->obj      = NULL;
    (*self)->handlers = handlers;

    if(0 != (r = (*self)->handlers->create(&((*self)->obj)))) SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    return 0;

 err:
    if(*self)
    {
        free(*self);
        *self = NULL;
    }
    return r;
}

int svx_poller_init_channel(svx_poller_t *self, svx_channel_t *channel)
{
    if(NULL == self || NULL == channel)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, channel:%p\n", self, channel);
        
    return self->handlers->init_channel(self->obj, channel);
}

int svx_poller_update_channel(svx_poller_t *self, svx_channel_t *channel)
{
    if(NULL == self || NULL == channel)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, channel:%p\n", self, channel);
        
    return self->handlers->update_channel(self->obj, channel);
}

int svx_poller_poll(svx_poller_t *self, svx_channel_t **active_channels, size_t active_channels_size, 
                    size_t *active_channels_used, int timeout_ms)
{
    if(NULL == self || NULL == active_channels || 0 == active_channels_size || NULL == active_channels_used || timeout_ms < -1)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, active_channels:%p, active_channels_size:%zu, active_channels_used:%p, timeout_ms:%d\n",
                                 self, active_channels, active_channels_size, active_channels_used, timeout_ms);
    
    return self->handlers->poll(self->obj, active_channels, active_channels_size, active_channels_used, timeout_ms);
}

int svx_poller_destroy(svx_poller_t **self)
{
    if(NULL == self)  SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    if(NULL == *self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "*self:%p\n", *self);

    (*self)->handlers->destroy(&((*self)->obj));
    free(*self);
    *self = NULL;

    return 0;
}
