/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "svx_tcp_connection.h"
#include "svx_inetaddr.h"
#include "svx_circlebuf.h"
#include "svx_looper.h"
#include "svx_channel.h"
#include "svx_errno.h"
#include "svx_log.h"

#define SVX_TCP_CONNECTION_READ_BUF_MIN_STEP  64
#define SVX_TCP_CONNECTION_WRITE_BUF_MIN_STEP 64

typedef enum
{
    SVX_TCP_CONNECTION_STATE_CONNECTED,
    SVX_TCP_CONNECTION_STATE_DISCONNECTING,
    SVX_TCP_CONNECTION_STATE_DISCONNECTED
} svx_tcp_connection_state_t;

struct svx_tcp_connection
{
    svx_tcp_connection_state_t      state;
    unsigned int                    ref_count;
    svx_looper_t                   *looper;
    svx_channel_t                  *channel;
    int                             fd;
    svx_circlebuf_t                *read_buf;
    size_t                          read_buf_max_len;
    svx_circlebuf_t                *write_buf;
    size_t                          write_buf_high_water_mark;
    svx_tcp_connection_callbacks_t *callbacks;
    int                             write_completed_enable;
    int                             high_water_mark_enable;
    svx_tcp_connection_remove_cb_t  remove_cb;
    void                           *remove_cb_arg;
    void                           *context;
    void                           *info;
};

/* callback for write_completed */
typedef struct
{
    svx_tcp_connection_t *self;
} svx_tcp_connection_write_completed_callback_param_t;
static void svx_tcp_connection_write_completed_callback_run(void *arg)
{
    svx_tcp_connection_write_completed_callback_param_t *p = (svx_tcp_connection_write_completed_callback_param_t *)arg;
    p->self->callbacks->write_completed_cb(p->self, p->self->callbacks->write_completed_cb_arg);
    svx_tcp_connection_del_ref(p->self);
}
static void svx_tcp_connection_write_completed_callback_clean(void *arg)
{
    svx_tcp_connection_write_completed_callback_param_t *p = (svx_tcp_connection_write_completed_callback_param_t *)arg;
    svx_tcp_connection_del_ref(p->self);
}

/* callback for high_water_mark */
typedef struct
{
    svx_tcp_connection_t *self;
    size_t                water_mark;
} svx_tcp_connection_high_water_mark_callback_param_t;
static void svx_tcp_connection_high_water_mark_callback_run(void *arg)
{
    svx_tcp_connection_high_water_mark_callback_param_t *p = (svx_tcp_connection_high_water_mark_callback_param_t *)arg;
    p->self->callbacks->high_water_mark_cb(p->self, p->water_mark, p->self->callbacks->high_water_mark_cb_arg);
    svx_tcp_connection_del_ref(p->self);
}
static void svx_tcp_connection_high_water_mark_callback_clean(void *arg)
{
    svx_tcp_connection_high_water_mark_callback_param_t *p = (svx_tcp_connection_high_water_mark_callback_param_t *)arg;
    svx_tcp_connection_del_ref(p->self);
}

static void svx_tcp_connection_handle_close(svx_tcp_connection_t *self)
{
    int r;
    
    if(SVX_TCP_CONNECTION_STATE_DISCONNECTED == self->state) return;
    self->state = SVX_TCP_CONNECTION_STATE_DISCONNECTED;

    if(0 != (r = svx_channel_del_events(self->channel, SVX_CHANNEL_EVENT_ALL)))
        SVX_LOG_ERRNO_ERR(r, "del_events() error. fd:%d\n", self->fd);
    
    self->remove_cb(self, self->remove_cb_arg);
}
SVX_LOOPER_GENERATE_RUN_1(svx_tcp_connection_handle_close, svx_tcp_connection_t *, self)

