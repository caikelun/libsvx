/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet/udp.h>
#include "svx_icmp.h"
#include "svx_looper.h"
#include "svx_channel.h"
#include "svx_inetaddr.h"
#include "svx_util.h"
#include "svx_errno.h"
#include "svx_log.h"

#define SVX_ICMP_UNREACH_MIN_LEN_AFTER_INNER_IP_ADDR 8
#define SVX_ICMP_READ_MAX_TIMES_PER_LOOP             64

struct svx_icmp
{
    int            running;
    svx_looper_t  *looper;
    uint16_t       icmphdr_id;
    uint16_t       icmphdr_seq;
    int            fd4;
    svx_channel_t *channel4;
    int            fd6;
    svx_channel_t *channel6;
    svx_icmp_cb_t  echoreply_cb;
    void          *echoreply_cb_arg;
    svx_icmp_cb_t  unreach_port_cb;
    void          *unreach_port_cb_arg;
};

static void svx_icmp_handle_read4(void *arg)
{
    svx_icmp_t         *self = (svx_icmp_t *)arg;
    int                 i;
    uint8_t             buf[1500];
    ssize_t             buf_len;
    svx_inetaddr_t      peer_addr;
    socklen_t           peer_addr_len;
    struct ip          *iphdr;
    int                 iphdr_len;
    struct icmp        *icmphdr;
    struct ip          *inner_iphdr;
    int                 inner_iphdr_len;
    struct udphdr      *udphdr;

    for(i = 0; i < SVX_ICMP_READ_MAX_TIMES_PER_LOOP; i++)
    {
        peer_addr_len = sizeof(struct sockaddr_in);
        if(0 > (buf_len = recvfrom(self->fd4, buf, sizeof(buf), 0, &(peer_addr.storage.addr), &peer_addr_len))) return;
        if(buf_len < sizeof(struct ip) + ICMP_MINLEN) return;

        /* IP header */
        iphdr = (struct ip *)buf;
        if(IPPROTO_ICMP != iphdr->ip_p) return;
        iphdr_len = iphdr->ip_hl * 4;
        if(buf_len < iphdr_len + ICMP_MINLEN) return;
        
        /* ICMP header */
        icmphdr = (struct icmp *)(buf + iphdr_len);
        
        switch(icmphdr->icmp_type)
        {
        case ICMP_ECHOREPLY:
            if(NULL == self->echoreply_cb) return;
            if(self->icmphdr_id != icmphdr->icmp_id) return;
            self->echoreply_cb(peer_addr, self->echoreply_cb_arg);
            break;
        case ICMP_UNREACH:
            if(NULL == self->unreach_port_cb) return;
            if(ICMP_UNREACH_PORT != icmphdr->icmp_code) return;
            if(buf_len < iphdr_len + ICMP_MINLEN + sizeof(struct ip)) return;
            inner_iphdr = (struct ip *)(buf + iphdr_len + ICMP_MINLEN);
            if(IPPROTO_UDP != inner_iphdr->ip_p) return; /* UDP only */
            inner_iphdr_len = inner_iphdr->ip_hl * 4;
            if(buf_len < iphdr_len + ICMP_MINLEN + inner_iphdr_len + SVX_ICMP_UNREACH_MIN_LEN_AFTER_INNER_IP_ADDR) return;
            udphdr = (struct udphdr *)(buf + iphdr_len + ICMP_MINLEN + inner_iphdr_len);
            memcpy(&(peer_addr.storage.addr4.sin_addr), &(inner_iphdr->ip_dst), sizeof(struct in_addr));
            peer_addr.storage.addr4.sin_port = udphdr->dest;
            self->unreach_port_cb(peer_addr, self->unreach_port_cb_arg);
            break;
        default:
            break;
        }
    }
}

