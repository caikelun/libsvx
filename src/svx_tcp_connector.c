/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "svx_tcp_connector.h"
#include "svx_inetaddr.h"
#include "svx_channel.h"
#include "svx_util.h"
#include "svx_errno.h"
#include "svx_log.h"

typedef enum
{
    SVX_TCP_CONNECTOR_STATE_DISCONNECTED = 0,
    SVX_TCP_CONNECTOR_STATE_CONNECTING,
    SVX_TCP_CONNECTOR_STATE_CONNECTED
} svx_tcp_connector_state_t;

struct svx_tcp_connector
{
    /* const */
    svx_looper_t                           *looper;
    svx_inetaddr_t                         *server_addr; /* connect() to this address */
    svx_inetaddr_t                         *client_addr; /* bind() to this address */
    int64_t                                 init_delay_ms;
    int64_t                                 max_delay_ms;
    svx_tcp_connector_connected_callback_t  connected_cb;
    void                                   *connected_cb_arg;

    /* variable, always useful */
    int                                     working;

    /* variable, only useful while connecting */
    svx_tcp_connector_state_t               state;
    int                                     fd;
    svx_channel_t                          *channel;
    int64_t                                 cur_delay_ms;
    svx_looper_timer_id_t                   retry_timer_id;
};

static int svx_tcp_connector_retry(svx_tcp_connector_t *self);

static void svx_tcp_connector_reset(svx_tcp_connector_t *self)
{
    int r;
    
    self->working      = 0;
    self->state        = SVX_TCP_CONNECTOR_STATE_DISCONNECTED;
    self->cur_delay_ms = self->init_delay_ms;
    SVX_LOOPER_TIMER_ID_INIT(&(self->retry_timer_id));
    if(self->channel)
        if(0 != (r = svx_channel_destroy(&(self->channel)))) SVX_LOG_ERRNO_ERR(r, NULL);
    if(self->fd >= 0)
    {
        close(self->fd);
        self->fd = -1;
    }
}

int svx_tcp_connector_create(svx_tcp_connector_t **self, svx_looper_t *looper,
                             svx_inetaddr_t *server_addr, svx_inetaddr_t *client_addr,
                             int64_t init_delay_ms, int64_t max_delay_ms,
                             svx_tcp_connector_connected_callback_t connected_cb, void *connected_cb_arg)
{
    if(NULL == self || NULL == looper || NULL == connected_cb || init_delay_ms < 0 || max_delay_ms < 0 || init_delay_ms > max_delay_ms)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, looper:%p, connected_cb:%p, init_delay_ms:%"PRId64", max_delay_ms:%"PRId64"\n",
                                 self, looper, connected_cb, init_delay_ms, max_delay_ms);

    if(NULL == (*self = malloc(sizeof(svx_tcp_connector_t)))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    (*self)->looper           = looper;
    (*self)->server_addr      = server_addr;
    (*self)->client_addr      = client_addr;
    (*self)->init_delay_ms    = init_delay_ms;
    (*self)->max_delay_ms     = max_delay_ms;
    (*self)->connected_cb     = connected_cb;
    (*self)->connected_cb_arg = connected_cb_arg;
    (*self)->working          = 0;
    (*self)->state            = SVX_TCP_CONNECTOR_STATE_DISCONNECTED;
    (*self)->fd               = -1;
    (*self)->channel          = NULL;
    (*self)->cur_delay_ms     = init_delay_ms;
    SVX_LOOPER_TIMER_ID_INIT(&((*self)->retry_timer_id));

    return 0;
}

int svx_tcp_connector_destroy(svx_tcp_connector_t **self)
{
    if(NULL == self)  SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    if(NULL == *self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "*self:%p\n", *self);

    if(!SVX_LOOPER_TIMER_ID_IS_INITIALIZER(&((*self)->retry_timer_id)))
        svx_looper_cancel((*self)->looper, (*self)->retry_timer_id);
    
    svx_tcp_connector_reset(*self);
    free(*self);
    *self = NULL;

    return 0;
}

int svx_tcp_connector_set_client_addr(svx_tcp_connector_t *self, svx_inetaddr_t *client_addr)
{
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    self->client_addr = client_addr;
    
    return 0;
}

int svx_tcp_connector_set_retry_delay_ms(svx_tcp_connector_t *self, int64_t init_delay_ms, int64_t max_delay_ms)
{
    if(NULL == self || init_delay_ms < 0 || max_delay_ms < 0 || init_delay_ms > max_delay_ms)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, init_delay_ms:%"PRId64", max_delay_ms:%"PRId64"\n",
                                 self, init_delay_ms, max_delay_ms);

    self->init_delay_ms = init_delay_ms;
    self->max_delay_ms  = max_delay_ms;

    return 0;
}

