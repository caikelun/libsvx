/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_inetaddr.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-10-29
 */

#ifndef SVX_INETADDR_H
#define SVX_INETADDR_H 1

#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

/*!
 * \defgroup Inetaddr Inetaddr
 * \ingroup  Network
 *
 * \brief    This is a wrapper of sock address for Compatible with IPv4 and IPv6. 
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Wrapper of sock address for Compatible IPv4 and IPv6.
 *
 * \warning Do not modify the value of this struct directly, 
 *          buf to use the svx_inetaddr_from_* functions.
 */
typedef struct
{
    union
    {
        struct sockaddr     addr;  /*!< The socket address for structure pointer. */
        struct sockaddr_in  addr4; /*!< The socket address for IPv4. */
        struct sockaddr_in6 addr6; /*!< The socket address for IPv6. */
    } storage; /*!< The socket address. */
} svx_inetaddr_t;

/*!
 * Get the maximum string length of IP address. (X:X:X:X:X:X:X:X%ifname)
 */
#define SVX_INETADDR_STR_IP_LEN                                         \
    (INET6_ADDRSTRLEN + 1 + IF_NAMESIZE + 1)

/*!
 * Get the maximum string length of IP address and port. ([X:X:X:X:X:X:X:X%ifname]:65535)
 */
#define SVX_INETADDR_STR_ADDR_LEN                                       \
    (1 + INET6_ADDRSTRLEN + 1 + IF_NAMESIZE + 1 + 1 + 5 + 1)

/*!
 * Get the family of the IP address.
 */
#define SVX_INETADDR_FAMILY(self)                                       \
    ((self)->storage.addr.sa_family)

/*!
 * To determine whether it is a IPv4 address.
 */
#define SVX_INETADDR_IS_IPV4(self)                                      \
    (AF_INET == SVX_INETADDR_FAMILY(self) ? 1 : 0)

/*!
 * To determine whether it is a IPv4 address.
 */
#define SVX_INETADDR_IS_IPV6(self)                                      \
    (AF_INET6 == SVX_INETADDR_FAMILY(self) ? 1 : 0)

/*!
 * Get the length of the inetaddr.
 */
#define SVX_INETADDR_LEN(self)                                          \
    ((socklen_t)((SVX_INETADDR_IS_IPV4(self)) ?                         \
                 sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6)))

/*!
 * Get the protocol of the inetaddr.
 */
#define SVX_INETADDR_PROTO_LEVEL(self)                                  \
    (SVX_INETADDR_IS_IPV4(self) ? IPPROTO_IP : IPPROTO_IPV6)

/*!
 * To check if this inetaddr has an unspecified port.
 */
#define SVX_INETADDR_IS_PORT_UNSPECIFIED(self)                          \
    ((SVX_INETADDR_IS_IPV4(self)) ?                                     \
     (0 == (self)->storage.addr4.sin_port ? 1 : 0) :                    \
     (0 == (self)->storage.addr6.sin6_port ? 1 : 0))

/*!
 * To check if this inetaddr has an IP address.
 */
#define SVX_INETADDR_IS_IP_UNSPECIFIED(self)                            \
    ((SVX_INETADDR_IS_IPV4(self)) ?                                     \
     (INADDR_ANY == ntohl((self)->storage.addr4.sin_addr.s_addr) ? 1 : 0) : \
     (IN6_IS_ADDR_UNSPECIFIED(&((self)->storage.addr6.sin6_addr)) ? 1 : 0))
    
/*!
 * To check if this inetaddr has a loopback IP address.
 */
#define SVX_INETADDR_IS_IP_LOOPBACK(self)                               \
    ((SVX_INETADDR_IS_IPV4(self)) ?                                     \
     (INADDR_LOOPBACK == ntohl((self)->storage.addr4.sin_addr.s_addr) ? 1 : 0) : \
     (IN6_IS_ADDR_LOOPBACK(&((self)->storage.addr6.sin6_addr)) ? 1 : 0))

/*!
 * To check if this inetaddr has a multicast IP address.
 */