static void svx_tcp_connection_handle_read(void *arg)
{
    svx_tcp_connection_t *self = (svx_tcp_connection_t *)arg;
    struct iovec          iov[3];
    int                   iov_cnt;
    char                  extra_buf[64 * 1024];
    size_t                extra_buf_len;
    size_t                buf_len;
    size_t                freespace_len;
    ssize_t               n;
    int                   r;

    /* prepare buffers for readv() */
    svx_circlebuf_get_buf_len(self->read_buf, &buf_len);
    svx_circlebuf_get_freespace_ptr(self->read_buf, (uint8_t **)(&(iov[0].iov_base)), &(iov[0].iov_len),
                                    (uint8_t **)(&(iov[1].iov_base)), &(iov[1].iov_len));
    freespace_len = iov[0].iov_len + iov[1].iov_len;

    if((buf_len != freespace_len) || (NULL == iov[0].iov_base && NULL == iov[1].iov_base))
        SVX_LOG_ERRNO_GOTO_ERR(err, SVX_ERRNO_UNKNOWN, "You MUST always take out all data from the buffer on each read-callback. fd:%d\n", self->fd);

    if(freespace_len > self->read_buf_max_len)
    {
        SVX_LOG_ERRNO_GOTO_ERR(err, SVX_ERRNO_UNKNOWN, "fd:%d\n", self->fd);
    }
    else if(freespace_len == self->read_buf_max_len)
    {
        /* read_buf reached the max_len limit, so do not use the extra_buf */
        iov_cnt = (NULL == iov[1].iov_base ? 1 : 2);
    }
    else
    {
        extra_buf_len = ((self->read_buf_max_len - freespace_len) < sizeof(extra_buf) ?
                         (self->read_buf_max_len - freespace_len) : sizeof(extra_buf));
        if(NULL == iov[1].iov_base)
        {
            iov[1].iov_base = extra_buf;
            iov[1].iov_len  = extra_buf_len;
            iov_cnt = 2;
        }
        else
        {
            iov[2].iov_base = extra_buf;
            iov[2].iov_len  = extra_buf_len;
            iov_cnt = 3;
        }
    }

    /* read data */
    do n = readv(self->fd, iov, iov_cnt);
    while(-1 == n && EINTR == errno);

    if(n < 0)
    {
        if(EAGAIN == errno || EWOULDBLOCK == errno) return;

        if(ECONNRESET == errno)
            SVX_LOG_ERRNO_GOTO_NOTICE(err, errno, "readv() error. fd:%d\n", self->fd);
        else
            SVX_LOG_ERRNO_GOTO_ERR(err, errno, "readv() error. fd:%d\n", self->fd);
    }
    else if(0 == n)
    {
        /* FIN has arrived */
        svx_tcp_connection_handle_close(self);
    }
    else
    {
        /* read OK */
        if(n <= freespace_len)
        {
            /* extra_buf not used*/
            svx_circlebuf_commit_data(self->read_buf, n);
        }
        else
        {
            /* extra_buf used*/
            svx_circlebuf_commit_data(self->read_buf, freespace_len);
            if(0 != (r = svx_circlebuf_append_data(self->read_buf, (uint8_t *)extra_buf, (size_t)(n - freespace_len))))
                SVX_LOG_ERRNO_GOTO_ERR(err, r, "append_data() error. fd:%d\n", self->fd);
        }

        /* callback */
        if(self->callbacks->read_cb)
            self->callbacks->read_cb(self, self->read_buf, self->callbacks->read_cb_arg);
        else
            svx_circlebuf_erase_all_data(self->read_buf);
    }

    return;

 err:
    svx_tcp_connection_handle_close(self);    
}