static void svx_tcp_connector_handle_write(void *arg)
{
    svx_tcp_connector_t *self = (svx_tcp_connector_t *)arg;
    int                  sock_err;
    socklen_t            sock_err_len = (socklen_t)(sizeof(sock_err));
    svx_inetaddr_t       client_addr;
    int                  fd = self->fd;
    int                  r = 0;

    /* check if we are still working && connecting */
    if(0 == self->working || SVX_TCP_CONNECTOR_STATE_CONNECTING != self->state)
    {
        svx_tcp_connector_reset(self);
        return;
    }

    /* always destroy channel here */
    if(self->channel) if(0 != (r = svx_channel_destroy(&(self->channel)))) SVX_LOG_ERRNO_ERR(r, NULL);

    /* check fd */
    if(self->fd < 0)
        SVX_LOG_ERRNO_GOTO_ERR(retry, SVX_ERRNO_UNKNOWN, NULL);

    /* check socket error */
    if(0 > getsockopt(self->fd, SOL_SOCKET, SO_ERROR, &sock_err, &sock_err_len))
        SVX_LOG_ERRNO_GOTO_ERR(retry, errno, NULL);
    if(0 != sock_err) goto retry;

    /* check TCP self-connection */
    if(0 != (r = svx_inetaddr_from_fd_local(&client_addr, self->fd)))
        SVX_LOG_ERRNO_GOTO_ERR(retry, r, NULL);
    if(0 != svx_inetaddr_cmp_addr(&client_addr, self->server_addr)) goto retry;

    /* the task has been successfully completed */
    self->state = SVX_TCP_CONNECTOR_STATE_CONNECTED;
    self->working = 0;
    self->fd = -1; /* clear the fd */
    self->connected_cb(fd, self->connected_cb_arg);
    return;

 retry:
    svx_tcp_connector_retry(self);
}

static int svx_tcp_connector_try(svx_tcp_connector_t *self)
{
    int       r  = 0;
    const int on = 1;

    /* create socket fd */
    if(0 > (self->fd = socket(self->server_addr->storage.addr.sa_family, SOCK_STREAM, IPPROTO_TCP)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);

    /* set fd to non-blocking */
    if(0 != (r = svx_util_set_nonblocking(self->fd)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    /* set SO_REUSEADDR (if we use a specified client port) */
    if(!SVX_INETADDR_IS_PORT_UNSPECIFIED(self->client_addr))
        if(0 != setsockopt(self->fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
            SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);

    /* bind to a specified client address (if we use a specified client IP or port) */
    if(!SVX_INETADDR_IS_IP_UNSPECIFIED(self->client_addr) || !SVX_INETADDR_IS_PORT_UNSPECIFIED(self->client_addr))
        if(0 != bind(self->fd, &(self->client_addr->storage.addr), SVX_INETADDR_LEN(self->client_addr)))
            SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);

    /* do connect() */
    if(0 == connect(self->fd, &(self->server_addr->storage.addr), SVX_INETADDR_LEN(self->server_addr)))
        errno = 0;
    switch(errno)
    {
    case 0:
    case EINPROGRESS:
    case EINTR:
    case EISCONN:
        /* connecting */
        self->state = SVX_TCP_CONNECTOR_STATE_CONNECTING;
        if(0 != (r = svx_channel_create(&(self->channel), self->looper, self->fd, SVX_CHANNEL_EVENT_WRITE)))
            SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
        if(0 != (r = svx_channel_set_write_callback(self->channel, svx_tcp_connector_handle_write, self)))
            SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
        break;
    case EAGAIN:
    case EADDRINUSE:
    case EADDRNOTAVAIL:
    case ECONNREFUSED:
    case ENETUNREACH:
    case ETIMEDOUT:
    case ENOBUFS:
        /* some temporary problem, retry */
        if(0 != (r = svx_tcp_connector_retry(self))) SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
        break;
    case EACCES:
    case EPERM:
    case EPROTOTYPE:
    case EAFNOSUPPORT:
    case EALREADY:
    case EBADF:
    case EFAULT:
    case ENOTSOCK:
    default:
        /* some unrecoverable problem, failed */
        SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);
    }

    return 0;

 err:
    svx_tcp_connector_reset(self);
    return r;
}

static void svx_tcp_connector_retry_func(void *arg)
{
    svx_tcp_connector_t *self = (svx_tcp_connector_t *)arg;

    svx_tcp_connector_try(self);
}

static int svx_tcp_connector_retry(svx_tcp_connector_t *self)
{
    int r = 0;

    /* check if we are still working*/
    if(0 == self->working)
    {
        svx_tcp_connector_reset(self);
        return 0;
    }

    /* clear */
    self->state = SVX_TCP_CONNECTOR_STATE_DISCONNECTED;
    SVX_LOOPER_TIMER_ID_INIT(&(self->retry_timer_id));
    if(self->channel)
        if(0 != (r = svx_channel_destroy(&(self->channel)))) SVX_LOG_ERRNO_ERR(r, NULL);
    if(self->fd >= 0)
    {
        close(self->fd);
        self->fd = -1;
    }
    
    /* retry with a delay */
    if(0 != (r = svx_looper_run_after(self->looper, svx_tcp_connector_retry_func, NULL, self,
                                      self->cur_delay_ms, &(self->retry_timer_id))))
        SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    /* for next delay */
    if(self->cur_delay_ms < self->max_delay_ms)
    {
        self->cur_delay_ms *= 2;
        if(self->cur_delay_ms > self->max_delay_ms)
            self->cur_delay_ms = self->max_delay_ms;
    }

    return 0;
}

int svx_tcp_connector_connect(svx_tcp_connector_t *self)
{
    int r = 0;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    if(1 == self->working) SVX_LOG_ERRNO_RETURN_WARNING(SVX_ERRNO_INPROGRESS, NULL);

    svx_tcp_connector_reset(self);
    self->working = 1;

    if(0 != (r = svx_tcp_connector_try(self))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    return 0;
}

int svx_tcp_connector_cancel(svx_tcp_connector_t *self)
{
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    if(0 == self->working) SVX_LOG_ERRNO_RETURN_WARNING(SVX_ERRNO_NOTRUN, NULL);

    svx_looper_cancel(self->looper, self->retry_timer_id);
    svx_tcp_connector_reset(self);
    return 0;
}
