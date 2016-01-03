/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include "svx_util.h"
#include "svx_looper.h"
#include "svx_channel.h"
#include "svx_inetaddr.h"
#include "svx_icmp.h"
#include "svx_udp.h"
#include "svx_log.h"

#define TEST_EXIT do {SVX_LOG_ERR("exit(1). line: %d. errno:%d\n", __LINE__, errno); exit(1);} while(0)

static svx_looper_t   *test_icmp_looper;
static svx_icmp_t     *test_icmp_icmp;
static svx_inetaddr_t  test_icmp_addr_echo;
static svx_inetaddr_t  test_icmp_addr_unreach;
static int             test_icmp_echoreply_recvived;
static int             test_icmp_unreach_port_recvived;

void test_icmp_echoreply_cb(svx_inetaddr_t addr, void *arg)
{
    SVX_UTIL_UNUSED(arg);
    
    if(0 == svx_inetaddr_cmp_ip(&test_icmp_addr_echo, &addr))
        test_icmp_echoreply_recvived = 1;

    if(1 == test_icmp_unreach_port_recvived)
        if(svx_looper_quit(test_icmp_looper)) TEST_EXIT;
}

void test_icmp_unreach_port_cb(svx_inetaddr_t addr, void *arg)
{
    SVX_UTIL_UNUSED(arg);
    
    if(0 == svx_inetaddr_cmp_addr(&test_icmp_addr_unreach, &addr))
        test_icmp_unreach_port_recvived = 1;
        
    if(1 == test_icmp_echoreply_recvived)
        if(svx_looper_quit(test_icmp_looper)) TEST_EXIT;
}

void *test_icmp_thd(void *arg)
{
    int fd;

    SVX_UTIL_UNUSED(arg);
    
    /* send ICMP ECHO */
    if(svx_icmp_send_echo(test_icmp_icmp, &test_icmp_addr_echo)) TEST_EXIT;

    /* send a package via UDP to a unreachable address */
    if(0 != svx_udp_client(&fd, NULL, NULL, SVX_INETADDR_FAMILY(&test_icmp_addr_unreach))) TEST_EXIT;
    if(3 != sendto(fd, "123", 3, 0, &(test_icmp_addr_unreach.storage.addr), SVX_INETADDR_LEN(&test_icmp_addr_unreach))) TEST_EXIT;
    if(close(fd)) TEST_EXIT;

    return NULL;
}

void test_icmp_do(const char *ip, uint16_t port)
{
    pthread_t tid;

    test_icmp_echoreply_recvived = 0;
    test_icmp_unreach_port_recvived = 0;

    if(svx_inetaddr_from_ipport(&test_icmp_addr_echo, ip, 0)) TEST_EXIT;
    if(svx_inetaddr_from_ipport(&test_icmp_addr_unreach, ip, port)) TEST_EXIT;    
    if(svx_looper_create(&test_icmp_looper)) TEST_EXIT;
    if(svx_icmp_create(&test_icmp_icmp, test_icmp_looper)) TEST_EXIT;
    if(svx_icmp_set_echoreply_cb(test_icmp_icmp, test_icmp_echoreply_cb, NULL)) TEST_EXIT;
    if(svx_icmp_set_unreach_port_cb(test_icmp_icmp, test_icmp_unreach_port_cb, NULL)) TEST_EXIT;
    if(svx_icmp_start(test_icmp_icmp)) TEST_EXIT;

    if(pthread_create(&tid, NULL, &test_icmp_thd, NULL)) TEST_EXIT;

    if(svx_looper_loop(test_icmp_looper)) TEST_EXIT;
    
    if(pthread_join(tid, NULL)) TEST_EXIT;
    if(svx_icmp_destroy(&test_icmp_icmp)) TEST_EXIT;
    if(svx_looper_destroy(&test_icmp_looper)) TEST_EXIT;

    /* check */
    if(0 == test_icmp_echoreply_recvived) TEST_EXIT;
    if(0 == test_icmp_unreach_port_recvived) TEST_EXIT;
}

int test_icmp_runner()
{
    if(!svx_util_is_root())
    {
        printf("\n\n\n***** THE ICMP TEST NEED ROOT PRIVILEGES *****\n\n\n");
        return 1;
    }
    
    test_icmp_do("127.0.0.1", 54321);
    test_icmp_do("::1", 54321);
    
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    return 0;
}
