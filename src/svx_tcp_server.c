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
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include "svx_tcp_server.h"
#include "svx_tcp_acceptor.h"
#include "svx_tcp_connection.h"
#include "svx_tree.h"
#include "svx_queue.h"
#include "svx_inetaddr.h"
#include "svx_errno.h"
#include "svx_log.h"

#define SVX_TCP_SERVER_DEFAULT_READ_BUF_MIN_LEN          128
#define SVX_TCP_SERVER_DEFAULT_READ_BUF_MAX_LEN          (1 * 1024 * 1024)
#define SVX_TCP_SERVER_DEFAULT_WRITE_BUF_MIN_LEN         128
#define SVX_TCP_SERVER_DEFAULT_WRITE_BUF_HIGH_WATER_MARK (4 * 1024 * 1024)

/* svx_tcp_connection_t's wrapper struct for rbtree's node */
typedef struct svx_tcp_connection_node
{
    svx_tcp_connection_t              *conn_ptr;
    RB_ENTRY(svx_tcp_connection_node)  link;
} svx_tcp_connection_node_t;
static __inline__ int svx_tcp_connection_node_cmp(svx_tcp_connection_node_t *a, svx_tcp_connection_node_t *b)
{
    if(a->conn_ptr == b->conn_ptr) return 0;
    else return (a->conn_ptr > b->conn_ptr ? 1 : -1);
}
typedef RB_HEAD(svx_tcp_connection_tree, svx_tcp_connection_node) svx_tcp_connection_tree_t;
RB_GENERATE_STATIC(svx_tcp_connection_tree, svx_tcp_connection_node, link, svx_tcp_connection_node_cmp)

/* TCP listener's queue */
typedef struct svx_tcp_server_listener
{
    svx_inetaddr_t      listen_addr;
    svx_tcp_acceptor_t *acceptor;
    TAILQ_ENTRY(svx_tcp_server_listener,) link;
} svx_tcp_server_listener_t;
typedef TAILQ_HEAD(svx_tcp_server_listener_queue, svx_tcp_server_listener,) svx_tcp_server_listener_queue_t;

struct svx_tcp_server
{
    svx_tcp_server_listener_queue_t  listeners;
    svx_tcp_connection_tree_t        conns;
    svx_looper_t                    *base_looper;
    pthread_t                       *io_threads;
    svx_looper_t                   **io_loopers;
    int                              io_loopers_num;
    int                              io_loopers_idx;
    size_t                           read_buf_min_len;
    size_t                           read_buf_max_len;
    size_t                           write_buf_min_len;
    size_t                           write_buf_high_water_mark;
    int                              keepalive_idle_s; /* <=0: do NOT send TCP keep-alive. >0: send period. */
    int                              keepalive_intvl_s;
    int                              keepalive_cnt;
    int                              reuseport;
    svx_tcp_connection_callbacks_t   callbacks;
};

static void svx_tcp_server_handle_remove(svx_tcp_connection_t *conn, void *arg);
SVX_LOOPER_GENERATE_RUN_2(svx_tcp_server_handle_remove, svx_tcp_connection_t *, conn, void *, arg)
static void svx_tcp_server_handle_remove(svx_tcp_connection_t *conn, void *arg)
{
    svx_tcp_server_t          *self = (svx_tcp_server_t *)arg;
    svx_tcp_connection_node_t *node;

    if(!svx_looper_is_loop_thread(self->base_looper))
    {
        SVX_LOOPER_DISPATCH_HELPER_2(self->base_looper, svx_tcp_server_handle_remove, conn, arg);
        return;
    }

    svx_tcp_connection_get_info(conn, (void *)&node);

    RB_REMOVE(svx_tcp_connection_tree, &(self->conns), node);
    free(node);
    svx_tcp_connection_del_ref(conn);
}

