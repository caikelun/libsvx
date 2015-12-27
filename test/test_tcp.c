/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include "svx_poller.h"
#include "svx_looper.h"
#include "svx_inetaddr.h"
#include "svx_tcp_server.h"
#include "svx_tcp_client.h"
#include "svx_threadpool.h"
#include "svx_log.h"

#define TEST_TCP_LISTEN_IPV4               "127.0.0.1"
#define TEST_TCP_LISTEN_IPV6               "::1"
#define TEST_TCP_LISTEN_PORT               20000

#define TEST_TCP_READ_BUF_MIN_LEN          128
#define TEST_TCP_READ_BUF_MAX_LEN          (64 * 1024)
#define TEST_TCP_WRITE_BUF_MIN_LEN         128
#define TEST_TCP_WRITE_BUF_HIGH_WATER_MARK (64 * 1024)

#define TEST_TCP_SMALL_BODY_MAX_LEN        64
#define TEST_TCP_LARGE_BODY_MAX_LEN        (5 * 1024 * 1024)

#define TEST_TCP_CLIENT_LOOPER_CNT         5 /* how many loopers(threads) for clients? */
#define TEST_TCP_CLIENT_CNT_PER_LOOPER     3 /* how many clients per looper? */
#define TEST_TCP_CLIENT_ROUND_PER_CLIENT   2 /* how many rounds for each client */

#define SVX_TEST_TCP_PROTO_CMD_ECHO        1
#define SVX_TEST_TCP_PROTO_CMD_UPLOAD      2
#define SVX_TEST_TCP_PROTO_CMD_DOWNLOAD    3

#define TEST_EXIT         do {SVX_LOG_ERR("exit(1). line: %d. errno:%d.\n", __LINE__, errno); exit(1);} while(0)
#define TEST_CHECK_FAILED do {SVX_LOG_ERR("check failed. line: %d.\n", __LINE__); exit(1);} while(0)

typedef struct
{
    int looper_idx;
    int client_idx;
} test_tcp_client_info_t;

typedef struct
{
    uint8_t  cmd;  /* 1:echo small body. 2:upload large body. 3:download large body. */
    uint8_t  type; /* 1:request. 2:response. */
    uint32_t looper_idx; /* for checking data, only for test program */
    uint32_t client_idx; /* for checking data, only for test program */
    uint32_t body_len;
}__attribute__((packed)) test_tcp_proto_header_t;

typedef struct
{
    pthread_t         tid;
    svx_looper_t     *looper;
    svx_threadpool_t *threadpool;
    svx_tcp_server_t *tcp_server;
    const char       *ip;
    int               io_loopers_num;
} test_tcp_server_t;

typedef struct
{
    uint8_t  cmd;
    uint32_t looper_idx;
    uint32_t client_idx;
    uint32_t body_len;
    uint32_t body_idx; /* hold the body index uploaded or downloaded */
} test_tcp_server_ctx_t;

typedef struct
{
    pthread_t          tid;
    svx_looper_t      *looper;
    svx_threadpool_t  *threadpool;
    svx_tcp_client_t **tcp_client;
    size_t             tcp_clients_alive_cnt;
} test_tcp_client_t;

typedef struct
{
    uint32_t cmd_round_total;
    uint32_t cmd_round_cur;
    uint8_t  cmd_send; /* command seq: 1 -> 2 -> 3 */
    uint8_t  cmd_recv;
    uint32_t body_len;
    uint32_t body_idx; /* hold the body index uploaded or downloaded */
} test_tcp_client_ctx_t;

typedef struct
{
    svx_tcp_connection_t   *conn;
    test_tcp_client_ctx_t  *ctx;
    test_tcp_client_info_t  client_info;
} test_tcp_client_worker_thread_param_t;

typedef struct
{
    uint8_t content;
    size_t  len;
} test_tcp_msg_t;
static test_tcp_msg_t msg_echo[TEST_TCP_CLIENT_LOOPER_CNT][TEST_TCP_CLIENT_CNT_PER_LOOPER];
static test_tcp_msg_t msg_upload[TEST_TCP_CLIENT_LOOPER_CNT][TEST_TCP_CLIENT_CNT_PER_LOOPER];
static test_tcp_msg_t msg_download[TEST_TCP_CLIENT_LOOPER_CNT][TEST_TCP_CLIENT_CNT_PER_LOOPER];

static test_tcp_server_t test_tcp_server;
static int               test_tcp_server_closed_conns = 0;
static pthread_mutex_t   test_tcp_server_closed_conns_mutex = PTHREAD_MUTEX_INITIALIZER;
static test_tcp_client_t test_tcp_clients[TEST_TCP_CLIENT_LOOPER_CNT];
static int               test_tcp_clients_alive_cnt = TEST_TCP_CLIENT_LOOPER_CNT;
static pthread_mutex_t   test_tcp_clients_alive_cnt_mutex = PTHREAD_MUTEX_INITIALIZER;

