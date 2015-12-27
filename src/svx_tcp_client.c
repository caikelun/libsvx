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
#include <inttypes.h>
#include "svx_tcp_client.h"
#include "svx_tcp_connector.h"
#include "svx_tcp_connection.h"
#include "svx_inetaddr.h"
#include "svx_looper.h"
#include "svx_util.h"
#include "svx_errno.h"
#include "svx_log.h"

#define SVX_TCP_CLIENT_DEFAULT_READ_BUF_MIN_LEN              128
#define SVX_TCP_CLIENT_DEFAULT_READ_BUF_MAX_LEN              (1 * 1024 * 1024)
#define SVX_TCP_CLIENT_DEFAULT_WRITE_BUF_MIN_LEN             128
#define SVX_TCP_CLIENT_DEFAULT_WRITE_BUF_HIGH_WATER_MARK     (4 * 1024 * 1024)
#define SVX_TCP_CLIENT_DEFAULT_CONNECTOR_RETRY_INIT_DELAY_MS 500
#define SVX_TCP_CLIENT_DEFAULT_CONNECTOR_RETRY_MAX_DELAY_MS  (10 * 1000)

struct svx_tcp_client
{
    svx_tcp_connection_t           *conn_ptr;
    svx_inetaddr_t                  server_addr;
    svx_inetaddr_t                  client_addr;
    svx_tcp_connector_t            *connector;
    svx_looper_t                   *looper;
    size_t                          read_buf_min_len;
    size_t                          read_buf_max_len;
    size_t                          write_buf_min_len;
    size_t                          write_buf_high_water_mark;
    svx_tcp_connection_callbacks_t  callbacks;
};

static void svx_tcp_client_handle_remove(svx_tcp_connection_t *conn, void *arg)
{
    svx_tcp_client_t *self = (svx_tcp_client_t *)arg;

    if(self->conn_ptr != conn) SVX_LOG_ERRNO_ERR(SVX_ERRNO_NOTFND, NULL);

    self->conn_ptr = NULL;
    svx_tcp_connection_del_ref(conn);
}