static void svx_icmp_handle_read6(void *arg)
{
    svx_icmp_t         *self = (svx_icmp_t *)arg;
    int                 i;
    uint8_t             buf[1500];
    ssize_t             buf_len;
    svx_inetaddr_t      peer_addr;
    socklen_t           peer_addr_len;
    struct icmp6_hdr   *icmphdr;
    struct ip6_hdr     *inner_iphdr;
    int                 inner_iphdr_len;
    uint8_t             inner_iphdr_next_header;
    struct udphdr      *udphdr;

    for(i = 0; i < SVX_ICMP_READ_MAX_TIMES_PER_LOOP; i++)
    {
        peer_addr_len = sizeof(struct sockaddr_in6);
        if(0 > (buf_len = recvfrom(self->fd6, buf, sizeof(buf), 0, &(peer_addr.storage.addr), &peer_addr_len))) return;
        if(buf_len < sizeof(struct icmp6_hdr)) return;
        
        /* ICMP header */
        icmphdr = (struct icmp6_hdr *)buf;
        
        switch(icmphdr->icmp6_type)
        {
        case ICMP6_ECHO_REPLY:
            if(NULL == self->echoreply_cb) return;
            if(self->icmphdr_id != icmphdr->icmp6_id) return;
            self->echoreply_cb(peer_addr, self->echoreply_cb_arg);
            break;
        case ICMP6_DST_UNREACH:
            if(NULL == self->unreach_port_cb) return;
            if(ICMP6_DST_UNREACH_NOPORT != icmphdr->icmp6_code) return;
            if(buf_len < sizeof(struct icmp6_hdr) + sizeof(struct ip6_hdr)) return;
            inner_iphdr = (struct ip6_hdr *)(buf + sizeof(struct icmp6_hdr));
            inner_iphdr_len = sizeof(struct ip6_hdr);
            inner_iphdr_next_header = inner_iphdr->ip6_nxt;
            /* walk through the IPv6 extension headers chain */
            while(1)
            {
                switch(inner_iphdr_next_header)
                {
                case IPPROTO_UDP:      /* 17:  User Datagram Protocol. */
                    /* found the UDP header we need */
                    if(buf_len < sizeof(struct icmp6_hdr) + inner_iphdr_len + SVX_ICMP_UNREACH_MIN_LEN_AFTER_INNER_IP_ADDR) return;
                    udphdr = (struct udphdr *)(buf + sizeof(struct icmp6_hdr) + inner_iphdr_len);
                    memcpy(&(peer_addr.storage.addr6.sin6_addr), &(inner_iphdr->ip6_dst), sizeof(struct in6_addr));
                    peer_addr.storage.addr6.sin6_port = udphdr->dest;
                    self->unreach_port_cb(peer_addr, self->unreach_port_cb_arg);
                    return;
                case IPPROTO_HOPOPTS:  /* 0:   IPv6 Hop-by-Hop options. */
                case IPPROTO_ROUTING:  /* 43:  IPv6 routing header. */
                case IPPROTO_ESP:      /* 50:  encapsulating security payload. */
                case IPPROTO_AH:       /* 51:  authentication header. */
                case IPPROTO_DSTOPTS:  /* 60:  IPv6 destination options. */
                case IPPROTO_MH:       /* 135: IPv6 mobility header. */
                    /* to next IPv6 extension header */
                    inner_iphdr_next_header = ((struct ip6_ext *)(buf + sizeof(struct icmp6_hdr) + inner_iphdr_len))->ip6e_nxt;
                    inner_iphdr_len += ((struct ip6_ext *)(buf + sizeof(struct icmp6_hdr) + inner_iphdr_len))->ip6e_len;
                    if(buf_len < sizeof(struct icmp6_hdr) + inner_iphdr_len) return;
                    break;
                case IPPROTO_FRAGMENT: /* 44:  IPv6 fragmentation header. */
                    /* to next IPv6 extension header */
                    inner_iphdr_next_header = ((struct ip6_frag *)(buf + sizeof(struct icmp6_hdr) + inner_iphdr_len))->ip6f_nxt;
                    inner_iphdr_len += sizeof(struct ip6_frag);
                    if(buf_len < sizeof(struct icmp6_hdr) + inner_iphdr_len) return;
                    break;
                case IPPROTO_ICMPV6:   /* 58:  ICMPv6.  */
                case IPPROTO_NONE:     /* 59:  IPv6 no next header. */
                default:
                    /* found some unwanted header or no headers left */
                    return;
                }
            }
            break;
        default:
            break;
        }
    }
}

