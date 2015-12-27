/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "svx_auto_config.h"
#include "svx_notifier.h"
#include "svx_errno.h"
#include "svx_log.h"
#include "svx_util.h"

#if SVX_HAVE_EVENTFD
#include <sys/eventfd.h>
#endif

struct svx_notifier
{
#if SVX_HAVE_EVENTFD
    int evfd; /* eventfd */
#else
    int pipefds[2]; /* pipefds[0] for read, pipefds[1] for write */
#endif
};

int svx_notifier_create(svx_notifier_t **self, int *fd)
{
    if(NULL == self || NULL == fd) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, fd:%p\n", self, fd);

    if(NULL == (*self = malloc(sizeof(svx_notifier_t)))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);

#if SVX_HAVE_EVENTFD
    /* do not use the EFD_SEMAPHORE flag */
    if(0 > ((*self)->evfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)))
    {
        free(*self);
        *self = NULL;
        SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    }
    *fd = (*self)->evfd;
#else
    int r;
    if(0 != pipe((*self)->pipefds))
    {
        free(*self);
        *self = NULL;
        SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    }
    if(0 != (r = svx_util_set_nonblocking((*self)->pipefds[0])) ||
       0 != (r = svx_util_set_nonblocking((*self)->pipefds[1])))
    {
        SVX_LOG_ERRNO_ERR(r, "set fd to nonblocking failed\n");
        if(0 != close((*self)->pipefds[0])) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
        if(0 != close((*self)->pipefds[1])) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
        free(*self);
        *self = NULL;
        return r;
    }
    *fd = (*self)->pipefds[0];
#endif

    return 0;
}

int svx_notifier_destroy(svx_notifier_t **self)
{
    if(NULL == self)  SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    if(NULL == *self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "*self:%p\n", *self);

#if SVX_HAVE_EVENTFD
    if(0 != close((*self)->evfd)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
#else
    if(0 != close((*self)->pipefds[0])) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(0 != close((*self)->pipefds[1])) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
#endif

    free(*self);
    *self = NULL;
    return 0;
}

int svx_notifier_send(svx_notifier_t *self)
{
    ssize_t n;
    
    if(NULL == self) return SVX_ERRNO_INVAL;

#if SVX_HAVE_EVENTFD
    uint64_t data = 1;
    do
        n = write(self->evfd, &data, sizeof(uint64_t));
    while(-1 == n && EINTR == errno);
#else
    uint8_t data = 1;
    do
        n = write(self->pipefds[1], &data, sizeof(uint8_t));
    while(-1 == n && EINTR == errno);
#endif

    if(n < 0 && 0 != errno && EAGAIN != errno) return errno;

    return 0;
}

int svx_notifier_recv(svx_notifier_t *self)
{
    ssize_t n;
    
    if(NULL == self) return SVX_ERRNO_INVAL;

#if SVX_HAVE_EVENTFD
    uint64_t buf = 0;
    do
        n = read(self->evfd, &buf, sizeof(buf));
    while(-1 == n && EINTR == errno);
#else
    uint8_t buf[64];
    do
        n = read(self->pipefds[0], buf, sizeof(buf));
    while(n > 0 || (-1 == n && EINTR == errno));
#endif

    return 0;
}