static void test_tcp_build_msg_buf(uint8_t *buf, size_t buf_len, uint8_t cmd, uint32_t looper_idx, uint32_t client_idx)
{
    uint8_t  content;
    uint32_t len;

    switch(cmd)
    {
    case SVX_TEST_TCP_PROTO_CMD_ECHO:
        content = msg_echo[looper_idx][client_idx].content;
        len     = msg_echo[looper_idx][client_idx].len;
        break;
    case SVX_TEST_TCP_PROTO_CMD_UPLOAD:
        content = msg_upload[looper_idx][client_idx].content;
        len     = msg_upload[looper_idx][client_idx].len;
        break;
    case SVX_TEST_TCP_PROTO_CMD_DOWNLOAD:
        content = msg_download[looper_idx][client_idx].content;
        len     = msg_download[looper_idx][client_idx].len;
        break;
    default:
        TEST_EXIT;
    }

    if(buf_len > len) TEST_EXIT;

    memset(buf, content, buf_len);
}

static int test_tcp_check_msg_buf(uint8_t *buf, size_t buf_len, uint8_t cmd, uint32_t looper_idx, uint32_t client_idx)
{
    uint8_t  content;
    uint32_t len;
    size_t   i;

    switch(cmd)
    {
    case SVX_TEST_TCP_PROTO_CMD_ECHO:
        content = msg_echo[looper_idx][client_idx].content;
        len     = msg_echo[looper_idx][client_idx].len;
        break;
    case SVX_TEST_TCP_PROTO_CMD_UPLOAD:
        content = msg_upload[looper_idx][client_idx].content;
        len     = msg_upload[looper_idx][client_idx].len;
        break;
    case SVX_TEST_TCP_PROTO_CMD_DOWNLOAD:
        content = msg_download[looper_idx][client_idx].content;
        len     = msg_download[looper_idx][client_idx].len;
        break;
    default:
        TEST_EXIT;
    }

    if(buf_len > len) TEST_EXIT;

    for(i = 0; i < buf_len; i++)
        if(buf[i] != content)
        {
            /*
            printf("buf_len:%zu, cmd:%hhu, looper_idx:%u, client_idx:%u, i:%zu, content:%hhu\n",
                   buf_len, cmd, looper_idx, client_idx, i, content);
            for(i = 0; i < buf_len; i++)
                printf("[%02zu]: %hhu\n", i, buf[i]);
            */
            return 1; /* failed */
        }

    return 0; /* OK */
}

static void test_tcp_server_send_response_header(svx_tcp_connection_t *conn,
                                                 uint8_t cmd, uint32_t looper_idx,
                                                 uint32_t client_idx, uint32_t body_len)
{
    test_tcp_proto_header_t header = {cmd, 2, htonl(looper_idx), htonl(client_idx), htonl(body_len)};
    if(svx_tcp_connection_write(conn, (uint8_t *)&header, sizeof(header))) TEST_EXIT;
}

static void test_tcp_server_established_cb(svx_tcp_connection_t *conn, void *arg)
{
    test_tcp_server_ctx_t *ctx;

    if(NULL == (ctx = calloc(1, sizeof(test_tcp_server_ctx_t)))) TEST_EXIT;
    svx_tcp_connection_set_context(conn, ctx);

    if(svx_tcp_connection_disable_write_completed(conn)) TEST_EXIT;
}

static void test_tcp_server_write_completed_cb(svx_tcp_connection_t *conn, void *arg)
{
    test_tcp_server_ctx_t *ctx;
    uint8_t                tmp[TEST_TCP_WRITE_BUF_HIGH_WATER_MARK - 1];
    size_t                 send_len;
    
    svx_tcp_connection_get_context(conn, (void *)&ctx);

    send_len = ((ctx->body_len - ctx->body_idx) > sizeof(tmp) ? sizeof(tmp) : (ctx->body_len - ctx->body_idx));
    if(0 == send_len || ctx->body_idx + send_len == ctx->body_len)
    {
        /* this is the last sending for download, so we disable the write_completed callback */
        if(svx_tcp_connection_disable_write_completed(conn)) TEST_EXIT;        
    }
    if(send_len > 0)
    {
        test_tcp_build_msg_buf(tmp, send_len, ctx->cmd, ctx->looper_idx, ctx->client_idx);
        if(svx_tcp_connection_write(conn, tmp, send_len)) TEST_EXIT;
        ctx->body_idx += send_len;
    }
    if(ctx->body_idx == ctx->body_len)
    {
        /* download finished */
        ctx->cmd = 0; /* finished */
    }
}

