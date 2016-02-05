/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/* This is a simplest HTTP server only for benchmark test. */

#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "svx_tcp_server.h"
#include "svx_process.h"
#include "svx_util.h"
#include "svx_log.h"

/* The HTTP server object */
static svx_tcp_server_t *server = NULL;

/* The callback to handle TCP connection established */
static void server_established_cb(svx_tcp_connection_t *conn, void *arg)
{
    SVX_UTIL_UNUSED(arg);

    /* In this simple program, although we always send all the response data in a single writing,
       but we still set the TCP_NODELAY. To be more close to the real world HTTP server. */
    if(svx_tcp_connection_set_nodelay(conn, 1)) exit(1);
}

/* The callback to handle HTTP request */
static void server_read_cb(svx_tcp_connection_t *conn, svx_circlebuf_t *buf, void *arg)
{
    SVX_UTIL_UNUSED(arg);
    
    /* Get the current HTTP request. */
    char   req[1024];
    size_t req_len;
    if(svx_circlebuf_get_data_by_ending(buf, (const uint8_t *)"\r\n\r\n", 4,
                                        (uint8_t *)req, sizeof(req), &req_len)) return;

    /* We only deal with the /hello for test. */
    if(0 != memcmp(req, "GET /hello HTTP/1.", 18))
        svx_tcp_connection_shutdown_wr(conn);

    /* If we should keep the TCP connection? */
    int keep_alive = 1;
    if(strcasestr(req, "Connection: close\r\n") ||
       (!strcasestr(req, "Connection: keep-alive") && ('0' == req[18])/* HTTP 1.0 */))
        keep_alive = 0;

    /* Build the HTTP response. */
    char   resp[1024];
    size_t resp_len = snprintf(resp, sizeof(resp),
                               "HTTP/1.1 200 OK\r\n"
                               "Server: libsvx-simplest-HTTP-server\r\n"
                               "Content-Type: text/plain\r\n"
                               "Content-Length: 12\r\n"
                               "Connection: %s\r\n"
                               "\r\n"
                               "Hello world!",
                               keep_alive ? "keep-alive" : "close");

    /* Wirte the HTTP response to client. */
    svx_tcp_connection_write(conn, (const uint8_t *)resp, resp_len);

    /* Close the TCP connection. */
    if(!keep_alive) svx_tcp_connection_shutdown_wr(conn);
}

static int server_start(svx_looper_t *looper, void *arg)
{
    SVX_UTIL_UNUSED(arg);

    /* Init an address for HTTP server. */
    svx_inetaddr_t listen_addr;
    if(svx_inetaddr_from_ipport(&listen_addr, "0.0.0.0", 8080)) exit(1);

    /* Create the HTTP server. */
    if(svx_tcp_server_create(&server, looper, listen_addr)) exit(1);

    /* Set the server's I/O mode.
     *
     * 0 : No worker-thread will created. (all I/O in one thread)
     * N : Create N worker-thread for I/O.
     * */
    if(svx_tcp_server_set_io_loopers_num(server, 0)) exit(1);

    /* Set TCP keep-alive for discarding timeout connections. */
    if(svx_tcp_server_set_keepalive(server, 5, 1, 3)) exit(1);

    /* Set a callback fucntion for handling TCP connection established */
    if(svx_tcp_server_set_established_cb(server, server_established_cb, NULL)) exit(1);

    /* Set a callback function for handling HTTP request. */
    if(svx_tcp_server_set_read_cb(server, server_read_cb, NULL)) exit(1);

    /* Start the HTTP server. */
    if(svx_tcp_server_start(server)) exit(1);
    
    return 0;
}

int server_stop(svx_looper_t *looper, void *arg)
{
    SVX_UTIL_UNUSED(looper);    
    SVX_UTIL_UNUSED(arg);
    
    /* Stop the HTTP server. */
    if(svx_tcp_server_destroy(&server)) exit(1);
    
    return 0;
}

int main(int argc, char **argv)
{
    /* Get the command line args. */
    const char *command;
    if(1 == argc) command = "start";
    else if(2 == argc) command = argv[1];
    else exit(1);

    /* Set a large MAX FDs for benchmarking test. */
    if(svx_process_set_maxfds(102400)) exit(1);

    /* Set the "start" and "stop" callback for the process. */
    if(svx_process_set_callbacks(server_start, NULL,
                                 NULL, NULL,
                                 server_stop, NULL)) exit(1);

    /* Run */
    if(!strcmp(command, "start")) /* start */
    {
        if(svx_process_start()) exit(1);
    }
    else if(!strcmp(command, "stop")) /* stop */
    {
        if(svx_process_stop()) exit(1);
    }
    else if(!strcmp(command, "kill")) /* kill (force stop) */
    {
        if(svx_process_force_stop()) exit(1);
    }
    else if(!strcmp(command, "status")) /* show the status */
    {
        printf("%s is %sRUNNING\n", argv[0],
               svx_process_is_running() ? "" : "NOT ");
    }
    else exit(1);
    
    return 0;
}
