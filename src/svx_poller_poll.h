/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#include <stdint.h>
#include <sys/types.h>
#include "svx_channel.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int svx_poller_poll_create(void **self);
extern int svx_poller_poll_init_channel(void *self, svx_channel_t *channel);
extern int svx_poller_poll_update_channel(void *self, svx_channel_t *channel);
extern int svx_poller_poll_poll(void *self, svx_channel_t **active_channels, size_t active_channels_size, 
                                size_t *active_channels_used, int timeout_ms);
extern int svx_poller_poll_destroy(void **self);

#ifdef __cplusplus
}
#endif