static void test_tcp_server_response_upload(void *arg)
{
    svx_tcp_connection_t  *conn = (svx_tcp_connection_t *)arg;
    test_tcp_server_ctx_t *ctx;
    uint8_t                cmd;

    svx_tcp_connection_get_context(conn, (void *)&ctx);

    /* ctx->cmd MUST be set to 0 before "sending upload response to client" */
    cmd = ctx->cmd;
    ctx->cmd = 0; /* finished */
    
    test_tcp_server_send_response_header(conn, cmd, ctx->looper_idx, ctx->client_idx, 0);
    svx_tcp_connection_del_ref(conn);

}

static void test_tcp_server_read_cb(svx_tcp_connection_t *conn, svx_circlebuf_t *buf, void *arg)
{
    test_tcp_server_ctx_t   *ctx;
    test_tcp_proto_header_t  header;
    uint8_t                  tmp[TEST_TCP_READ_BUF_MAX_LEN];
    size_t                   data_len;

    svx_tcp_connection_get_context(conn, (void *)&ctx);

    if(0 == ctx->cmd)
    {
        /* read command header */
        if(svx_circlebuf_get_data(buf, (uint8_t *)&header, sizeof(header))) return; /* have not enough body */
        if(1 != header.type) TEST_EXIT; /* not a request? */
        ctx->cmd        = header.cmd;
        ctx->looper_idx = ntohl(header.looper_idx);
        ctx->client_idx = ntohl(header.client_idx);
        ctx->body_len   = ntohl(header.body_len);
        ctx->body_idx   = 0;
    }

    /* handle the command */
    switch(ctx->cmd)
    {
    case SVX_TEST_TCP_PROTO_CMD_ECHO:
        /* recv echo request (body) */
        if(ctx->body_len != msg_echo[ctx->looper_idx][ctx->client_idx].len) TEST_EXIT;
        if(ctx->body_len > 0)
        {
            if(svx_circlebuf_get_data(buf, tmp, ctx->body_len)) return; /* have not enough body */
            if(test_tcp_check_msg_buf(tmp, ctx->body_len, ctx->cmd, ctx->looper_idx, ctx->client_idx)) TEST_CHECK_FAILED;
        }
        
        /* send echo response */
        test_tcp_server_send_response_header(conn, ctx->cmd, ctx->looper_idx, ctx->client_idx, ctx->body_len);
        if(ctx->body_len > 0)
            if(svx_tcp_connection_write(conn, tmp, ctx->body_len)) TEST_EXIT;
        ctx->cmd = 0; /* finished */
        break;
        
    case SVX_TEST_TCP_PROTO_CMD_UPLOAD:
        /* recv upload request */
        if(ctx->body_len != msg_upload[ctx->looper_idx][ctx->client_idx].len) TEST_EXIT;
        if(svx_circlebuf_get_data_len(buf, &data_len)) TEST_EXIT;
        if(data_len > sizeof(tmp)) TEST_EXIT;
        if(data_len > 0)
        {
            /* read all body from client */
            if(svx_circlebuf_get_data(buf, tmp, data_len)) TEST_EXIT;
            if(test_tcp_check_msg_buf(tmp, data_len, ctx->cmd, ctx->looper_idx, ctx->client_idx)) TEST_CHECK_FAILED;
            ctx->body_idx += data_len;
            if(ctx->body_idx > ctx->body_len) TEST_EXIT;
        }
        
        /* send upload response */
        if(ctx->body_idx == ctx->body_len)
        {
            if(0 == (ctx->looper_idx % 2))
            {
                /* in current thread */
                test_tcp_server_send_response_header(conn, ctx->cmd, ctx->looper_idx, ctx->client_idx, 0);
                ctx->cmd = 0; /* finished */
            }
            else
            {
                /* in a worker thread (only for test) */
                svx_tcp_connection_add_ref(conn);
                if(svx_threadpool_dispatch(test_tcp_server.threadpool, test_tcp_server_response_upload, NULL, conn)) TEST_EXIT;
            }
        }
        break;
        
    case SVX_TEST_TCP_PROTO_CMD_DOWNLOAD:
        /* recv download request */
        if(0 != ctx->body_len) TEST_EXIT;
        ctx->body_len = msg_download[ctx->looper_idx][ctx->client_idx].len;

        /* send download request */
        if(svx_tcp_connection_enable_write_completed(conn)) TEST_EXIT; /* for continuous transmission */
        test_tcp_server_send_response_header(conn, ctx->cmd, ctx->looper_idx, ctx->client_idx, ctx->body_len);
        break;
        
    default:
        TEST_EXIT; /* unknown command */
    }
}

