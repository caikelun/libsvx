/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "svx_inetaddr.h"
#include "svx_errno.h"
#include "svx_log.h"

int svx_inetaddr_from_ipport(svx_inetaddr_t *self, const char *ip, uint16_t port)
{
    int   r = 0;
    char *ifname = NULL;
    char  ipv6[INET6_ADDRSTRLEN + 1 + IF_NAMESIZE + 1];

    if(NULL == self || NULL == ip) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, ip:%p\n", self, ip);
    
    memset(self, 0, sizeof(svx_inetaddr_t));

    if(!strchr(ip, ':'))
    {
        /* IPv4 */
        self->storage.addr4.sin_family = AF_INET;
        self->storage.addr4.sin_port = htons(port);
        if(1 != (r = inet_pton(AF_INET, ip, &(self->storage.addr4.sin_addr))))
            SVX_LOG_ERRNO_RETURN_ERR((0 == r ? SVX_ERRNO_INVAL : errno), "ip:%s\n", ip);
    }
    else
    {
        /* IPv6 */
        self->storage.addr6.sin6_family = AF_INET6;
        self->storage.addr6.sin6_port = htons(port);
        self->storage.addr6.sin6_flowinfo = 0; /* ignored, always 0 */
        self->storage.addr6.sin6_scope_id = 0; /* only used for link-local IPv6 address */

        strncpy(ipv6, ip, sizeof(ipv6));
        if(NULL != (ifname = strchr(ipv6, '%')))
        {
            *ifname = '\0';
            ifname++;
        }
        
        if(1 != (r = inet_pton(AF_INET6, ipv6, &(self->storage.addr6.sin6_addr))))
            SVX_LOG_ERRNO_RETURN_ERR((0 == r ? SVX_ERRNO_INVAL : errno), "ip:%s\n", ipv6);

        /* set scope id for link-local IPv6 address */
        /* man ipv6(7): Linux supports it only for link-local addresses, 
           in that case sin6_scope_id contains the interface index */
        if(IN6_IS_ADDR_LINKLOCAL(&(self->storage.addr6.sin6_addr)) || 
           IN6_IS_ADDR_MC_LINKLOCAL(&(self->storage.addr6.sin6_addr)))
        {
            if(NULL == ifname || '\0' == *ifname)
                SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_FORMAT, "IPv6 link-local address without interface: %s\n", ip);
            if(0 == (self->storage.addr6.sin6_scope_id = if_nametoindex(ifname)))
                SVX_LOG_ERRNO_RETURN_ERR(errno, "ifname:%s\n", ifname);
        }
    }

    return 0;
}

int svx_inetaddr_from_addr(svx_inetaddr_t *self, const struct sockaddr *addr)
{
    socklen_t addrlen = 0;

    if(NULL == self || NULL == addr) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, addr:%p\n", self, addr);
    if(AF_INET != addr->sa_family && AF_INET6 != addr->sa_family)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "addr->sa_family:%hu\n", addr->sa_family);

    addrlen = (AF_INET == addr->sa_family ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6));
    memset(self, 0, sizeof(svx_inetaddr_t));
    memcpy(&(self->storage.addr), addr, addrlen);

    return 0;
}

int svx_inetaddr_from_fd_local(svx_inetaddr_t *self, int fd)
{
    socklen_t addrlen = 0;

    if(NULL == self || fd < 0) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, fd:%d\n", self, fd);

    addrlen = sizeof(self->storage);
    if(0 != getsockname(fd, &(self->storage.addr), &addrlen)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

    if(!((AF_INET  == self->storage.addr.sa_family && sizeof(struct sockaddr_in)  == addrlen) ||
         (AF_INET6 == self->storage.addr.sa_family && sizeof(struct sockaddr_in6) == addrlen)))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTSPT, "fd:%d, family:%u, addrlen:%u\n", 
                                 fd, self->storage.addr.sa_family, addrlen);
    
    return 0;
}

int svx_inetaddr_from_fd_peer(svx_inetaddr_t *self, int fd)
{
    socklen_t addrlen = 0;

    if(NULL == self || fd < 0) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, fd:%d\n", self, fd);

    addrlen = sizeof(self->storage);
    if(0 != getpeername(fd, &(self->storage.addr), &addrlen)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

    if(!((AF_INET  == self->storage.addr.sa_family && sizeof(struct sockaddr_in)  == addrlen) ||
         (AF_INET6 == self->storage.addr.sa_family && sizeof(struct sockaddr_in6) == addrlen)))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTSPT, "fd:%d, family:%u, addrlen:%u\n", 
                                 fd, self->storage.addr.sa_family, addrlen);
    
    return 0;
}

