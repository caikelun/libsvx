/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "svx_tcp_acceptor.h"
#include "svx_inetaddr.h"
#include "svx_channel.h"
#include "svx_util.h"
#include "svx_errno.h"
#include "svx_log.h"

struct svx_tcp_acceptor
{
    svx_looper_t                         *looper;
    svx_inetaddr_t                       *listen_addr;
    int                                   listen_fd;
    svx_channel_t                        *listen_channel;
    int                                   idle_fd;
    svx_tcp_acceptor_accepted_callback_t  accepted_cb;
    void                                 *accepted_cb_arg;
};

static void svx_tcp_acceptor_handle_read(void *arg)
{
    svx_tcp_acceptor_t *self = (svx_tcp_acceptor_t *)arg;
    int                 conn_fd;
    int                 r;

    while(1)
    {
        if(0 > (conn_fd = accept(self->listen_fd, NULL, NULL)))
        {
            /* fail */
            switch(errno)
            {
            case EAGAIN:
            case EINTR:
            case ECONNABORTED:
            case EPROTO:
                break;
            case ENFILE:
            case EMFILE:
                close(self->idle_fd);
                self->idle_fd = accept(self->listen_fd, NULL, NULL);
                close(self->idle_fd);
                self->idle_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
                break;
            default:
                SVX_LOG_ERRNO_ERR(errno, "accept() failed. listen_fd:%d\n", self->listen_fd);
                break;
            }
            break;
        }
        else
        {
            /* success*/ 
            if(0 != (r = svx_util_set_nonblocking(conn_fd)))
            {
                SVX_LOG_ERRNO_ERR(r, "set non-blocking failed. listen_fd:%d, conn_fd:%d\n", self->listen_fd, conn_fd);
                close(conn_fd);
                break;
            }
            self->accepted_cb(conn_fd, self->accepted_cb_arg);
        }
    }
}

int svx_tcp_acceptor_create(svx_tcp_acceptor_t **self, svx_looper_t *looper, svx_inetaddr_t *listen_addr,
                            svx_tcp_acceptor_accepted_callback_t accepted_cb, void *accepted_cb_arg)
{
    int r = 0;

    if(NULL == self || NULL == looper || NULL == accepted_cb)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, looper:%p, accepted_cb:%p\n", self, looper, accepted_cb);

    if(NULL == (*self = malloc(sizeof(svx_tcp_acceptor_t)))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    (*self)->looper          = looper;
    (*self)->listen_addr     = listen_addr;
    (*self)->listen_fd       = -1;
    (*self)->listen_channel  = NULL;
    (*self)->idle_fd         = -1;
    (*self)->accepted_cb     = accepted_cb;
    (*self)->accepted_cb_arg = accepted_cb_arg;

    if(0 > ((*self)->idle_fd = open("/dev/null", O_RDONLY | O_CLOEXEC)))
    {
        r = errno;
        free(*self);
        *self = NULL;
        SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    }

    return 0;
}

int svx_tcp_acceptor_destroy(svx_tcp_acceptor_t **self)
{
    if(NULL == self)  SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    if(NULL == *self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "*self:%p\n", *self);

    svx_tcp_acceptor_stop(*self);
    if((*self)->idle_fd >= 0) close((*self)->idle_fd);
    free(*self);
    *self = NULL;

    return 0;
}

int svx_tcp_acceptor_start(svx_tcp_acceptor_t *self, int if_reuse_port)
{
    const int on  = 1;
    const int off = 0;
    int       r   = 0;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    if(NULL != self->listen_channel || self->listen_fd >= 0) svx_tcp_acceptor_stop(self);

    if(0 > (self->listen_fd = socket(self->listen_addr->storage.addr.sa_family, SOCK_STREAM, IPPROTO_TCP)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);

    if(0 != (r = svx_util_set_nonblocking(self->listen_fd)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    if(0 != setsockopt(self->listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);

#ifdef SO_REUSEPORT
    if_reuse_port = if_reuse_port ? 1 : 0;
    if(0 != setsockopt(self->listen_fd, SOL_SOCKET, SO_REUSEPORT, &if_reuse_port, sizeof(if_reuse_port)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);
#endif

    /* always allow IPv4-mapped IPv6 addresses */
    if(SVX_INETADDR_IS_IPV6(self->listen_addr))
        if(0 != setsockopt(self->listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off)))
            SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);

    if(0 != bind(self->listen_fd, &(self->listen_addr->storage.addr), SVX_INETADDR_LEN(self->listen_addr)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);

    if(0 != listen(self->listen_fd, SOMAXCONN))
        SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);
    
    if(0 != (r = svx_channel_create(&(self->listen_channel), self->looper, self->listen_fd, SVX_CHANNEL_EVENT_READ)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    
    if(0 != (r = svx_channel_set_read_callback(self->listen_channel, svx_tcp_acceptor_handle_read, self)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    return 0;

 err:
    svx_tcp_acceptor_stop(self);

    return r;
}

int svx_tcp_acceptor_stop(svx_tcp_acceptor_t *self)
{
    int r;
    
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    
    if(NULL != self->listen_channel)
        if(0 != (r = svx_channel_destroy(&(self->listen_channel)))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    if(self->listen_fd >= 0)
    {
        close(self->listen_fd);
        self->listen_fd = -1;
    }

    return 0;
}