static void test_tcp_server_high_water_mark_cb(svx_tcp_connection_t *conn, size_t water_mark, void *arg)
{
    TEST_EXIT;
}

static void test_tcp_server_exit(void *arg)
{
    if(svx_tcp_server_stop(test_tcp_server.tcp_server)) TEST_EXIT;
    if(svx_looper_quit(test_tcp_server.looper)) TEST_EXIT;
}

static void test_tcp_server_closed_cb(svx_tcp_connection_t *conn, void *arg)
{
    test_tcp_server_ctx_t *ctx;

    svx_tcp_connection_get_context(conn, (void *)&ctx);

    pthread_mutex_lock(&test_tcp_server_closed_conns_mutex);
    
    test_tcp_server_closed_conns++;
    
    /* all loopers for clients are destroyed, so stop the server and quit the server looper */
    if(test_tcp_server_closed_conns == TEST_TCP_CLIENT_LOOPER_CNT * TEST_TCP_CLIENT_CNT_PER_LOOPER)
        if(svx_threadpool_dispatch(test_tcp_server.threadpool, test_tcp_server_exit, NULL, NULL)) TEST_EXIT;

    pthread_mutex_unlock(&test_tcp_server_closed_conns_mutex);

    free(ctx);
}

static void *test_tcp_server_main_thd(void *arg)
{
    svx_inetaddr_t     listen_addr;
    test_tcp_server_t *server = &test_tcp_server;

    /* create thread pool */
    if(svx_threadpool_create(&(server->threadpool), 3, 0)) TEST_EXIT;
    
    /* create looper */
    if(svx_looper_create(&(server->looper))) TEST_EXIT;

    /* create TCP server */
    if(svx_inetaddr_from_ipport(&listen_addr, test_tcp_server.ip, TEST_TCP_LISTEN_PORT)) TEST_EXIT;
    if(svx_tcp_server_create(&(server->tcp_server), server->looper, listen_addr)) TEST_EXIT;
    if(svx_tcp_server_set_io_loopers_num(server->tcp_server, server->io_loopers_num)) TEST_EXIT;
    if(svx_tcp_server_set_keepalive(server->tcp_server, 10, 1, 3)) TEST_EXIT;
    if(svx_tcp_server_set_read_buf_len(server->tcp_server, TEST_TCP_READ_BUF_MIN_LEN, TEST_TCP_READ_BUF_MAX_LEN)) TEST_EXIT;
    if(svx_tcp_server_set_write_buf_len(server->tcp_server, TEST_TCP_WRITE_BUF_MIN_LEN)) TEST_EXIT;
    if(svx_tcp_server_set_established_cb(server->tcp_server, test_tcp_server_established_cb, NULL)) TEST_EXIT;
    if(svx_tcp_server_set_read_cb(server->tcp_server, test_tcp_server_read_cb, NULL)) TEST_EXIT;
    if(svx_tcp_server_set_write_completed_cb(server->tcp_server, test_tcp_server_write_completed_cb, NULL)) TEST_EXIT;
    if(svx_tcp_server_set_high_water_mark_cb(server->tcp_server, test_tcp_server_high_water_mark_cb, NULL, TEST_TCP_WRITE_BUF_HIGH_WATER_MARK)) TEST_EXIT;
    if(svx_tcp_server_set_closed_cb(server->tcp_server, test_tcp_server_closed_cb, NULL)) TEST_EXIT;

    /* start TCP server */
    if(svx_tcp_server_start(server->tcp_server)) TEST_EXIT;
    
    /* start looper (blocked here until svx_looper_quit()) */
    if(svx_looper_loop(server->looper)) TEST_EXIT;
    
    /* clean everything*/
    if(svx_tcp_server_destroy(&(server->tcp_server))) TEST_EXIT;
    if(svx_looper_destroy(&(server->looper))) TEST_EXIT;
    if(svx_threadpool_destroy(&(server->threadpool))) TEST_EXIT;

    return NULL;
}

static void test_tcp_client_send_request_header(svx_tcp_connection_t *conn,
                                                uint8_t cmd, uint32_t looper_idx,
                                                uint32_t client_idx, uint32_t body_len)
{
    test_tcp_proto_header_t header = {cmd, 1, htonl(looper_idx), htonl(client_idx), htonl(body_len)};
    if(svx_tcp_connection_write(conn, (uint8_t *)&header, sizeof(header))) TEST_EXIT;
}

