/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include "svx_udp.h"
#include "svx_inetaddr.h"
#include "svx_util.h"
#include "svx_errno.h"
#include "svx_log.h"

int svx_udp_server(int *fd, const svx_inetaddr_t *server_addr)
{
    const int on = 1;
    int       r;

    if(NULL == fd || NULL == server_addr) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%p, server_addr:%p\n", fd, server_addr);

    if(0 > (*fd = socket(server_addr->storage.addr.sa_family, SOCK_DGRAM, IPPROTO_UDP)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);

    /* only set SO_REUSEADDR before bind to a multicast address,
       this allow more than one process bind to the same address for receiving multicast datagram */
    if(SVX_INETADDR_IS_IP_MULTICAST(server_addr))
        if(0 != setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
            SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);

    if(0 != bind(*fd, &(server_addr->storage.addr), SVX_INETADDR_LEN(server_addr)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);

    return 0;

 err:
    if(*fd >= 0)
    {
        close(*fd);
        *fd = -1;
    }
    return r;
}

int svx_udp_client(int *fd, const svx_inetaddr_t *server_addr, const svx_inetaddr_t *client_addr, int family)
{
    const int on = 1;
    int       r;

    if(NULL == fd || (NULL == server_addr && NULL == client_addr && family < 0))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%p, server_addr:%p, client_addr:%p, family:%d\n",
                                 fd, server_addr, client_addr, family);

    if(NULL != server_addr)      family = server_addr->storage.addr.sa_family;
    else if(NULL != client_addr) family = client_addr->storage.addr.sa_family;

    if(0 > (*fd = socket(family, SOCK_DGRAM, IPPROTO_UDP)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);

    if(NULL != client_addr)
    {
        /* only set SO_REUSEADDR before bind to a multicast address,
           this allow more than one process bind to the same address for receiving multicast datagram */
        if(SVX_INETADDR_IS_IP_MULTICAST(client_addr))
            if(0 != setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
                SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);
        
        if(0 != bind(*fd, &(client_addr->storage.addr), SVX_INETADDR_LEN(client_addr)))
            SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);
    }

    if(NULL != server_addr)
        if(0 != connect(*fd, &(server_addr->storage.addr), SVX_INETADDR_LEN(server_addr)))
            SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);

    return 0;

 err:
    if(*fd >= 0)
    {
        close(*fd);
        *fd = -1;
    }
    return r;
}

int svx_udp_get_kernel_read_buf_len(int fd, size_t *max_len)
{
    socklen_t max_len_size = sizeof(size_t);

    if(fd < 0 || NULL == max_len) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d, max_len:%p\n", fd, max_len);
    
    if(0 != getsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void *)max_len, &max_len_size))
        SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);

    return 0;
}

int svx_udp_set_kernel_read_buf_len(int fd, size_t max_len)
{
    size_t    real_len      = 0;
    socklen_t real_len_size = sizeof(real_len);
    int       is_root       = svx_util_is_root();

    if(fd < 0 || 0 == max_len) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d, max_len:%zu\n", fd, max_len);

    if(0 != setsockopt(fd, SOL_SOCKET, is_root ? SO_RCVBUFFORCE : SO_RCVBUF, &(max_len), sizeof(max_len)))
        SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d, max_len:%zu\n", fd, max_len);
    if(0 != getsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void *)&real_len, &real_len_size))
        SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);
    
    if(real_len < max_len)
    {
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTSPT, "set rcvbuf(%s) failed. Set:%zu. Got:%zu.%s\n",
                                 is_root ? "SO_RCVBUFFORCE" : "SO_RCVBUF", max_len, real_len,
                                 is_root ? "" : " (try to re-run as root, "
                                 "or add the line \"net.core.rmem_max = xxxxxx\" to /etc/sysctl.conf)\n");
    }

    return 0;
}

int svx_udp_get_kernel_write_buf_len(int fd, size_t *max_len)
{
    socklen_t max_len_size = sizeof(size_t);

    if(fd < 0 || NULL == max_len) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d, max_len:%p\n", fd, max_len);
    
    if(0 != getsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void *)max_len, &max_len_size))
        SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);

    return 0;
}