static void svx_tcp_connection_handle_write(void *arg)
{
    svx_tcp_connection_t *self = (svx_tcp_connection_t *)arg;
    struct iovec          iov[2];
    size_t                data_len;
    ssize_t               n;
    int                   r;

    if(SVX_TCP_CONNECTION_STATE_CONNECTED != self->state) return;

    svx_circlebuf_get_data_ptr(self->write_buf, (uint8_t **)(&(iov[0].iov_base)), &(iov[0].iov_len),
                               (uint8_t **)(&(iov[1].iov_base)), &(iov[1].iov_len));
    data_len = iov[0].iov_len + iov[1].iov_len;

    /* no data need to write */
    if(0 == data_len)
    {
        if(0 != (r = svx_channel_del_events(self->channel, SVX_CHANNEL_EVENT_WRITE)))
            SVX_LOG_ERRNO_GOTO_ERR(err, r, "del_events() error. fd:%d\n", self->fd);
        return;
    }

    /* write data */
    do n = writev(self->fd, iov, (NULL == iov[1].iov_base) ? 1 : 2);
    while(-1 == n && EINTR == errno);

    if(n < 0)
    {
        if(EAGAIN == errno || EWOULDBLOCK == errno) return;

        /* error */
        SVX_LOG_ERRNO_GOTO_ERR(err, errno, "writev() error. fd:%d\n", self->fd);
    }
    else if(n > 0)
    {
        /* write OK */
        svx_circlebuf_erase_data(self->write_buf, n);

        if(n == data_len)
        {
            if(0 != (r = svx_channel_del_events(self->channel, SVX_CHANNEL_EVENT_WRITE)))
                SVX_LOG_ERRNO_GOTO_ERR(err, r, "del_events() error. fd:%d\n", self->fd);
            
            if(SVX_TCP_CONNECTION_STATE_DISCONNECTING == self->state)
                shutdown(self->fd, SHUT_WR);

            if(self->callbacks->write_completed_cb && self->write_completed_enable)
            {
                svx_tcp_connection_add_ref(self);
                svx_tcp_connection_write_completed_callback_param_t p = {self};
                svx_looper_dispatch(self->looper, svx_tcp_connection_write_completed_callback_run,
                                    svx_tcp_connection_write_completed_callback_clean, &p, sizeof(p));
            }
        }
    }

    return;

 err:
    svx_tcp_connection_handle_close(self);    
}