static void test_tcp_client_send_echo_request(svx_tcp_connection_t *conn, test_tcp_client_ctx_t *ctx,
                                              test_tcp_client_info_t *client_info)
{
    uint8_t tmp[TEST_TCP_READ_BUF_MAX_LEN];

    ctx->cmd_send = SVX_TEST_TCP_PROTO_CMD_ECHO;
    ctx->body_len = msg_echo[client_info->looper_idx][client_info->client_idx].len;
    ctx->body_idx = 0;

    test_tcp_client_send_request_header(conn, ctx->cmd_send, client_info->looper_idx, client_info->client_idx, ctx->body_len);
    if(ctx->body_len > 0)
    {
        test_tcp_build_msg_buf(tmp, ctx->body_len, ctx->cmd_send, client_info->looper_idx, client_info->client_idx);
        if(svx_tcp_connection_write(conn, tmp, ctx->body_len)) TEST_EXIT;
    }
}

static void test_tcp_client_send_upload_request(svx_tcp_connection_t *conn, test_tcp_client_ctx_t *ctx,
                                                test_tcp_client_info_t *client_info)
{
    ctx->cmd_send = SVX_TEST_TCP_PROTO_CMD_UPLOAD;
    ctx->body_len = msg_upload[client_info->looper_idx][client_info->client_idx].len;
    ctx->body_idx = 0;

    /* printf("[%d][%d] send upload request, body_len:%u\n", client_info->looper_idx, client_info->client_idx, ctx->body_len); */
    test_tcp_client_send_request_header(conn, ctx->cmd_send, client_info->looper_idx, client_info->client_idx, ctx->body_len);
    /* all upload body(data) will be send in the write_completed_cb()...... */
}

static void test_tcp_client_send_download_request(svx_tcp_connection_t *conn, test_tcp_client_ctx_t *ctx,
                                                  test_tcp_client_info_t *client_info)
{
    ctx->cmd_send = SVX_TEST_TCP_PROTO_CMD_DOWNLOAD;
    ctx->body_len = 0;
    ctx->body_idx = 0;
    test_tcp_client_send_request_header(conn, ctx->cmd_send, client_info->looper_idx, client_info->client_idx, ctx->body_len);
}

static void test_tcp_client_request_download(void *arg)
{
    test_tcp_client_worker_thread_param_t *p = (test_tcp_client_worker_thread_param_t *)arg;
    
    test_tcp_client_send_download_request(p->conn, p->ctx, &(p->client_info));
    svx_tcp_connection_del_ref(p->conn);
    free(arg);
}

static void test_tcp_client_established_cb(svx_tcp_connection_t *conn, void *arg)
{
    test_tcp_client_info_t *client_info = (test_tcp_client_info_t *)arg;
    test_tcp_client_ctx_t  *ctx;

    if(NULL == (ctx = calloc(1, sizeof(test_tcp_client_ctx_t)))) TEST_EXIT;
    svx_tcp_connection_set_context(conn, ctx);

    if(svx_tcp_connection_disable_write_completed(conn)) TEST_EXIT;

    ctx->cmd_round_total = TEST_TCP_CLIENT_ROUND_PER_CLIENT;
    ctx->cmd_round_cur   = 0; /* first round */
    ctx->cmd_recv        = 0;

    test_tcp_client_send_echo_request(conn, ctx, client_info);
}

static void test_tcp_client_write_completed_cb(svx_tcp_connection_t *conn, void *arg)
{
    test_tcp_client_info_t *client_info = (test_tcp_client_info_t *)arg;
    test_tcp_client_ctx_t  *ctx;
    uint8_t                 tmp[TEST_TCP_WRITE_BUF_HIGH_WATER_MARK - 1];
    size_t                  send_len;

    svx_tcp_connection_get_context(conn, (void *)&ctx);

    send_len = ((ctx->body_len - ctx->body_idx) > sizeof(tmp) ? sizeof(tmp) : (ctx->body_len - ctx->body_idx));
    if(0 == send_len || ctx->body_idx + send_len == ctx->body_len)
    {
        /* printf("[%d][%d] send upload request send_len:%zu (%u/%u) [last]\n", client_info->looper_idx, client_info->client_idx, send_len, ctx->body_idx, ctx->body_len); */
        /* this is the last sending for upload, so we disable the write_completed callback */
        if(svx_tcp_connection_disable_write_completed(conn)) TEST_EXIT;
    }
    if(send_len > 0)
    {
        /* printf("[%d][%d] send upload request send_len:%zu (%u/%u)\n", client_info->looper_idx, client_info->client_idx, send_len, ctx->body_idx, ctx->body_len); */
        test_tcp_build_msg_buf(tmp, send_len, ctx->cmd_send, client_info->looper_idx, client_info->client_idx);
        if(svx_tcp_connection_write(conn, tmp, send_len)) TEST_EXIT;
        ctx->body_idx += send_len;
    }
}