int svx_udp_set_kernel_write_buf_len(int fd, size_t max_len)
{
    size_t    real_len      = 0;
    socklen_t real_len_size = sizeof(real_len);
    int       is_root       = svx_util_is_root();

    if(fd < 0 || 0 == max_len) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d, max_len:%zu\n", fd, max_len);

    if(0 != setsockopt(fd, SOL_SOCKET, is_root ? SO_SNDBUFFORCE : SO_SNDBUF, &(max_len), sizeof(max_len)))
        SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d, max_len:%zu\n", fd, max_len);
    if(0 != getsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void *)&real_len, &real_len_size))
        SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);
    
    if(real_len < max_len)
    {
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTSPT, "set sndbuf(%s) failed. Set:%zu. Got:%zu.%s\n",
                                 is_root ? "SO_SNDBUFFORCE" : "SO_SNDBUF", max_len, real_len,
                                 is_root ? "" : " (try to re-run as root, "
                                 "or add the line \"net.core.wmem_max = xxxxxx\" to /etc/sysctl.conf)\n");
    }

    return 0;
}

int svx_udp_mcast_join(int fd, const svx_inetaddr_t *grp, unsigned int ifindex)
{
    struct group_req req;

    if(fd < 0 || NULL == grp || SVX_INETADDR_LEN(grp) > sizeof(req.gr_group) || 0 == ifindex)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d, grp:%p, ifindex:%u\n", fd, grp, ifindex);

    memset(&req, 0, sizeof(req));
    req.gr_interface = ifindex;
    memcpy(&(req.gr_group), grp, SVX_INETADDR_LEN(grp));

    if(0 != setsockopt(fd, SVX_INETADDR_PROTO_LEVEL(grp), MCAST_JOIN_GROUP, &req, sizeof(req)))
        SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);

    return 0;
}

int svx_udp_mcast_leave(int fd, const svx_inetaddr_t *grp, unsigned int ifindex)
{
    struct group_req req;

    if(fd < 0 || NULL == grp || SVX_INETADDR_LEN(grp) > sizeof(req.gr_group))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d, grp:%p\n", fd, grp);

    memset(&req, 0, sizeof(req));
    req.gr_interface = ifindex;
    memcpy(&(req.gr_group), grp, SVX_INETADDR_LEN(grp));

    if(0 != setsockopt(fd, SVX_INETADDR_PROTO_LEVEL(grp), MCAST_LEAVE_GROUP, &req, sizeof(req)))
        SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);

    return 0;
}

int svx_udp_mcast_join_source_group(int fd, const svx_inetaddr_t *src, const svx_inetaddr_t *grp, unsigned int ifindex)
{
    struct group_source_req req;

    if(fd < 0 || NULL == src || NULL == grp ||
       SVX_INETADDR_LEN(src) > sizeof(req.gsr_source) || SVX_INETADDR_LEN(grp) > sizeof(req.gsr_group) || 0 == ifindex)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d, src:%p, grp:%p, ifindex:%u\n", fd, src, grp, ifindex);

    memset(&req, 0, sizeof(req));
    req.gsr_interface = ifindex;
    memcpy(&(req.gsr_source), src, SVX_INETADDR_LEN(src));
    memcpy(&(req.gsr_group), grp, SVX_INETADDR_LEN(grp));

    if(0 != setsockopt(fd, SVX_INETADDR_PROTO_LEVEL(grp), MCAST_JOIN_SOURCE_GROUP, &req, sizeof(req)))
        SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);

    return 0;
}

int svx_udp_mcast_leave_source_group(int fd, const svx_inetaddr_t *src, const svx_inetaddr_t *grp, unsigned int ifindex)
{
    struct group_source_req req;

    if(fd < 0 || NULL == src || NULL == grp ||
       SVX_INETADDR_LEN(src) > sizeof(req.gsr_source) || SVX_INETADDR_LEN(grp) > sizeof(req.gsr_group))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d, src:%p, grp:%p\n", fd, src, grp);

    memset(&req, 0, sizeof(req));
    req.gsr_interface = ifindex;
    memcpy(&(req.gsr_source), src, SVX_INETADDR_LEN(src));
    memcpy(&(req.gsr_group), grp, SVX_INETADDR_LEN(grp));

    if(0 != setsockopt(fd, SVX_INETADDR_PROTO_LEVEL(grp), MCAST_LEAVE_SOURCE_GROUP, &req, sizeof(req)))
        SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);

    return 0;
}

int svx_udp_mcast_block_source(int fd, const svx_inetaddr_t *src, const svx_inetaddr_t *grp, unsigned int ifindex)
{
    struct group_source_req req;

    if(fd < 0 || NULL == src || NULL == grp ||
       SVX_INETADDR_LEN(src) > sizeof(req.gsr_source) || SVX_INETADDR_LEN(grp) > sizeof(req.gsr_group))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d, src:%p, grp:%p\n", fd, src, grp);

    memset(&req, 0, sizeof(req));
    req.gsr_interface = ifindex;
    memcpy(&(req.gsr_source), src, SVX_INETADDR_LEN(src));
    memcpy(&(req.gsr_group), grp, SVX_INETADDR_LEN(grp));

    if(0 != setsockopt(fd, SVX_INETADDR_PROTO_LEVEL(grp), MCAST_BLOCK_SOURCE, &req, sizeof(req)))
        SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);

    return 0;
}