int svx_inetaddr_get_ipport(svx_inetaddr_t *self, char *ip, size_t ip_len, uint16_t *port)
{
    size_t len;

    if(NULL == self || ((NULL == ip || ip_len < SVX_INETADDR_STR_IP_LEN) && NULL == port))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, ip:%p, ip_len:%zu, port:%p\n", self, ip, ip_len, port);
    
    switch(self->storage.addr.sa_family)
    {
    case AF_INET:
        if(NULL != ip && ip_len >= SVX_INETADDR_STR_IP_LEN)
        {
            memset(ip, 0, ip_len);
            if(NULL == inet_ntop(AF_INET, &(self->storage.addr4.sin_addr), ip, (socklen_t)ip_len))
                SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
        }
        if(NULL != port) *port = ntohs(self->storage.addr4.sin_port);
        return 0;
    case AF_INET6:
        if(NULL != ip && ip_len >= SVX_INETADDR_STR_IP_LEN)
        {
            memset(ip, 0, ip_len);
            if(NULL == inet_ntop(AF_INET6, &(self->storage.addr6.sin6_addr), ip, (socklen_t)ip_len))
                SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
            
            /* append IPv6 link-local address interface name */
            if(IN6_IS_ADDR_LINKLOCAL(&(self->storage.addr6.sin6_addr)) || 
               IN6_IS_ADDR_MC_LINKLOCAL(&(self->storage.addr6.sin6_addr)))
            {
                len = strlen(ip);
                ip[len++] = '%';
                if(NULL == if_indextoname(self->storage.addr6.sin6_scope_id, ip + len))
                    SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
            }
        }
        if(NULL != port) *port = ntohs(self->storage.addr6.sin6_port);
        return 0;
    default:
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOTSPT, "family:%u\n", self->storage.addr.sa_family);
    }
}

int svx_inetaddr_get_addr_str(svx_inetaddr_t *self, char *addr, size_t len)
{
    char     ip[SVX_INETADDR_STR_IP_LEN] = "";
    uint16_t port = 0;
    int      r = 0;

    if(NULL == self || NULL == addr || len < SVX_INETADDR_STR_ADDR_LEN)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, addr:%p, len:%zu\n", self, addr, len);

    if(0 != (r = svx_inetaddr_get_ipport(self, ip, sizeof(ip), &port))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    snprintf(addr, len, "[%s]:%"PRIu16, ip, port);
    return 0;
}

static int svx_inetaddr_cmp_inner(svx_inetaddr_t *sa1, svx_inetaddr_t *sa2, int cmp_ip, int cmp_port)
{
    int r = 0;

    if(sa1->storage.addr.sa_family != sa2->storage.addr.sa_family)
        return sa1->storage.addr.sa_family > sa2->storage.addr.sa_family ? 1 : -1;

    switch(sa1->storage.addr.sa_family)
    {
    case AF_INET:
        if(cmp_ip && 0 != (r = memcmp(&(sa1->storage.addr4.sin_addr), &(sa2->storage.addr4.sin_addr), sizeof(struct in_addr))))
            return r;
        if(cmp_port && sa1->storage.addr4.sin_port != sa1->storage.addr4.sin_port)
            return sa1->storage.addr4.sin_port > sa1->storage.addr4.sin_port ? 1 : -1;
        return 0;
    case AF_INET6:
        if(cmp_ip && 0 != (r = memcmp(&(sa1->storage.addr6.sin6_addr), &(sa2->storage.addr6.sin6_addr), sizeof(struct in6_addr))))
            return r;
        if(cmp_port && sa1->storage.addr6.sin6_port != sa1->storage.addr6.sin6_port)
            return sa1->storage.addr4.sin_port > sa1->storage.addr4.sin_port ? 1 : -1;
        return 0;
    default:
        SVX_LOG_ERRNO_ERR(SVX_ERRNO_NOTSPT, "family:%u\n", sa1->storage.addr.sa_family);
        return 1;
    }
}

int svx_inetaddr_cmp_addr(svx_inetaddr_t *sa1, svx_inetaddr_t *sa2)
{
    if(NULL == sa1 || NULL == sa2) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "sa1:%p, sa2:%p\n", sa1, sa2);

    return svx_inetaddr_cmp_inner(sa1, sa2, 1, 1);
}

int svx_inetaddr_cmp_ip(svx_inetaddr_t *sa1, svx_inetaddr_t *sa2)
{
    if(NULL == sa1 || NULL == sa2) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "sa1:%p, sa2:%p\n", sa1, sa2);

    return svx_inetaddr_cmp_inner(sa1, sa2, 1, 0);
}

int svx_inetaddr_cmp_port(svx_inetaddr_t *sa1, svx_inetaddr_t *sa2)
{
    if(NULL == sa1 || NULL == sa2) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "sa1:%p, sa2:%p\n", sa1, sa2);

    return svx_inetaddr_cmp_inner(sa1, sa2, 0, 1);
}