static void test_tcp_client_read_cb(svx_tcp_connection_t *conn, svx_circlebuf_t *buf, void *arg)
{
    test_tcp_client_info_t                *client_info = (test_tcp_client_info_t *)arg;
    test_tcp_client_ctx_t                 *ctx;
    test_tcp_proto_header_t                header;
    uint8_t                                tmp[TEST_TCP_READ_BUF_MAX_LEN];
    size_t                                 data_len;
    test_tcp_client_worker_thread_param_t *param = NULL;

    svx_tcp_connection_get_context(conn, (void *)&ctx);

    if(0 == ctx->cmd_recv)
    {
        /* read command header */
        if(svx_circlebuf_get_data(buf, (uint8_t *)&header, sizeof(header))) return; /* have not enough body */
        if(2 != header.type) TEST_EXIT; /* not a response? */
        ctx->cmd_recv = header.cmd;
        if(ctx->cmd_recv != ctx->cmd_send) TEST_EXIT; /* command mismatch */
        if(client_info->looper_idx != ntohl(header.looper_idx)) TEST_EXIT; /* looper index mismatch */
        if(client_info->client_idx != ntohl(header.client_idx)) TEST_EXIT; /* client index mismatch */
        ctx->body_len = ntohl(header.body_len);
        ctx->body_idx = 0;
    }

    /* handle the command */
    switch(ctx->cmd_recv)
    {
    case SVX_TEST_TCP_PROTO_CMD_ECHO:
        /* recv echo response (body) */
        if(ctx->body_len > 0)
        {
            if(svx_circlebuf_get_data(buf, tmp, ctx->body_len)) return; /* have not enough body */
            if(test_tcp_check_msg_buf(tmp, ctx->body_len, ctx->cmd_recv, client_info->looper_idx, client_info->client_idx)) TEST_CHECK_FAILED;
        }
        ctx->cmd_recv = 0;
        
        /* send upload request */
        if(svx_tcp_connection_enable_write_completed(conn)) TEST_EXIT; /* for continuous transmission */
        test_tcp_client_send_upload_request(conn, ctx, client_info);
        break;
        
    case SVX_TEST_TCP_PROTO_CMD_UPLOAD:
        /* recv upload response (no body) */
        if(0 != ctx->body_len) TEST_EXIT;
        ctx->cmd_recv = 0;
        
        /* send download request */
        /* printf("[%d][%d] send download request\n", client_info->looper_idx, client_info->client_idx); */
        if(0 == (client_info->client_idx % 2))
        {
            /* in current thread */
            test_tcp_client_send_download_request(conn, ctx, client_info);
        }
        else
        {            
            /* in another worker thread (only for test) */
            if(NULL == (param = malloc(sizeof(test_tcp_client_worker_thread_param_t)))) TEST_EXIT;
            param->conn        = conn;
            param->ctx         = ctx;
            param->client_info = *client_info;
            svx_tcp_connection_add_ref(conn);
            if(svx_threadpool_dispatch(test_tcp_clients[client_info->looper_idx].threadpool, test_tcp_client_request_download, NULL, param)) TEST_EXIT;
        }
        break;
        
    case SVX_TEST_TCP_PROTO_CMD_DOWNLOAD:
        /* recv download response (body) */
        if(ctx->body_len != msg_download[client_info->looper_idx][client_info->client_idx].len) TEST_EXIT;
        if(svx_circlebuf_get_data_len(buf, &data_len)) TEST_EXIT;
        if(data_len > sizeof(tmp)) TEST_EXIT;
        if(0 == ctx->body_idx && 0 == (client_info->client_idx % 2))
            usleep(100 * 1000); /* fill the server-side OS'send buffer (only for test) */
        if(data_len > 0)
        {
            /* read all body from client */
            if(svx_circlebuf_get_data(buf, tmp, data_len)) TEST_EXIT;
            if(test_tcp_check_msg_buf(tmp, data_len, ctx->cmd_recv, client_info->looper_idx, client_info->client_idx)) TEST_CHECK_FAILED;
            ctx->body_idx += data_len;
            if(ctx->body_idx > ctx->body_len) TEST_EXIT;
        }
        if(ctx->body_idx == ctx->body_len)
        {
            /* download finished */
            ctx->cmd_recv = 0;
            ctx->cmd_round_cur++;

            /* send echo request (next round) or close conn */
            if(ctx->cmd_round_cur < ctx->cmd_round_total)
            {
                /* send download request (next round) */
                test_tcp_client_send_echo_request(conn, ctx, client_info);
            }
            else
            {
                /* close conn */
                if(0 == (client_info->client_idx % 3))
                {
                    if(svx_tcp_connection_shutdown_wr(conn)) TEST_EXIT;
                }
                else
                {
                    if(svx_tcp_connection_close(conn)) TEST_EXIT;
                }
            }
        }
        break;
        
    default:
        TEST_EXIT; /* unknown command */
    }
}