int svx_udp_mcast_unblock_source(int fd, const svx_inetaddr_t *src, const svx_inetaddr_t *grp, unsigned int ifindex)
{
    struct group_source_req req;

    if(fd < 0 || NULL == src || NULL == grp ||
       SVX_INETADDR_LEN(src) > sizeof(req.gsr_source) || SVX_INETADDR_LEN(grp) > sizeof(req.gsr_group))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d, src:%p, grp:%p\n", fd, src, grp);

    memset(&req, 0, sizeof(req));
    req.gsr_interface = ifindex;
    memcpy(&(req.gsr_source), src, SVX_INETADDR_LEN(src));
    memcpy(&(req.gsr_group), grp, SVX_INETADDR_LEN(grp));

    if(0 != setsockopt(fd, SVX_INETADDR_PROTO_LEVEL(grp), MCAST_UNBLOCK_SOURCE, &req, sizeof(req)))
        SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);

    return 0;
}

int svx_udp_mcast_get_ttl(int fd, unsigned int *ttl)
{
    svx_inetaddr_t local_addr;
    int            r;
    
    if(fd < 0 || NULL == ttl) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d, ttl:%p\n", fd, ttl);

    if(0 != (r = svx_inetaddr_from_fd_local(&local_addr, fd)))
        SVX_LOG_ERRNO_RETURN_ERR(r, "fd:%d\n", fd);
    
    switch(local_addr.storage.addr.sa_family)
    {
    case AF_INET:
        {
            u_char    val;
            socklen_t len = sizeof(val);
            if(0 != getsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &val, &len))
                SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);
            *ttl = (unsigned int)val;
            return 0;
        }
    case AF_INET6:
        {
            u_int     val;
            socklen_t len = sizeof(val);
            if(0 != getsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &val, &len))
                SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);
            *ttl = (unsigned int)val;
            return 0;
        }
    default:
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTSPT, "fd:%d, family:%d\n",
                                 fd, local_addr.storage.addr.sa_family);
    }
}

int svx_udp_mcast_set_ttl(int fd, unsigned int ttl)
{
    svx_inetaddr_t local_addr;
    int            r;

    if(fd < 0 || ttl < 0) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d, ttl:%u\n", fd, ttl);

    if(0 != (r = svx_inetaddr_from_fd_local(&local_addr, fd)))
        SVX_LOG_ERRNO_RETURN_ERR(r, "fd:%d\n", fd);
    
    switch(local_addr.storage.addr.sa_family)
    {
    case AF_INET:
        {
            u_char val = (u_char)ttl;
            if(0 != setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &val, sizeof(val)))
                SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d, ttl:%u\n", fd, ttl);
            return 0;
        }
    case AF_INET6:
        {
            u_int val = (u_int)ttl;
            if(0 != setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &val, sizeof(val)))
                SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d, ttl:%u\n", fd, ttl);
            return 0;
        }
    default:
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTSPT, "fd:%d, family:%d\n",
                                 fd, local_addr.storage.addr.sa_family);
    }
}

int svx_udp_mcast_get_if(int fd, unsigned int *ifindex)
{
    svx_inetaddr_t local_addr;
    int            r;

    if(fd < 0 || NULL == ifindex) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d, ifindex:%p\n", fd, ifindex);

    if(0 != (r = svx_inetaddr_from_fd_local(&local_addr, fd)))
        SVX_LOG_ERRNO_RETURN_ERR(r, "fd:%d\n", fd);
    
    switch(local_addr.storage.addr.sa_family)
    {
    case AF_INET:
        {
            struct in_addr      inaddr;
            socklen_t           len = sizeof(inaddr);
            struct ifaddrs     *ifaddr, *next;
            struct sockaddr_in *sa;

            /* get in_addr from fd */
            if(0 != getsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &inaddr, &len))
                SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);

            /* get ifname from in_addr, get ifindex from ifname */
            if(0 != getifaddrs(&ifaddr)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
            for(next = ifaddr; NULL != next; next = next->ifa_next)
            {
                if((next->ifa_flags & IFF_UP) && (next->ifa_flags & IFF_RUNNING) &&
                   next->ifa_addr && AF_INET == next->ifa_addr->sa_family)
                {
                    sa = (struct sockaddr_in *)(next->ifa_addr);
                    if(0 == memcmp(&(sa->sin_addr), &inaddr, sizeof(struct in_addr)))
                    {
                        if(0 != (*ifindex = if_nametoindex(next->ifa_name)))
                        {
                            freeifaddrs(ifaddr);
                            return 0; /* OK */
                        }
                    }
                }
            }
            freeifaddrs(ifaddr);
            SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTFND, "fd:%d\n", fd); /* failed */
        }
    case AF_INET6:
        {
            u_int     val;
            socklen_t len = sizeof(val);
            if(0 != getsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, &val, &len))
                SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);
            *ifindex = (unsigned int)val;
            return 0;
        }
    default:
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTSPT, "fd:%d, family:%d\n",
                                 fd, local_addr.storage.addr.sa_family);
    }
}