static void svx_tcp_server_handle_accepted(int fd, void *arg)
{
    svx_tcp_server_t          *self = (svx_tcp_server_t *)arg;
    svx_tcp_connection_node_t *node = NULL;
    svx_looper_t              *looper;
    int                        on;
    int                        r;

    /* get looper */
    if(0 == self->io_loopers_num)
    {
        looper = self->base_looper;
    }
    else
    {
        looper = self->io_loopers[self->io_loopers_idx];

        self->io_loopers_idx++;
        if(self->io_loopers_idx >= self->io_loopers_num)
            self->io_loopers_idx = 0;
    }

    /* set TCP keep-alive */
    if(self->keepalive_idle_s > 0)
    {
        on = 1;
        if(0 != setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)))
            SVX_LOG_ERRNO_GOTO_ERR(err, errno, NULL);
        if(0 != setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &(self->keepalive_idle_s), sizeof(self->keepalive_idle_s)))
            SVX_LOG_ERRNO_GOTO_ERR(err, errno, NULL);
        if(0 != setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &(self->keepalive_intvl_s), sizeof(self->keepalive_intvl_s)))
            SVX_LOG_ERRNO_GOTO_ERR(err, errno, NULL);
        if(0 != setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &(self->keepalive_cnt), sizeof(self->keepalive_cnt)))
            SVX_LOG_ERRNO_GOTO_ERR(err, errno, NULL);
    }

    /* create node and connection */
    if(NULL == (node = malloc(sizeof(svx_tcp_connection_node_t)))) SVX_LOG_ERRNO_GOTO_ERR(err, r = SVX_ERRNO_NOMEM, NULL);
    node->conn_ptr = NULL;
    if(0 != (r = svx_tcp_connection_create(&(node->conn_ptr), looper, fd,
                                           self->read_buf_min_len, self->read_buf_max_len,
                                           self->write_buf_min_len, self->write_buf_high_water_mark,
                                           &(self->callbacks), svx_tcp_server_handle_remove, self, node)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    /* save the node(and the connection) into conns collection */
    if(NULL != RB_INSERT(svx_tcp_connection_tree, &(self->conns), node))
        SVX_LOG_ERRNO_GOTO_ERR(err, r = SVX_ERRNO_REPEAT, "colliding conn's key?!\n");

    /* start connection */
    if(0 != (r = svx_tcp_connection_start(node->conn_ptr)))
    {
        RB_REMOVE(svx_tcp_connection_tree, &(self->conns), node);
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    }

    return;

 err:
    if(fd >= 0) close(fd);
    if(NULL != node)
    {
        if(NULL != node->conn_ptr) svx_tcp_connection_del_ref(node->conn_ptr);
        free(node);
    }
}

int svx_tcp_server_create(svx_tcp_server_t **self, svx_looper_t *looper, svx_inetaddr_t listen_addr)
{
    int r;

    if(NULL == self || NULL == looper) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, looper:%p\n", self, looper);

    if(NULL == (*self = malloc(sizeof(svx_tcp_server_t)))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    TAILQ_INIT(&((*self)->listeners));
    RB_INIT(&((*self)->conns));
    (*self)->base_looper                    = looper;
    (*self)->io_threads                     = NULL;
    (*self)->io_loopers                     = NULL;
    (*self)->io_loopers_num                 = 0;
    (*self)->io_loopers_idx                 = 0;
    (*self)->read_buf_min_len               = SVX_TCP_SERVER_DEFAULT_READ_BUF_MIN_LEN;
    (*self)->read_buf_max_len               = SVX_TCP_SERVER_DEFAULT_READ_BUF_MAX_LEN;
    (*self)->write_buf_min_len              = SVX_TCP_SERVER_DEFAULT_WRITE_BUF_MIN_LEN;
    (*self)->write_buf_high_water_mark      = SVX_TCP_SERVER_DEFAULT_WRITE_BUF_HIGH_WATER_MARK;
    (*self)->keepalive_idle_s               = 0;
    (*self)->keepalive_intvl_s              = 0;
    (*self)->keepalive_cnt                  = 0;
    (*self)->reuseport                      = 0;
    memset(&((*self)->callbacks), 0, sizeof((*self)->callbacks));

    if(0 != (r = svx_tcp_server_add_listener(*self, listen_addr))) SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
        
    return 0;

 err:
    if(NULL != *self)
    {
        free(*self);
        *self = NULL;
    }

    return r;
}

int svx_tcp_server_destroy(svx_tcp_server_t **self)
{
    svx_tcp_server_listener_t *listener = NULL, *listener_tmp = NULL;

    if(NULL == self)  SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    if(NULL == *self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "*self:%p\n", *self);

    TAILQ_FOREACH_FROM_SAFE(listener, &((*self)->listeners), link, listener_tmp)
    {
        TAILQ_REMOVE(&((*self)->listeners), listener, link);
        svx_tcp_acceptor_destroy(&(listener->acceptor));
        free(listener);
        listener = NULL;
    }
    
    free(*self);
    *self = NULL;
    return 0;
}

int svx_tcp_server_add_listener(svx_tcp_server_t *self, svx_inetaddr_t listen_addr)
{
    svx_tcp_server_listener_t *listener = NULL;
    char                       addr1[SVX_INETADDR_STR_ADDR_LEN];
    char                       addr2[SVX_INETADDR_STR_ADDR_LEN];
    int                        r;
    
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    /* Check for duplicate addresses */
    TAILQ_FOREACH(listener, &(self->listeners), link)
    {
        if(0 == svx_inetaddr_cmp_addr(&(listener->listen_addr), &listen_addr))
        {
            svx_inetaddr_get_addr_str(&(listener->listen_addr), addr1, sizeof(addr1));
            svx_inetaddr_get_addr_str(&listen_addr, addr2, sizeof(addr2));
            SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_REPEAT, "existent: %s, duplicate: %s\n", addr1, addr2);
        }
    }

    /* Add a new listener */
    if(NULL == (listener = malloc(sizeof(svx_tcp_server_listener_t))))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    listener->listen_addr = listen_addr;
    listener->acceptor    = NULL;
    if(0 != (r = svx_tcp_acceptor_create(&(listener->acceptor), self->base_looper, &(listener->listen_addr), svx_tcp_server_handle_accepted, self)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    TAILQ_INSERT_TAIL(&(self->listeners), listener, link);

    return 0;

 err:
    if(NULL != listener) free(listener);
    return r;
}

int svx_tcp_server_set_io_loopers_num(svx_tcp_server_t *self, int io_loopers_num)
{
    if(NULL == self || io_loopers_num < 0)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, io_loopers_num:%d\n", self, io_loopers_num);

    self->io_loopers_num = io_loopers_num;
    
    return 0;
}

int svx_tcp_server_set_keepalive(svx_tcp_server_t *self, time_t idle_s, time_t intvl_s, unsigned int cnt)
{
    if(NULL == self || idle_s < 0 || intvl_s < 0)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, idle_s:%jd, intvl_s:%jd\n", self, (intmax_t)idle_s, (intmax_t)intvl_s);

    self->keepalive_idle_s  = idle_s;
    self->keepalive_intvl_s = intvl_s;
    self->keepalive_cnt     = cnt;
    
    return 0;
}

int svx_tcp_server_set_reuseport(svx_tcp_server_t *self, int on)
{
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

#ifdef SO_REUSEPORT
    self->reuseport = (on ? 1 : 0);
#else
    SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTSPT, "System does NOT support SO_REUSEPORT.\n");
#endif

    return 0;
}

int svx_tcp_server_set_read_buf_len(svx_tcp_server_t *self, size_t min_len, size_t max_len)
{
    if(NULL == self || 0 == min_len || 0 == max_len || min_len > max_len)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, min_len:%zu, max_len:%zu\n", self, min_len, max_len);

    self->read_buf_min_len = min_len;
    self->read_buf_max_len = max_len;

    return 0;
}

int svx_tcp_server_set_write_buf_len(svx_tcp_server_t *self, size_t min_len)
{
    if(NULL == self || 0 == min_len)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, min_len:%zu\n", self, min_len);

    self->write_buf_min_len = min_len;

    return 0;
}

int svx_tcp_server_set_established_cb(svx_tcp_server_t *self, svx_tcp_connection_established_cb_t cb, void *arg)
{
    if(NULL == self || NULL == cb) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, cb:%p\n", self, cb);

    self->callbacks.established_cb     = cb;
    self->callbacks.established_cb_arg = arg;

    return 0;
}

int svx_tcp_server_set_read_cb(svx_tcp_server_t *self, svx_tcp_connection_read_cb_t cb, void *arg)
{
    if(NULL == self || NULL == cb) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, cb:%p\n", self, cb);

    self->callbacks.read_cb     = cb;
    self->callbacks.read_cb_arg = arg;

    return 0;
}

int svx_tcp_server_set_write_completed_cb(svx_tcp_server_t *self, svx_tcp_connection_write_completed_cb_t cb, void *arg)
{
    if(NULL == self || NULL == cb) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, cb:%p\n", self, cb);

    self->callbacks.write_completed_cb     = cb;
    self->callbacks.write_completed_cb_arg = arg;

    return 0;
}

int svx_tcp_server_set_high_water_mark_cb(svx_tcp_server_t *self, svx_tcp_connection_high_water_mark_cb_t cb, void *arg, size_t high_water_mark)
{
    if(NULL == self || NULL == cb || 0 == high_water_mark) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, cb:%p, high_water_mark:%zu\n\n", self, cb, high_water_mark);

    self->callbacks.high_water_mark_cb     = cb;
    self->callbacks.high_water_mark_cb_arg = arg;
    self->write_buf_high_water_mark        = high_water_mark;

    return 0;
}

int svx_tcp_server_set_closed_cb(svx_tcp_server_t *self, svx_tcp_connection_closed_cb_t cb, void *arg)
{
    if(NULL == self || NULL == cb) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, cb:%p\n", self, cb);

    self->callbacks.closed_cb     = cb;
    self->callbacks.closed_cb_arg = arg;

    return 0;
}

static void *svx_tcp_server_io_loopers_func(void *arg)
{
    svx_looper_t *looper = (svx_looper_t *)arg;

    svx_looper_loop(looper);

    return NULL;
}

SVX_LOOPER_GENERATE_RUN_1(svx_tcp_server_start, svx_tcp_server_t *, self)
int svx_tcp_server_start(svx_tcp_server_t *self)
{
    svx_tcp_server_listener_t *listener = NULL;
    uint16_t                   i;
    int                        r;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_1(self->base_looper, svx_tcp_server_start, self);

    if(self->io_loopers_num > 0)
    {
        if(NULL == (self->io_threads = malloc(sizeof(pthread_t) * self->io_loopers_num)))
            SVX_LOG_ERRNO_GOTO_ERR(err, r = SVX_ERRNO_NOMEM, NULL);
        if(NULL == (self->io_loopers = malloc(sizeof(svx_looper_t *) * self->io_loopers_num)))
            SVX_LOG_ERRNO_GOTO_ERR(err, r = SVX_ERRNO_NOMEM, NULL);
        self->io_loopers_idx = 0;

        for(i = 0; i < self->io_loopers_num; i++)
            if(0 != (r = svx_looper_create(&(self->io_loopers[i]))))
                SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

        for(i = 0; i < self->io_loopers_num; i++)
        {
            if(0 != (r = pthread_create(&(self->io_threads[i]), NULL, &svx_tcp_server_io_loopers_func, self->io_loopers[i])))
            {
                while(i > 0)
                {
                    svx_looper_quit(self->io_loopers[i - 1]);
                    pthread_join(self->io_threads[i - 1], NULL);
                    i--;
                }
                SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
            }
        }
    }

    TAILQ_FOREACH(listener, &(self->listeners), link)
        if(0 != (r = svx_tcp_acceptor_start(listener->acceptor, self->reuseport)))
            SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    return 0;

 err:
    if(NULL != self->io_loopers)
    {
        for(i = 0; i < self->io_loopers_num; i++)
            if(NULL != self->io_loopers[i])
                svx_looper_destroy(&(self->io_loopers[i]));
        free(self->io_loopers);
        self->io_loopers = NULL;
        self->io_loopers_idx = 0;
    }
    if(NULL != self->io_threads)
    {
        free(self->io_threads);
        self->io_threads = NULL;
    }
    
    return r;
}

SVX_LOOPER_GENERATE_RUN_1(svx_tcp_server_stop, svx_tcp_server_t *, self)
int svx_tcp_server_stop(svx_tcp_server_t *self)
{
    svx_tcp_server_listener_t *listener = NULL;
    svx_tcp_connection_node_t *node     = NULL;
    svx_tcp_connection_node_t *node_tmp = NULL;
    int                        i;
    int                        r;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    SVX_LOOPER_CHECK_DISPATCH_HELPER_1(self->base_looper, svx_tcp_server_stop, self);

    TAILQ_FOREACH(listener, &(self->listeners), link)
        if(0 != (r = svx_tcp_acceptor_stop(listener->acceptor)))
            SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    RB_FOREACH_SAFE(node, svx_tcp_connection_tree, &(self->conns), node_tmp)
    {
        svx_tcp_connection_destroy(node->conn_ptr);
        RB_REMOVE(svx_tcp_connection_tree, &(self->conns), node);
        free(node);
    }

    if(self->io_loopers_num > 0)
    {
        for(i = 0; i < self->io_loopers_num; i++)
            svx_looper_quit(self->io_loopers[i]);

        for(i = 0; i < self->io_loopers_num; i++)
        {
            pthread_join(self->io_threads[i], NULL);
            svx_looper_destroy(&(self->io_loopers[i]));
        }

        free(self->io_loopers);
        self->io_loopers = NULL;
        self->io_loopers_idx = 0;

        free(self->io_threads);
        self->io_threads = NULL;
    }
    
    return 0;
}