int svx_icmp_create(svx_icmp_t **self, svx_looper_t *looper)
{
    if(NULL == self || NULL == looper)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, looper:%p\n", self, looper);

    if(NULL == (*self = malloc(sizeof(svx_icmp_t)))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    (*self)->running             = 0;
    (*self)->looper              = looper;
    (*self)->icmphdr_id          = (uint16_t)(getpid() & 0xffff);
    (*self)->icmphdr_seq         = 0;
    (*self)->fd4                 = -1;
    (*self)->channel4            = NULL;
    (*self)->fd6                 = -1;
    (*self)->channel6            = NULL;
    (*self)->echoreply_cb        = NULL;
    (*self)->echoreply_cb_arg    = NULL;
    (*self)->unreach_port_cb     = NULL;
    (*self)->unreach_port_cb_arg = NULL;

    return 0;
}
    
int svx_icmp_destroy(svx_icmp_t **self)
{
    int r;
    
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    if(NULL == *self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "*self:%p\n", *self);

    if(0 != (r = svx_icmp_stop(*self))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    free(*self);
    *self = NULL;
    return 0;
}

int svx_icmp_set_echoreply_cb(svx_icmp_t *self, svx_icmp_cb_t cb, void *cb_arg)
{
    if(NULL == self || NULL == cb) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, cb:%p\n", self, cb);

    self->echoreply_cb     = cb;
    self->echoreply_cb_arg = cb_arg;

    return 0;
}

int svx_icmp_set_unreach_port_cb(svx_icmp_t *self, svx_icmp_cb_t cb, void *cb_arg)
{
    if(NULL == self || NULL == cb) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, cb:%p\n", self, cb);

    self->unreach_port_cb     = cb;
    self->unreach_port_cb_arg = cb_arg;

    return 0;
}

int svx_icmp_start(svx_icmp_t *self)
{
    struct icmp6_filter filt;
    int                 r;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    
    if(self->running) return 0;

    if(NULL == self->echoreply_cb && NULL == self->unreach_port_cb)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_UNKNOWN, "Can not start icmp when echoreply_cb and unreach_port_cb are all NULL.\n");
    
    if(self->fd4 >= 0 || NULL != self->channel4 || self->fd6 >= 0 || NULL != self->channel6)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_UNKNOWN, NULL);

    /* create fds for ICMPv4 and ICMPv6 */
    if((self->fd4 = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0)
        SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);
    if(0 != (r = svx_util_set_nonblocking(self->fd4)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    if((self->fd6 = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6)) < 0)
        SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);
    if(0 != (r = svx_util_set_nonblocking(self->fd6)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    /* set block/pass for ICMPv6 */
    memset(&filt, 0, sizeof(filt));
    ICMP6_FILTER_SETBLOCKALL(&filt);
    if(NULL != self->echoreply_cb)    ICMP6_FILTER_SETPASS(ICMP6_ECHO_REPLY, &filt);
    if(NULL != self->unreach_port_cb) ICMP6_FILTER_SETPASS(ICMP6_DST_UNREACH, &filt);
    if(0 != setsockopt(self->fd6, SOL_ICMPV6, ICMP6_FILTER, &filt, sizeof(filt)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);

    /* create channels */
    if(0 != (r = svx_channel_create(&(self->channel4), self->looper, self->fd4, SVX_CHANNEL_EVENT_READ)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    if(0 != (r = svx_channel_set_read_callback(self->channel4, svx_icmp_handle_read4, self)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    if(0 != (r = svx_channel_create(&(self->channel6), self->looper, self->fd6, SVX_CHANNEL_EVENT_READ)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    if(0 != (r = svx_channel_set_read_callback(self->channel6, svx_icmp_handle_read6, self)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    
    self->running = 1;
    return 0;

 err:
    if(self->fd4 >= 0)
    {
        close(self->fd4);
        self->fd4 = -1;
    }
    if(self->fd6 >= 0)
    {
        close(self->fd6);
        self->fd6 = -1;
    }
    if(NULL != self->channel4)
        svx_channel_destroy(&(self->channel4));
    if(NULL != self->channel6)
        svx_channel_destroy(&(self->channel6));

    return r;
}
    
int svx_icmp_stop(svx_icmp_t *self)
{
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    
    if(!(self->running)) return 0;

    if(NULL != self->channel4)
        svx_channel_destroy(&(self->channel4));
    if(NULL != self->channel6)
        svx_channel_destroy(&(self->channel6));
    
    if(self->fd4 >= 0)
    {
        close(self->fd4);
        self->fd4 = -1;
    }
    if(self->fd6 >= 0)
    {
        close(self->fd6);
        self->fd6 = -1;
    }

    self->running = 0;
    return 0;
}

/* reference: RFC 1071 */
static uint16_t svx_icmp_checksum(uint16_t *addr, size_t len)
{
    uint32_t sum = 0;

    while(len > 1)
    {
        sum += *addr++;
        len -= 2;
    }

    if(len > 0)
        sum += *(uint8_t *)addr;

    while(sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);

    return (uint16_t)(~sum);
}

int svx_icmp_send_echo(svx_icmp_t *self, svx_inetaddr_t *addr)
{
    struct icmp      icmp4;
    struct icmp6_hdr icmp6;

    if(NULL == self || NULL == addr) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, addr:%p\n", self, addr);

    switch(SVX_INETADDR_FAMILY(addr))
    {
    case AF_INET:
        memset(&icmp4, 0, sizeof(icmp4));
        icmp4.icmp_type  = ICMP_ECHO;
        icmp4.icmp_code  = 0;
        icmp4.icmp_id    = self->icmphdr_id;
        icmp4.icmp_seq   = __sync_fetch_and_add(&(self->icmphdr_seq), 1);
        icmp4.icmp_cksum = 0;
        icmp4.icmp_cksum = svx_icmp_checksum((uint16_t *)(&icmp4), ICMP_MINLEN);
        if(0 > sendto(self->fd4, &icmp4, ICMP_MINLEN, 0, &(addr->storage.addr), SVX_INETADDR_LEN(addr)))
            SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
        return 0;
    case AF_INET6:
        memset(&icmp6, 0, sizeof(icmp6));
        icmp6.icmp6_type = ICMP6_ECHO_REQUEST;
        icmp6.icmp6_code = 0;
        icmp6.icmp6_id   = self->icmphdr_id;
        icmp6.icmp6_seq  = __sync_fetch_and_add(&(self->icmphdr_seq), 1);
        if(0 > sendto(self->fd6, &icmp6, sizeof(icmp6), 0, &(addr->storage.addr), SVX_INETADDR_LEN(addr)))
            SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
        return 0;
    default:
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "sa_family:%hu\n", SVX_INETADDR_FAMILY(addr));
    }
}
