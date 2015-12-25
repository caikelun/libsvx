/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#ifndef SVX_POLLER_EPOLL_H
#define SVX_POLLER_EPOLL_H 1

#include "svx_auto_config.h"
#if SVX_HAVE_EPOLL

#include <stdint.h>
#include <sys/types.h>
#include "svx_channel.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int svx_poller_epoll_create(void **self);
extern int svx_poller_epoll_init_channel(void *self, svx_channel_t *channel);
extern int svx_poller_epoll_update_channel(void *self, svx_channel_t *channel);
extern int svx_poller_epoll_poll(void *self, svx_channel_t **active_channels, size_t active_channels_size,
                                 size_t *active_channels_used, int timeout_ms);
extern int svx_poller_epoll_destroy(void **self);

#ifdef __cplusplus
}
#endif

#endif

#endif