static void test_tcp_client_high_water_mark_cb(svx_tcp_connection_t *conn, size_t water_mark, void *arg)
{
    TEST_EXIT;
}

static void test_tcp_client_closed_cb(svx_tcp_connection_t *conn, void *arg)
{
    test_tcp_client_info_t *client_info = (test_tcp_client_info_t *)arg;
    test_tcp_client_ctx_t  *ctx;

    svx_tcp_connection_get_context(conn, (void *)&ctx);

    test_tcp_clients[client_info->looper_idx].tcp_clients_alive_cnt--;
    if(0 == test_tcp_clients[client_info->looper_idx].tcp_clients_alive_cnt)
    {
        /* all clients closed, quit the looper for these clients */
        if(svx_looper_quit(test_tcp_clients[client_info->looper_idx].looper)) TEST_EXIT;
    }

    free(ctx);
    free(client_info);
}

static void *test_tcp_client_main_thd(void *arg)
{
    int                     looper_idx = (int)((intptr_t)arg);
    svx_inetaddr_t          listen_addr;
    test_tcp_client_t      *client = &(test_tcp_clients[looper_idx]);
    test_tcp_client_info_t *client_info;
    int                     i;

    /* create thread pool */
    if(svx_threadpool_create(&(client->threadpool), 3, 0)) TEST_EXIT;

    /* create looper */
    if(svx_looper_create(&(client->looper))) TEST_EXIT;

    if(svx_inetaddr_from_ipport(&listen_addr, test_tcp_server.ip, TEST_TCP_LISTEN_PORT)) TEST_EXIT;
    if(NULL == (client->tcp_client = (svx_tcp_client_t **)malloc(sizeof(svx_tcp_client_t *) * TEST_TCP_CLIENT_CNT_PER_LOOPER))) TEST_EXIT;
    client->tcp_clients_alive_cnt = TEST_TCP_CLIENT_CNT_PER_LOOPER;
    for(i = 0; i < TEST_TCP_CLIENT_CNT_PER_LOOPER; i++)
    {
        if(NULL == (client_info = malloc(sizeof(test_tcp_client_info_t)))) TEST_EXIT;
        client_info->looper_idx = looper_idx;
        client_info->client_idx = i;
        
        /* create TCP client */
        if(svx_tcp_client_create(&(client->tcp_client[i]), client->looper, listen_addr)) TEST_EXIT;
        if(svx_tcp_client_set_reconnect_delay(client->tcp_client[i], 300, 5000)) TEST_EXIT;
        if(svx_tcp_client_set_read_buf_len(client->tcp_client[i], TEST_TCP_READ_BUF_MIN_LEN, TEST_TCP_READ_BUF_MAX_LEN)) TEST_EXIT;
        if(svx_tcp_client_set_write_buf_len(client->tcp_client[i], TEST_TCP_WRITE_BUF_MIN_LEN)) TEST_EXIT;
        if(svx_tcp_client_set_established_cb(client->tcp_client[i], test_tcp_client_established_cb, client_info)) TEST_EXIT;
        if(svx_tcp_client_set_read_cb(client->tcp_client[i], test_tcp_client_read_cb, client_info)) TEST_EXIT;
        if(svx_tcp_client_set_write_completed_cb(client->tcp_client[i], test_tcp_client_write_completed_cb, client_info)) TEST_EXIT;
        if(svx_tcp_client_set_high_water_mark_cb(client->tcp_client[i], test_tcp_client_high_water_mark_cb, client_info, TEST_TCP_WRITE_BUF_HIGH_WATER_MARK)) TEST_EXIT;
        if(svx_tcp_client_set_closed_cb(client->tcp_client[i], test_tcp_client_closed_cb, client_info)) TEST_EXIT;

        /* connect to server */
        if(svx_tcp_client_connect(client->tcp_client[i])) TEST_EXIT;
    }

    /* loop (blocked here until svx_looper_quit()) */
    if(svx_looper_loop(client->looper)) TEST_EXIT;

    /* clean everything */
    for(i = 0; i < TEST_TCP_CLIENT_CNT_PER_LOOPER; i++)
        if(svx_tcp_client_destroy(&(client->tcp_client[i]))) TEST_EXIT;
    free(client->tcp_client);
    if(svx_looper_destroy(&(client->looper))) TEST_EXIT;
    if(svx_threadpool_destroy(&(client->threadpool))) TEST_EXIT;

    pthread_mutex_lock(&test_tcp_clients_alive_cnt_mutex);
    test_tcp_clients_alive_cnt--;
    pthread_mutex_unlock(&test_tcp_clients_alive_cnt_mutex);
    
    return NULL;
}

