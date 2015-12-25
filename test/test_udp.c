/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#define _GNU_SOURCE
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
#include <net/if.h>
#include <ifaddrs.h>
#include "svx_looper.h"
#include "svx_channel.h"
#include "svx_inetaddr.h"
#include "svx_udp.h"
#include "svx_log.h"

#define TEST_UDP_READ_BUF_LEN  10240
#define TEST_UDP_WRITE_BUF_LEN 10240
#define TEST_UDP_PKG_MAX_LEN   1024
#define TEST_UDP_LOOP_ROUND    10

#define TEST_EXIT do {SVX_LOG_ERR("exit(1). line: %d. errno:%d\n", __LINE__, errno); exit(1);} while(0)

typedef struct
{
    const char   *server_ip;
    uint16_t      server_port;
    const char   *client_ip;
    uint16_t      client_port;
    unsigned int  ifindex;
} test_udp_addr;

static svx_looper_t *looper = NULL;

static void test_udp_server_read_callback(void *arg)
{
    int            fd = (int)((intptr_t)arg);
    svx_inetaddr_t peer_addr;
    socklen_t      peer_addr_len = sizeof(peer_addr);
    uint8_t        buf[TEST_UDP_PKG_MAX_LEN];
    ssize_t        len;

    if(0 > (len = recvfrom(fd, buf, sizeof(buf), 0, &(peer_addr.storage.addr), &peer_addr_len))) TEST_EXIT;
    if(len != sendto(fd, buf, len, 0, &(peer_addr.storage.addr), peer_addr_len)) TEST_EXIT;
}

static void *test_udp_server_thd(void *arg)
{
    test_udp_addr *addr = (test_udp_addr *)arg;
    int            fd;
    svx_inetaddr_t server_addr;
    svx_inetaddr_t client_addr;
    svx_channel_t *channel;
    
    if(svx_looper_create(&looper)) TEST_EXIT;
    if(svx_inetaddr_from_ipport(&server_addr, addr->server_ip, addr->server_port)) TEST_EXIT;
    if(svx_udp_server(&fd, &server_addr)) TEST_EXIT;
    if(SVX_INETADDR_IS_IP_MULTICAST(&server_addr)) //for multicast
    {
        if(NULL == addr->client_ip)
        {
            if(svx_udp_mcast_join(fd, &server_addr, addr->ifindex)) TEST_EXIT;
        }
        else
        {
            if(svx_inetaddr_from_ipport(&client_addr, addr->client_ip, addr->client_port)) TEST_EXIT;
            if(svx_udp_mcast_join_source_group(fd, &client_addr, &server_addr, addr->ifindex)) TEST_EXIT;
        }
    }
    if(svx_udp_set_kernel_read_buf_len(fd, TEST_UDP_READ_BUF_LEN)) TEST_EXIT;
    if(svx_udp_set_kernel_write_buf_len(fd, TEST_UDP_WRITE_BUF_LEN)) TEST_EXIT;
    if(svx_channel_create(&channel, looper, fd, SVX_CHANNEL_EVENT_READ)) TEST_EXIT;
    if(svx_channel_set_read_callback(channel, test_udp_server_read_callback, (void *)((intptr_t)fd))) TEST_EXIT;

    if(svx_looper_loop(looper)) TEST_EXIT;

    if(svx_channel_destroy(&channel)) TEST_EXIT;
    if(SVX_INETADDR_IS_IP_MULTICAST(&server_addr)) //for multicast
    {
        if(NULL == addr->client_ip)
        {
            if(svx_udp_mcast_leave(fd, &server_addr, addr->ifindex)) TEST_EXIT;
        }
        else
        {
            if(svx_udp_mcast_leave_source_group(fd, &client_addr, &server_addr, addr->ifindex)) TEST_EXIT;
        }
    }
    if(close(fd)) TEST_EXIT;
    if(svx_looper_destroy(&looper)) TEST_EXIT;

    return NULL;
}

static void *test_udp_client_thd(void *arg)
{
    test_udp_addr *addr = (test_udp_addr *)arg;
    int            fd;
    svx_inetaddr_t server_addr;
    svx_inetaddr_t client_addr;
    uint8_t        buf[TEST_UDP_PKG_MAX_LEN];
    size_t         len;
    int            i;

    memset(buf, 'a', sizeof(buf));

    if(svx_inetaddr_from_ipport(&server_addr, addr->server_ip, addr->server_port)) TEST_EXIT;
    if(!SVX_INETADDR_IS_IP_MULTICAST(&server_addr))
    {
        if(0 != svx_udp_client(&fd, NULL, NULL, SVX_INETADDR_FAMILY(&server_addr))) TEST_EXIT;
    }
    else //for multicast
    {
        if(NULL == addr->client_ip)
        {
            if(0 != svx_udp_client(&fd, NULL, NULL, SVX_INETADDR_FAMILY(&server_addr))) TEST_EXIT;
        }
        else
        {
            if(svx_inetaddr_from_ipport(&client_addr, addr->client_ip, addr->client_port)) TEST_EXIT;
            if(0 != svx_udp_client(&fd, NULL, &client_addr, -1)) TEST_EXIT;
        }
        if(0 != svx_udp_mcast_set_if(fd, addr->ifindex)) TEST_EXIT;
    }

    for(i = 0; i < TEST_UDP_LOOP_ROUND; i++)
    {
        len = (size_t)((labs(random()) % TEST_UDP_PKG_MAX_LEN) + 1);
        if(len != sendto(fd, buf, len, 0, &(server_addr.storage.addr), SVX_INETADDR_LEN(&server_addr))) TEST_EXIT;
        if(len != recvfrom(fd, buf, sizeof(buf), 0, NULL, NULL)) TEST_EXIT;
    }

    if(close(fd)) TEST_EXIT;

    if(svx_looper_quit(looper)) TEST_EXIT;

    return NULL;
}