static void svx_tcp_client_handle_connected(int fd, void *arg)
{
    svx_tcp_client_t *self = (svx_tcp_client_t *)arg;
    int               r;

    /* already have a connection. why? */
    if(NULL != self->conn_ptr)
    {
        SVX_LOG_ERRNO_ERR(SVX_ERRNO_UNKNOWN, NULL);
        svx_tcp_connection_del_ref(self->conn_ptr);
        self->conn_ptr = NULL;
    }

    /* create connection */
    if(0 != (r = svx_tcp_connection_create(&(self->conn_ptr), self->looper, fd,
                                           self->read_buf_min_len, self->read_buf_max_len,
                                           self->write_buf_min_len, self->write_buf_high_water_mark,
                                           &(self->callbacks), svx_tcp_client_handle_remove, self, NULL)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    /* start connection */
    if(0 != (r = svx_tcp_connection_start(self->conn_ptr)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    return;

 err:
    if(fd >= 0) close(fd);
    if(NULL != self->conn_ptr) svx_tcp_connection_del_ref(self->conn_ptr);
    self->conn_ptr = NULL;
}

int svx_tcp_client_create(svx_tcp_client_t **self, svx_looper_t *looper, svx_inetaddr_t server_addr)
{
    int r;
    
    if(NULL == self || NULL == looper) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, looper:%p\n", self, looper);

    if(NULL == (*self = malloc(sizeof(svx_tcp_client_t)))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    (*self)->conn_ptr                  = NULL;
    (*self)->server_addr               = server_addr;
    (*self)->connector                 = NULL;
    (*self)->looper                    = looper;
    (*self)->read_buf_min_len          = SVX_TCP_CLIENT_DEFAULT_READ_BUF_MIN_LEN;
    (*self)->read_buf_max_len          = SVX_TCP_CLIENT_DEFAULT_READ_BUF_MAX_LEN;
    (*self)->write_buf_min_len         = SVX_TCP_CLIENT_DEFAULT_WRITE_BUF_MIN_LEN;
    (*self)->write_buf_high_water_mark = SVX_TCP_CLIENT_DEFAULT_WRITE_BUF_HIGH_WATER_MARK;
    memset(&((*self)->callbacks), 0, sizeof((*self)->callbacks));

    svx_inetaddr_from_ipport(&((*self)->client_addr), "0.0.0.0", 0);

    if(0 != (r = svx_tcp_connector_create(&((*self)->connector), looper,
                                          &((*self)->server_addr), &((*self)->client_addr),
                                          SVX_TCP_CLIENT_DEFAULT_CONNECTOR_RETRY_INIT_DELAY_MS,
                                          SVX_TCP_CLIENT_DEFAULT_CONNECTOR_RETRY_MAX_DELAY_MS,
                                          svx_tcp_client_handle_connected, *self)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    return 0;

 err:
    if(NULL != *self)
    {
        if(NULL != (*self)->connector) svx_tcp_connector_destroy(&((*self)->connector));
        free(*self);
        *self = NULL;
    }

    return r;
}

int svx_tcp_client_destroy(svx_tcp_client_t **self)
{
    if(NULL == self)  SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    if(NULL == *self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "*self:%p\n", *self);

    svx_tcp_connector_destroy(&((*self)->connector));
    free(*self);
    *self = NULL;
    return 0;
}

int svx_tcp_client_set_client_addr(svx_tcp_client_t *self, svx_inetaddr_t client_addr)
{
    int r;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    self->client_addr = client_addr;
    
    if(0 != (r = svx_tcp_connector_set_client_addr(self->connector, &(self->client_addr))))
        SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    
    return 0;
}

int svx_tcp_client_set_reconnect_delay(svx_tcp_client_t *self, int64_t init_delay_ms, int64_t max_delay_ms)
{
    int r;

    if(NULL == self || init_delay_ms < 0 || max_delay_ms < 0 || init_delay_ms > max_delay_ms)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, init_delay_ms:%"PRId64", max_delay_ms:%"PRId64"\n",
                                 self, init_delay_ms, max_delay_ms);

    if(0 != (r = svx_tcp_connector_set_retry_delay_ms(self->connector, init_delay_ms, max_delay_ms)))
        SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    return 0;
}

int svx_tcp_client_set_read_buf_len(svx_tcp_client_t *self, size_t min_len, size_t max_len)
{
    if(NULL == self || 0 == min_len || 0 == max_len || min_len > max_len)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, min_len:%zu, max_len:%zu\n", self, min_len, max_len);

    self->read_buf_min_len = min_len;
    self->read_buf_max_len = max_len;
    
    return 0;
}

int svx_tcp_client_set_write_buf_len(svx_tcp_client_t *self, size_t min_len)
{
    if(NULL == self || 0 == min_len)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, min_len:%zu\n", self, min_len);

    self->write_buf_min_len = min_len;
    
    return 0;
}

int svx_tcp_client_set_established_cb(svx_tcp_client_t *self, svx_tcp_connection_established_cb_t cb, void *arg)
{
    if(NULL == self || NULL == cb) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, cb:%p\n", self, cb);

    self->callbacks.established_cb     = cb;
    self->callbacks.established_cb_arg = arg;

    return 0;
}

int svx_tcp_client_set_read_cb(svx_tcp_client_t *self, svx_tcp_connection_read_cb_t cb, void *arg)
{
    if(NULL == self || NULL == cb) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, cb:%p\n", self, cb);

    self->callbacks.read_cb     = cb;
    self->callbacks.read_cb_arg = arg;

    return 0;
}

int svx_tcp_client_set_write_completed_cb(svx_tcp_client_t *self, svx_tcp_connection_write_completed_cb_t cb, void *arg)
{
    if(NULL == self || NULL == cb) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, cb:%p\n", self, cb);

    self->callbacks.write_completed_cb     = cb;
    self->callbacks.write_completed_cb_arg = arg;

    return 0;
}

int svx_tcp_client_set_high_water_mark_cb(svx_tcp_client_t *self, svx_tcp_connection_high_water_mark_cb_t cb, void *arg, size_t high_water_mark)
{
    if(NULL == self || NULL == cb || 0 == high_water_mark) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, cb:%p, high_water_mark:%zu\n\n", self, cb, high_water_mark);

    self->callbacks.high_water_mark_cb     = cb;
    self->callbacks.high_water_mark_cb_arg = arg;
    self->write_buf_high_water_mark        = high_water_mark;

    return 0;
}

int svx_tcp_client_set_closed_cb(svx_tcp_client_t *self, svx_tcp_connection_closed_cb_t cb, void *arg)
{
    if(NULL == self || NULL == cb) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, cb:%p\n", self, cb);

    self->callbacks.closed_cb     = cb;
    self->callbacks.closed_cb_arg = arg;

    return 0;
}

SVX_LOOPER_GENERATE_RUN_1(svx_tcp_client_connect, svx_tcp_client_t *, self)
int svx_tcp_client_connect(svx_tcp_client_t *self)
{
    int r;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_1(self->looper, svx_tcp_client_connect, self);

    if(0 != (r = svx_tcp_connector_connect(self->connector))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    return 0;
}

SVX_LOOPER_GENERATE_RUN_1(svx_tcp_client_cancel, svx_tcp_client_t *, self)
int svx_tcp_client_cancel(svx_tcp_client_t *self)
{
    int r;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_1(self->looper, svx_tcp_client_cancel, self);

    if(0 != (r = svx_tcp_connector_cancel(self->connector))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    return 0;
}

SVX_LOOPER_GENERATE_RUN_1(svx_tcp_client_disconnect, svx_tcp_client_t *, self)
int svx_tcp_client_disconnect(svx_tcp_client_t *self)
{
    int r;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_1(self->looper, svx_tcp_client_disconnect, self);

    /* always cancel the connector */
    if(0 != (r = svx_tcp_connector_cancel(self->connector))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    /* close the connection */
    if(NULL != self->conn_ptr)
        if(0 != (r = svx_tcp_connection_close(self->conn_ptr))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    return 0;
}

SVX_LOOPER_GENERATE_RUN_1(svx_tcp_client_reconnect, svx_tcp_client_t *, self)
int svx_tcp_client_reconnect(svx_tcp_client_t *self)
{
    int r;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_1(self->looper, svx_tcp_client_reconnect, self);

    /* cancel the connector */
    if(0 != (r = svx_tcp_connector_cancel(self->connector))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    /* close the connection */
    if(NULL != self->conn_ptr)
        if(0 != (r = svx_tcp_connection_close(self->conn_ptr))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    /* connect again */
    if(0 != (r = svx_tcp_connector_connect(self->connector))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    return 0;    
}