static void test_tcp_do(const char *tcp_server_ip, int tcp_server_io_loopers_num)
{
    int i;

    test_tcp_server.ip             = tcp_server_ip;
    test_tcp_server.io_loopers_num = tcp_server_io_loopers_num;
    test_tcp_server_closed_conns   = 0;
    test_tcp_clients_alive_cnt     = TEST_TCP_CLIENT_LOOPER_CNT;

    /* a half of clients started before server started */
    for(i = 0; i < (TEST_TCP_CLIENT_LOOPER_CNT / 2); i++)
        if(pthread_create(&(test_tcp_clients[i].tid), NULL, &test_tcp_client_main_thd, (void *)((intptr_t)i))) TEST_EXIT;
    usleep(50 * 1000);

    /* server started */
    if(pthread_create(&(test_tcp_server.tid), NULL, &test_tcp_server_main_thd, NULL)) TEST_EXIT;
    usleep(50 * 1000);

    /* the other half of clients started after server started */
    for(; i < TEST_TCP_CLIENT_LOOPER_CNT; i++)
        if(pthread_create(&(test_tcp_clients[i].tid), NULL, &test_tcp_client_main_thd, (void *)((intptr_t)i))) TEST_EXIT;

    /* join all client and server threads */
    for(i = 0; i < TEST_TCP_CLIENT_LOOPER_CNT; i++)
        if(pthread_join(test_tcp_clients[i].tid, NULL)) TEST_EXIT;
    if(pthread_join(test_tcp_server.tid, NULL)) TEST_EXIT;

    /* final check */
    if(test_tcp_server_closed_conns != TEST_TCP_CLIENT_LOOPER_CNT * TEST_TCP_CLIENT_CNT_PER_LOOPER) TEST_EXIT;
    if(0 != test_tcp_clients_alive_cnt) TEST_EXIT;
    for(i = 0; i < TEST_TCP_CLIENT_LOOPER_CNT; i++)
        if(0 != test_tcp_clients[i].tcp_clients_alive_cnt) TEST_EXIT;
}

int test_tcp_runner()
{
    int            i, j;
    struct timeval tv;
    long           rand;

    svx_log_level_stdout = SVX_LOG_LEVEL_WARNING;

    gettimeofday(&tv, NULL);
    rand = tv.tv_sec + tv.tv_usec;
    
    /* load some random data */
    for(i = 0; i < TEST_TCP_CLIENT_LOOPER_CNT; i++)
    {
        for(j = 0; j < TEST_TCP_CLIENT_CNT_PER_LOOPER; j++)
        {
            msg_echo[i][j].content     = (uint8_t)(labs(random() + rand) % UINT8_MAX);
            msg_upload[i][j].content   = (uint8_t)(labs(random() + rand) % UINT8_MAX);
            msg_download[i][j].content = (uint8_t)(labs(random() + rand) % UINT8_MAX);
            
            msg_echo[i][j].len     = (size_t)(labs(random() + rand) % TEST_TCP_SMALL_BODY_MAX_LEN);
            msg_upload[i][j].len   = (size_t)(labs(random() + rand) % TEST_TCP_LARGE_BODY_MAX_LEN);

            if(0 == i && 0 == j)
                msg_download[i][j].len = TEST_TCP_LARGE_BODY_MAX_LEN - 1; /* make sure have one big buffer */
            else
                msg_download[i][j].len = (size_t)(labs(random() + rand) % TEST_TCP_LARGE_BODY_MAX_LEN);

            /*
            printf("[%d][%d] c1:%hhu, c2:%hhu, c3:%hhu, len1:%zu, len2:%zu, len3:%zu\n",
                   i, j, msg_echo[i][j].content, msg_upload[i][j].content, msg_download[i][j].content,
                   msg_echo[i][j].len, msg_upload[i][j].len, msg_download[i][j].len);
            */
        }
    }

    test_tcp_do(TEST_TCP_LISTEN_IPV4, 0);
    test_tcp_do(TEST_TCP_LISTEN_IPV4, 2);
    test_tcp_do(TEST_TCP_LISTEN_IPV6, 0);
    test_tcp_do(TEST_TCP_LISTEN_IPV6, 2);

    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    return 0;
}