static void test_udp_do(test_udp_addr *addr)
{
    pthread_t server_tid, client_tid;
    
    if(pthread_create(&server_tid, NULL, &test_udp_server_thd, (void *)addr)) TEST_EXIT;
    usleep(300 * 1000);
    if(pthread_create(&client_tid, NULL, &test_udp_client_thd, (void *)addr)) TEST_EXIT;

    if(pthread_join(server_tid, NULL)) TEST_EXIT;
    if(pthread_join(client_tid, NULL)) TEST_EXIT;
}

static int test_udp_interface_filter(struct ifaddrs *ifaddr)
{
    if(ifaddr->ifa_flags & IFF_LOOPBACK) return 1;
    if(!(ifaddr->ifa_flags & IFF_UP)) return 1;
    if(!(ifaddr->ifa_flags & IFF_RUNNING)) return 1;
    if(!(ifaddr->ifa_flags & IFF_MULTICAST)) return 1;
    if(!ifaddr->ifa_addr) return 1;
    if(AF_INET != ifaddr->ifa_addr->sa_family && AF_INET6 != ifaddr->ifa_addr->sa_family) return 1;
    if(AF_INET6 == ifaddr->ifa_addr->sa_family &&
       !IN6_IS_ADDR_LINKLOCAL(&(((struct sockaddr_in6 *)(ifaddr->ifa_addr))->sin6_addr))) return 1;

    return 0;
}

static int test_udp_interface_selector(char *ifname, size_t ifname_len, char *ipv4, size_t ipv4_len,
                                       char *ipv6, size_t ipv6_len)
{
    struct ifaddrs *ifaddr, *i, *j;
    svx_inetaddr_t  addr;
    int             r = 1;

    if(0 != getifaddrs(&ifaddr)) TEST_EXIT;
    for(i = ifaddr; i; i = i->ifa_next)
    {
        if(test_udp_interface_filter(i)) continue;

        for(j = i->ifa_next; j; j = j->ifa_next)
        {
            if(test_udp_interface_filter(j)) continue;
            
            if(0 == strcmp(i->ifa_name, j->ifa_name) &&
               i->ifa_addr->sa_family != j->ifa_addr->sa_family)
            {
                strncpy(ifname, i->ifa_name, ifname_len);
                
                if(svx_inetaddr_from_addr(&addr, i->ifa_addr)) TEST_EXIT;
                if(svx_inetaddr_get_ipport(&addr, SVX_INETADDR_IS_IPV4(&addr) ? ipv4 : ipv6,
                                           SVX_INETADDR_IS_IPV4(&addr) ? ipv4_len : ipv6_len, NULL)) TEST_EXIT;
                
                if(svx_inetaddr_from_addr(&addr, j->ifa_addr)) TEST_EXIT;
                if(svx_inetaddr_get_ipport(&addr, SVX_INETADDR_IS_IPV4(&addr) ? ipv4 : ipv6,
                                           SVX_INETADDR_IS_IPV4(&addr) ? ipv4_len : ipv6_len, NULL)) TEST_EXIT;

                r = 0;
                goto end;
            }
        }
    }
 end:
    freeifaddrs(ifaddr);
    return r;
}

int test_udp_runner()
{
    test_udp_addr addr;
    char ifname[IF_NAMESIZE + 1];
    char ipv4[SVX_INETADDR_STR_IP_LEN];
    char ipv6[SVX_INETADDR_STR_IP_LEN];
    char ipv6_grp[SVX_INETADDR_STR_IP_LEN];

    if(test_udp_interface_selector(ifname, sizeof(ifname), ipv4, sizeof(ipv4), ipv6, sizeof(ipv6)))
    {
        SVX_LOG_ERR("UDP test failed.\n"
                    "We need an interface which support the following features:\n"
                    "1. IPv4 address (unicast & multicast)\n"
                    "2. IPv6 link-local address (unicast & multicast)\n");
        exit(1);
    }

    addr.ifindex = if_nametoindex(ifname);
    snprintf(ipv6_grp, sizeof(ipv6_grp), "ff02::7:8:9%%%s", ifname);

    /* unicast, IPv4 */
    addr.server_ip   = "127.0.0.1";
    addr.server_port = 30000;
    addr.client_ip   = NULL;
    addr.client_port = 0;
    test_udp_do(&addr);

    /* unicast, IPv6 */
    addr.server_ip   = "::1";
    addr.server_port = 30000;
    addr.client_ip   = NULL;
    addr.client_port = 0;
    test_udp_do(&addr);
    
    /* multicast, IPv4 */
    addr.server_ip   = "239.7.8.9";
    addr.server_port = 30000;
    addr.client_ip   = NULL;
    addr.client_port = 0;
    test_udp_do(&addr);
    
    /* multicast, IPv6 */
    addr.server_ip   = ipv6_grp;
    addr.server_port = 30000;
    addr.client_ip   = NULL;
    addr.client_port = 0;
    test_udp_do(&addr);

    /* source specific multicast IPv4 */
    addr.server_ip   = "239.7.8.9";
    addr.server_port = 30000;
    addr.client_ip   = ipv4;
    addr.client_port = 40000;
    test_udp_do(&addr);

    /* source specific multicast IPv6 */
    addr.server_ip   = ipv6_grp;
    addr.server_port = 30000;
    addr.client_ip   = ipv6;
    addr.client_port = 40000;
    test_udp_do(&addr);
    
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    return 0;
}