#define SVX_INETADDR_IS_IP_MULTICAST(self)                              \
    ((SVX_INETADDR_IS_IPV4(self)) ?                                     \
     (IN_MULTICAST(ntohl((self)->storage.addr4.sin_addr.s_addr)) ? 1 : 0) : \
     (IN6_IS_ADDR_MULTICAST(&((self)->storage.addr6.sin6_addr)) ? 1 : 0))


/*!
 * Initialize an inetaddr struct form IP and port.
 *
 * | ADDRESS TYPE | IPv4        | IPv6                     |
 * |--------------|-------------|--------------------------|
 * | ANY          | "0.0.0.0"   | "::"                     |
 * | LOOPBACK     | "127.0.0.1" | "::1"                    |
 * | LINK-LOCAL   | /           | "X:X:X:X:X:X:X:X%ifname" |
 *
 * | PORT TYPE | Port       |
 * |-----------|------------|
 * | RANDOM    | 0          |
 * | SPECIFIED | [1, 65535] |
 *
 * \param[in] self  The address of inetaddr.
 * \param[in] ip    The IP address string.
 * \param[in] port  The port number.
 *
 * \return  On success, return zero; on error, return an error number greater than zero. 
 */
extern int svx_inetaddr_from_ipport(svx_inetaddr_t *self, const char *ip, uint16_t port);

/*!
 * Initialize an inetaddr struct form a sockaddr struct.
 *
 * \param[in] self  The address of inetaddr.
 * \param[in] addr  The address of sockaddr.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_inetaddr_from_addr(svx_inetaddr_t *self, const struct sockaddr *addr);

/*!
 * Initialize an inetaddr struct of local address by a file descriptor.
 *
 * \param[in] self  The address of inetaddr.
 * \param[in] fd    The file descriptor.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_inetaddr_from_fd_local(svx_inetaddr_t *self, int fd);

/*!
 * Initialize an inetaddr struct of peer address by a file descriptor.
 *
 * \param[in] self  The address of inetaddr.
 * \param[in] fd    The file descriptor.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_inetaddr_from_fd_peer(svx_inetaddr_t *self, int fd);

/*!
 * Get IP and port from a inetaddr struct.
 *
 * \param[in]  self    The address of inetaddr.
 * \param[out] ip      The address of a buffer for holding the output IP string.
 * \param[in]  ip_len  The IP buffer's length.
 * \param[out] port    The address of a uint16_t for holding the output port.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_inetaddr_get_ipport(svx_inetaddr_t *self, char *ip, size_t ip_len, uint16_t *port);

/*!
 * Get a string of socket address (IP and port) from a inetaddr struct.
 *
 * \param[in]  self  The address of inetaddr.
 * \param[out] addr  The address of a buffer for holding the socket address string.
 * \param[in]  len   The socket address buffer's length.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_inetaddr_get_addr_str(svx_inetaddr_t *self, char *addr, size_t len);

/*!
 * Compare the two inetaddr.
 *
 * \param[in] sa1  The first inetaddr.
 * \param[in] sa2  The second inetaddr.
 *
 * \return It returns an integer less than, equal to, or greater than zero if sa1
 *         is found, respectively, to be less than, to match, or be greater than sa2.
 */
extern int svx_inetaddr_cmp_addr(svx_inetaddr_t *sa1, svx_inetaddr_t *sa2);

/*!
 * Compare the two inetaddr's IP.
 *
 * \param[in] sa1  The first inetaddr.
 * \param[in] sa2  The second inetaddr.
 *
 * \return It returns an integer less than, equal to, or greater than zero if sa1's IP
 *         is found, respectively, to be less than, to match, or be greater than sa2's IP.
 */
extern int svx_inetaddr_cmp_ip(svx_inetaddr_t *sa1, svx_inetaddr_t *sa2);

/*!
 * Compare the two inetaddr's port.
 *
 * \param[in] sa1  The first inetaddr.
 * \param[in] sa2  The second inetaddr.
 *
 * \return It returns an integer less than, equal to, or greater than zero if sa1's port
 *         is found, respectively, to be less than, to match, or be greater than sa2's port.
 */
extern int svx_inetaddr_cmp_port(svx_inetaddr_t *sa1, svx_inetaddr_t *sa2);

#ifdef __cplusplus
}
#endif

/* \} */

#endif