int svx_tcp_connection_create(svx_tcp_connection_t **self, svx_looper_t *looper, int fd,
                              size_t read_buf_min_len, size_t read_buf_max_len,
                              size_t write_buf_min_len, size_t write_buf_high_water_mark,
                              svx_tcp_connection_callbacks_t *callbacks,
                              svx_tcp_connection_remove_cb_t remove_cb, void *remove_cb_arg,
                              void *info)
{
    int r = 0;
    
    if(NULL == self || NULL == looper || fd < 0 ||
       0 == read_buf_min_len || 0 == read_buf_max_len || read_buf_min_len > read_buf_max_len ||
       0 == write_buf_min_len || 0 == write_buf_high_water_mark || write_buf_min_len > write_buf_high_water_mark || NULL == callbacks || NULL == remove_cb)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, looper:%p, fd:%d, read_buf_min_len:%zu, "
                                 "read_buf_max_len:%zu, write_buf_min_len:%zu, write_buf_high_water_mark:%zu, callbacks:%p, remove_cb:%p\n",
                                 self, looper, fd, read_buf_min_len, read_buf_max_len, write_buf_min_len, write_buf_high_water_mark, callbacks, remove_cb);

    if(NULL == (*self = malloc(sizeof(svx_tcp_connection_t)))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    (*self)->state                     = SVX_TCP_CONNECTION_STATE_DISCONNECTED;
    (*self)->ref_count                 = 1;
    (*self)->looper                    = looper;
    (*self)->channel                   = NULL;
    (*self)->fd                        = fd;
    (*self)->read_buf                  = NULL;
    (*self)->read_buf_max_len          = read_buf_max_len;
    (*self)->write_buf                 = NULL;
    (*self)->write_buf_high_water_mark = write_buf_high_water_mark;
    (*self)->callbacks                 = callbacks;
    (*self)->write_completed_enable    = 1;
    (*self)->high_water_mark_enable    = 1;
    (*self)->remove_cb                 = remove_cb;
    (*self)->remove_cb_arg             = remove_cb_arg;
    (*self)->context                   = NULL;
    (*self)->info                      = info;

    if(0 != (r = svx_channel_create(&((*self)->channel), (*self)->looper, (*self)->fd, SVX_CHANNEL_EVENT_NULL)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    if(0 != (r = svx_channel_set_read_callback((*self)->channel, svx_tcp_connection_handle_read, *self)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    if(0 != (r = svx_channel_set_write_callback((*self)->channel, svx_tcp_connection_handle_write, *self)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    if(0 != (r = svx_circlebuf_create(&((*self)->read_buf), read_buf_max_len, read_buf_min_len, SVX_TCP_CONNECTION_READ_BUF_MIN_STEP)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    if(0 != (r = svx_circlebuf_create(&((*self)->write_buf), 0, write_buf_min_len, SVX_TCP_CONNECTION_WRITE_BUF_MIN_STEP)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    return 0;

 err:
    if(*self)
    {
        if((*self)->channel)   svx_channel_destroy(&((*self)->channel));
        if((*self)->read_buf)  svx_circlebuf_destroy(&((*self)->read_buf));
        if((*self)->write_buf) svx_circlebuf_destroy(&((*self)->write_buf));
        free(*self);
        *self = NULL;
    }

    return r;
}

SVX_LOOPER_GENERATE_RUN_1(svx_tcp_connection_destroy, svx_tcp_connection_t *, self)
int svx_tcp_connection_destroy(svx_tcp_connection_t *self)
{
    int r;
    
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_1(self->looper, svx_tcp_connection_destroy, self);

    self->state = SVX_TCP_CONNECTION_STATE_DISCONNECTED;
    if(0 != (r = svx_circlebuf_destroy(&(self->read_buf)))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_circlebuf_destroy(&(self->write_buf)))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_channel_destroy(&(self->channel)))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(self->fd >= 0) if(0 != close(self->fd)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    
    if(self->callbacks->closed_cb)
        self->callbacks->closed_cb(self, self->callbacks->closed_cb_arg);
    
    free(self);

    return 0;
}

SVX_LOOPER_GENERATE_RUN_1(svx_tcp_connection_start, svx_tcp_connection_t *, self)
int svx_tcp_connection_start(svx_tcp_connection_t *self)
{
    int r = 0;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_1(self->looper, svx_tcp_connection_start, self);

    if(0 != (r = svx_channel_add_events(self->channel, SVX_CHANNEL_EVENT_READ)))
        SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    self->state = SVX_TCP_CONNECTION_STATE_CONNECTED;

    if(self->callbacks->established_cb)
        self->callbacks->established_cb(self, self->callbacks->established_cb_arg);

    return 0;
}

int svx_tcp_connection_get_info(svx_tcp_connection_t *self, void **info)
{
    if(NULL == self || NULL == info)  SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, info:%p\n", self, info);

    *info = self->info;
    return 0;
}

int svx_tcp_connection_set_context(svx_tcp_connection_t *self, void *context)
{
    if(NULL == self)  SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    self->context = context;
    return 0;
}

int svx_tcp_connection_get_context(svx_tcp_connection_t *self, void **context)
{
    if(NULL == self || NULL == context)  SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, context:%p\n", self, context);

    *context = self->context;
    return 0;
}

static int svx_tcp_connection_destroy_safely(svx_tcp_connection_t *self)
{
    int r;
    
    if(NULL == self)  SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    if(0 != (r = svx_circlebuf_destroy(&(self->read_buf)))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_circlebuf_destroy(&(self->write_buf)))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_channel_destroy(&(self->channel)))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(self->fd >= 0) if(0 != close(self->fd)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    
    if(self->callbacks->closed_cb)
        self->callbacks->closed_cb(self, self->callbacks->closed_cb_arg);

    free(self);
    return 0;
}
SVX_LOOPER_GENERATE_RUN_1(svx_tcp_connection_destroy_safely, svx_tcp_connection_t *, self);

int svx_tcp_connection_add_ref(svx_tcp_connection_t *self)
{
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    if(!svx_looper_is_loop_thread(self->looper))
    {
        SVX_LOG_ERRNO_ERR(SVX_ERRNO_UNKNOWN, NULL);
        exit(1);
    }

    self->ref_count++;
    return 0;
}

SVX_LOOPER_GENERATE_RUN_1(svx_tcp_connection_del_ref, svx_tcp_connection_t *, self)
int svx_tcp_connection_del_ref(svx_tcp_connection_t *self)
{
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_1(self->looper, svx_tcp_connection_del_ref, self);

    self->ref_count--;
    if(0 == self->ref_count)
    {
        /* always destroy the conn in the next round */
        SVX_LOOPER_DISPATCH_HELPER_1(self->looper, svx_tcp_connection_destroy_safely, self);
    }
    return 0;
}

SVX_LOOPER_GENERATE_RUN_1(svx_tcp_connection_enable_read, svx_tcp_connection_t *, self)
int svx_tcp_connection_enable_read(svx_tcp_connection_t *self)
{
    int r;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_1(self->looper, svx_tcp_connection_enable_read, self);

    if(SVX_TCP_CONNECTION_STATE_DISCONNECTED == self->state)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTCONN, "not connected. enable read failed. fd:%d\n", self->fd);

    if(0 != (r = svx_channel_add_events(self->channel, SVX_CHANNEL_EVENT_READ)))
        SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    
    return 0;
}

SVX_LOOPER_GENERATE_RUN_1(svx_tcp_connection_disable_read, svx_tcp_connection_t *, self)
int svx_tcp_connection_disable_read(svx_tcp_connection_t *self)
{
    int r;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_1(self->looper, svx_tcp_connection_disable_read, self);

    if(SVX_TCP_CONNECTION_STATE_DISCONNECTED == self->state)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTCONN, "not connected. disable read failed. fd:%d\n", self->fd);

    if(0 != (r = svx_channel_del_events(self->channel, SVX_CHANNEL_EVENT_READ)))
        SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    
    return 0;
}

SVX_LOOPER_GENERATE_RUN_1(svx_tcp_connection_enable_write_completed, svx_tcp_connection_t *, self)
int svx_tcp_connection_enable_write_completed(svx_tcp_connection_t *self)
{
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_1(self->looper, svx_tcp_connection_enable_write_completed, self);

    self->write_completed_enable = 1;
    
    return 0;
}

SVX_LOOPER_GENERATE_RUN_1(svx_tcp_connection_disable_write_completed, svx_tcp_connection_t *, self)
int svx_tcp_connection_disable_write_completed(svx_tcp_connection_t *self)
{
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_1(self->looper, svx_tcp_connection_disable_write_completed, self);

    self->write_completed_enable = 0;
    
    return 0;
}

SVX_LOOPER_GENERATE_RUN_1(svx_tcp_connection_enable_high_water_mark, svx_tcp_connection_t *, self)
int svx_tcp_connection_enable_high_water_mark(svx_tcp_connection_t *self)
{
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_1(self->looper, svx_tcp_connection_enable_high_water_mark, self);

    self->high_water_mark_enable = 1;
    
    return 0;
}

SVX_LOOPER_GENERATE_RUN_1(svx_tcp_connection_disable_high_water_mark, svx_tcp_connection_t *, self)
int svx_tcp_connection_disable_high_water_mark(svx_tcp_connection_t *self)
{
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_1(self->looper, svx_tcp_connection_disable_high_water_mark, self);

    self->high_water_mark_enable = 0;
    
    return 0;
}

SVX_LOOPER_GENERATE_RUN_2(svx_tcp_connection_shrink_read_buf, svx_tcp_connection_t *, self, size_t, freespace_keep)
int svx_tcp_connection_shrink_read_buf(svx_tcp_connection_t *self, size_t freespace_keep)
{
    int r;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_2(self->looper, svx_tcp_connection_shrink_read_buf, self, freespace_keep);

    if(0 != (r = svx_circlebuf_shrink(self->read_buf, freespace_keep)))
        SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    
    return 0;
}

SVX_LOOPER_GENERATE_RUN_2(svx_tcp_connection_shrink_write_buf, svx_tcp_connection_t *, self, size_t, freespace_keep)
int svx_tcp_connection_shrink_write_buf(svx_tcp_connection_t *self, size_t freespace_keep)
{
    int r;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_2(self->looper, svx_tcp_connection_shrink_write_buf, self, freespace_keep);

    if(0 != (r = svx_circlebuf_shrink(self->write_buf, freespace_keep)))
        SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    
    return 0;
}

typedef struct
{
    svx_tcp_connection_t *self;
    uint8_t              *buf;
    size_t                len;
} svx_tcp_connection_write_param_t;
static void svx_tcp_connection_write_run(void *arg)
{
    svx_tcp_connection_write_param_t *p = (svx_tcp_connection_write_param_t *)arg;
    svx_tcp_connection_write(p->self, p->buf, p->len);
    if(p->buf) free(p->buf);
}
static void svx_tcp_connection_write_clean(void *arg)
{
    svx_tcp_connection_write_param_t *p = (svx_tcp_connection_write_param_t *)arg;
    if(p->buf) free(p->buf);
}
int svx_tcp_connection_write(svx_tcp_connection_t *self, const uint8_t *buf, size_t len)
{
    size_t   write_buf_data_len = 0;
    uint8_t  channel_events     = 0;
    size_t   data_len_old       = 0;
    size_t   data_len_new       = 0;
    ssize_t  n                  = 0;
    int      r                  = 0;
    uint8_t *buf2               = NULL;

    if(NULL == self || NULL == buf || 0 == len) 
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, buf:%p, len:%zu\n", self, buf, len);

    if(!svx_looper_is_loop_thread(self->looper))
    {
        if(NULL == (buf2 = malloc(len))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
        memcpy(buf2, buf, len);
        svx_tcp_connection_write_param_t p = {self, buf2, len};
        svx_looper_dispatch(self->looper, svx_tcp_connection_write_run, svx_tcp_connection_write_clean, &p, sizeof(p));
        return 0;
    }

    if(SVX_TCP_CONNECTION_STATE_CONNECTED != self->state)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTCONN, "not connected. write failed. fd:%d\n", self->fd);

    svx_channel_get_events(self->channel, &channel_events);
    svx_circlebuf_get_data_len(self->write_buf, &write_buf_data_len);

    /* if write buffer is empty, try to write immediately */
    if((0 == (channel_events & SVX_CHANNEL_EVENT_WRITE)) && 0 == write_buf_data_len)
    {
        do n = write(self->fd, buf, len);
        while(-1 == n && EINTR == errno);

        if(n < 0)
        {
            /* error */
            if(EAGAIN == errno || EWOULDBLOCK == errno)
                n = 0; /* wrote nothing, try later */
            else
                SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, "write() error. fd:%d\n", self->fd);
        }
        else
        {
            /* OK */
            if(len == n)
            {
                if(self->callbacks->write_completed_cb && self->write_completed_enable)
                {
                    svx_tcp_connection_add_ref(self);
                    svx_tcp_connection_write_completed_callback_param_t p = {self};
                    svx_looper_dispatch(self->looper, svx_tcp_connection_write_completed_callback_run,
                                        svx_tcp_connection_write_completed_callback_clean, &p, sizeof(p));
                }
            }
        }
    }

    /* save the rest of unsent data to the write buffer */
    if(n < len)
    {
        svx_circlebuf_get_data_len(self->write_buf, &data_len_old);
        if(0 != (r = svx_circlebuf_append_data(self->write_buf, buf + n, len - n)))
            SVX_LOG_ERRNO_GOTO_ERR(err, r, "append_data() error. fd:%d\n", self->fd);
        svx_circlebuf_get_data_len(self->write_buf, &data_len_new);

        if(self->callbacks->high_water_mark_cb &&
           self->high_water_mark_enable &&
           data_len_new >= self->write_buf_high_water_mark && 
           data_len_old < self->write_buf_high_water_mark)
        {
            svx_tcp_connection_add_ref(self);
            svx_tcp_connection_high_water_mark_callback_param_t p = {self, data_len_new};
            svx_looper_dispatch(self->looper, svx_tcp_connection_high_water_mark_callback_run,
                                svx_tcp_connection_high_water_mark_callback_clean, &p, sizeof(p));
        }

        /* enable writing for channel */
        if(0 == (channel_events & SVX_CHANNEL_EVENT_WRITE))
        {
            if(0 != (r = svx_channel_add_events(self->channel, SVX_CHANNEL_EVENT_WRITE)))
                SVX_LOG_ERRNO_GOTO_ERR(err, r, "add_events() error. fd:%d\n", self->fd);
        }
    }

    return 0;

 err:
    svx_tcp_connection_handle_close(self);
    return r;
}

SVX_LOOPER_GENERATE_RUN_1(svx_tcp_connection_shutdown_wr, svx_tcp_connection_t *, self)
int svx_tcp_connection_shutdown_wr(svx_tcp_connection_t *self)
{
    uint8_t channel_events = 0;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_1(self->looper, svx_tcp_connection_shutdown_wr, self);

    if(SVX_TCP_CONNECTION_STATE_CONNECTED != self->state) return 0;

    self->state = SVX_TCP_CONNECTION_STATE_DISCONNECTING;
    svx_channel_get_events(self->channel, &channel_events);
    if(0 == (channel_events & SVX_CHANNEL_EVENT_WRITE))
        shutdown(self->fd, SHUT_WR);
    
    return 0;
}

int svx_tcp_connection_close(svx_tcp_connection_t *self)
{
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    /* always close the connection in the next round */
    SVX_LOOPER_DISPATCH_HELPER_1(self->looper, svx_tcp_connection_handle_close, self);

    return 0;
}
