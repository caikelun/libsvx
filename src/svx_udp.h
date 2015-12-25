/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_udp.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-12-11
 */

#ifndef SVX_UDP_H
#define SVX_UDP_H 1

#include <stdint.h>
#include <sys/types.h>
#include "svx_inetaddr.h"

/*!
 * \defgroup UDP UDP
 * \ingroup  Network
 *
 * \brief    This is a UDP helper module which contain UDP server, UDP client,
 *           unicast and multicast.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * To create a file descriptor for UDP server which bind to the given 
 * server (local) address.
 *
 * \param[out] fd           Return the file descriptor for UDP server.
 * \param[in]  server_addr  The UDP server will bind to this address.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_server(int *fd, const svx_inetaddr_t *server_addr);

/*!
 * To create a file descriptor for UDP client which bind to the given 
 * client (local) address and connect to the given server (peer) address.
 *
 * \param[out] fd           Return the file descriptor for UDP server.
 * \param[in]  server_addr  The UDP client connect to this address.
 *                          NULL for do not connecting.
 * \param[in]  client_addr  The UDP client bind to this address.
 *                          NULL for do not binding.
 * \param[in]  family       If both \c server_addr and \c client_addr are NULL,
 *                          use the argument to determine the domain for socket(2).
 *                          \c AF_INET for IPv4, \c AF_INET6 for IPv6.
 *                          If either \c server_addr or \c client_addr are NOT
 *                          NULL, this argument will be ignored.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_client(int *fd, const svx_inetaddr_t *server_addr, const svx_inetaddr_t *client_addr, int family);

/*!
 * Get the kernel's maximum read buffer length for the given file descriptor.
 *
 * \param[in]  fd       The file descriptor.
 * \param[out] max_len  Return the kernel's maximum read buffer length.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_get_kernel_read_buf_len(int fd, size_t *max_len);

/*!
 * Set the kernel's maximum read buffer length for the given file descriptor.
 *
 * \param[in]  fd       The file descriptor.
 * \param[out] max_len  The kernel's maximum read buffer length.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_set_kernel_read_buf_len(int fd, size_t max_len);

/*!
 * Get the kernel's maximum write buffer length for the given file descriptor.
 *
 * \param[in]  fd       The file descriptor.
 * \param[out] max_len  Return the kernel's maximum write buffer length.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_get_kernel_write_buf_len(int fd, size_t *max_len);

/*!
 * Set the kernel's maximum write buffer length for the given file descriptor.
 *
 * \param[in]  fd       The file descriptor.
 * \param[out] max_len  The kernel's maximum write buffer length.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_set_kernel_write_buf_len(int fd, size_t max_len);

/*!
 * Let the specified file descriptor to join a multicast group on a specified network interface.
 *
 * \note  The network interface here is only for receiving multicast datagrams.
 *        Use \link svx_udp_mcast_set_if \endlink to set the network interface
 *        for sending multicast datagrams.
 *
 * \param[in] fd       The file descriptor.
 * \param[in] grp      The multicast group address.
 * \param[in] ifindex  Ths network interface index.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_mcast_join(int fd, const svx_inetaddr_t *grp, unsigned int ifindex);

/*!
 * Let the specified file descriptor to leave a multicast group on a specified network interface.
 *
 * \param[in] fd       The file descriptor.
 * \param[in] grp      The multicast group address.
 * \param[in] ifindex  Ths network interface index.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_mcast_leave(int fd, const svx_inetaddr_t *grp, unsigned int ifindex);

/*!
 * Let the specified file descriptor to join a source-specific multicast group 
 * on a specified network interface.
 *
 * \param[in] fd       The file descriptor.
 * \param[in] src      The source (peer) address.
 * \param[in] grp      The multicast group address.
 * \param[in] ifindex  Ths network interface index.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_mcast_join_source_group(int fd, const svx_inetaddr_t *src, const svx_inetaddr_t *grp, unsigned int ifindex);

/*!
 * Let the specified file descriptor to leave a source-specific multicast group 
 * on a specified network interface.
 *
 * \param[in] fd       The file descriptor.
 * \param[in] src      The source (peer) address.
 * \param[in] grp      The multicast group address.
 * \param[in] ifindex  Ths network interface index.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_mcast_leave_source_group(int fd, const svx_inetaddr_t *src, const svx_inetaddr_t *grp, unsigned int ifindex);

/*!
 * To block a source address for the specified file descriptor on the specified 
 * multicast group and network interface.
 *
 * \param[in] fd       The file descriptor.
 * \param[in] src      The source (peer) address.
 * \param[in] grp      The multicast group address.
 * \param[in] ifindex  Ths network interface index.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_mcast_block_source(int fd, const svx_inetaddr_t *src, const svx_inetaddr_t *grp, unsigned int ifindex);

/*!
 * To unblock a source address for the specified file descriptor on the specified 
 * multicast group and network interface.
 *
 * \param[in] fd       The file descriptor.
 * \param[in] src      The source (peer) address.
 * \param[in] grp      The multicast group address.
 * \param[in] ifindex  Ths network interface index.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_mcast_unblock_source(int fd, const svx_inetaddr_t *src, const svx_inetaddr_t *grp, unsigned int ifindex);

/*!
 * Get the TTL of outgoing multicast datagrams.
 *
 * \param[in]  fd   The file descriptor.
 * \param[out] ttl  Return the outgoing multicast datagrams TTL.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_mcast_get_ttl(int fd, unsigned int *ttl);

/*!
 * Set the TTL of outgoing multicast datagrams.
 *
 * \param[in] fd   The file descriptor.
 * \param[in] ttl  The outgoing multicast datagrams TTL.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_mcast_set_ttl(int fd, unsigned int ttl);

/*!
 * Get the network interface index of outgoing multicast datagrams.
 *
 * \param[in]  fd       The file descriptor.
 * \param[out] ifindex  Return the outgoing multicast datagrams network interface index.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_mcast_get_if(int fd, unsigned int *ifindex);

/*!
 * Set the network interface index of outgoing multicast datagrams.
 *
 * \note  The network interface here is only for sending multicast datagrams.
 *        Use \link svx_udp_mcast_join \endlink or \link svx_udp_mcast_join_source_group \endlink
 *        to join a multicast group for receiving multicast datagrams.
 *
 * \param[in] fd       The file descriptor.
 * \param[in] ifindex  The outgoing multicast datagrams network interface index.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_mcast_set_if(int fd, unsigned int ifindex);

/*!
 * Get loopback of outgoing multicast datagrams.
 *
 * \param[in]  fd    The file descriptor.
 * \param[out] loop  Return the outgoing multicast datagrams loopback.
 *                   \c 1 for enable, \c 0 for disable.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_mcast_get_loop(int fd, unsigned int *loop);

/*!
 * Set loopback of outgoing multicast datagrams.
 *
 * \param[in] fd    The file descriptor.
 * \param[in] loop  The outgoing multicast datagrams loopback.
 *                   \c 1 for enable, \c 0 for disable.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_udp_mcast_set_loop(int fd, unsigned int loop);

#ifdef __cplusplus
}
#endif

/* \} */

#endif