int svx_udp_mcast_set_if(int fd, unsigned int ifindex)
{
    svx_inetaddr_t local_addr;
    int            r;

    if(fd < 0 || 0 == ifindex) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d, ifindex:%u\n", fd, ifindex);

    if(0 != (r = svx_inetaddr_from_fd_local(&local_addr, fd)))
        SVX_LOG_ERRNO_RETURN_ERR(r, "fd:%d\n", fd);
    
    switch(local_addr.storage.addr.sa_family)
    {
    case AF_INET:
        {
            struct in_addr inaddr;
            struct ifreq   ifreq;

            /* get ifname from ifindex */
            if(NULL == if_indextoname(ifindex, ifreq.ifr_name))
                SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d, ifindex:%u\n", fd, ifindex);
            
            /* get in_addr from ifname */
            if(ioctl(fd, SIOCGIFADDR, &ifreq) < 0)
                SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d, ifindex:%u\n", fd, ifindex);
            memcpy(&inaddr, &((struct sockaddr_in *)&ifreq.ifr_addr)->sin_addr, sizeof(struct in_addr));
            
            if(0 != setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &inaddr, sizeof(struct in_addr)))
                SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d, ifindex:%u\n", fd, ifindex);
            
            return 0;
        }
    case AF_INET6:
        {
            u_int val = (u_int)ifindex;
            
            if(0 != setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, &val, sizeof(val)))
                SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d, ifindex:%u\n", fd, ifindex);
            
            return 0;
        }
    default:
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTSPT, "fd:%d, family:%d\n",
                                 fd, local_addr.storage.addr.sa_family);
    }
}

int svx_udp_mcast_get_loop(int fd, unsigned int *loop)
{
    svx_inetaddr_t local_addr;
    int            r;

    if(fd < 0 || NULL == loop) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d, loop:%p\n", fd, loop);

    if(0 != (r = svx_inetaddr_from_fd_local(&local_addr, fd)))
        SVX_LOG_ERRNO_RETURN_ERR(r, "fd:%d\n", fd);
    
    switch(local_addr.storage.addr.sa_family)
    {
    case AF_INET:
        {
            u_char val;
            socklen_t len = sizeof(val);
            if(0 != getsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &val, &len))
                SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);
            *loop = (unsigned int)val;
            return 0;
        }
    case AF_INET6:
        {
            u_int     val;
            socklen_t len = sizeof(val);
            if(0 != getsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &val, &len))
                SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);
            *loop = (unsigned int)val;
            return 0;
        }
    default:
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTSPT, "fd:%d, family:%d\n",
                                 fd, local_addr.storage.addr.sa_family);
    }
}

int svx_udp_mcast_set_loop(int fd, unsigned int loop)
{
    svx_inetaddr_t local_addr;
    int            r;

    if(fd < 0) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d\n", fd);

    if(0 != (r = svx_inetaddr_from_fd_local(&local_addr, fd)))
        SVX_LOG_ERRNO_RETURN_ERR(r, "fd:%d\n", fd);
    
    switch(local_addr.storage.addr.sa_family)
    {
    case AF_INET:
        {
            u_char val = (u_char)loop;
            if(0 != setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &val, sizeof(val)))
                SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d, loop:%u\n", fd, loop);
            return 0;
        }
    case AF_INET6:
        {
            u_int val = (u_int)loop;
            if(0 != setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &val, sizeof(val)))
                SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d, loop:%u\n", fd, loop);
            return 0;
        }
    default:
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTSPT, "fd:%d, family:%d\n",
                                 fd, local_addr.storage.addr.sa_family);
    }
}
