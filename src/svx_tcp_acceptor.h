/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_tcp_acceptor.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-12-11
 */

#ifndef SVX_TCP_ACCEPTOR_H
#define SVX_TCP_ACCEPTOR_H 1

#include <stdint.h>
#include <sys/types.h>
#include "svx_inetaddr.h"
#include "svx_looper.h"

/*!
 * \defgroup TCP_acceptor TCP_acceptor
 * \ingroup  Network
 *
 * \brief    This module used to listen on a TCP file descriptor and execute
 *           a callback function when the file descriptor become readable.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Signature for accepted callback.
 *
 * \param[in] fd   The new TCP connection's file descriptor.
 * \param[in] arg  The argument which passed by \link svx_tcp_acceptor_create \endlink.
 */
typedef void (*svx_tcp_acceptor_accepted_callback_t)(int fd, void *arg);

/*!
 * The type for TCP acceptor.
 */
typedef struct svx_tcp_acceptor svx_tcp_acceptor_t;

/*!
 * To create a new TCP acceptor.
 *
 * \param[out] self             The pointer for return the TCP acceptor object.
 * \param[in]  looper           The looper which the TCP acceptor associate with.
 * \param[in]  listen_addr      The listen address.
 * \param[in]  accepted_cb      The callback function for new TCP connection.
 * \param[in]  accepted_cb_arg  The argument pass the callback function.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_acceptor_create(svx_tcp_acceptor_t **self, 
                                   svx_looper_t *looper, 
                                   svx_inetaddr_t *listen_addr,
                                   svx_tcp_acceptor_accepted_callback_t accepted_cb, 
                                   void *accepted_cb_arg);

/*!
 * To destroy a TCP acceptor.
 *
 * \param[in, out] self  The second rank pointer of the TCP acceptor.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_acceptor_destroy(svx_tcp_acceptor_t **self);

/*!
 * Start the TCP acceptor.
 *
 * \param[in] self           The address of the TCP acceptor.
 * \param[in] if_reuse_port  Whether to enable SO_REUSEPORT for the listen fd.
 *                           (SO_REUSEPORT was added from Linux kernel 3.9.0)
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_acceptor_start(svx_tcp_acceptor_t *self, int if_reuse_port);

/*!
 * Stop the TCP acceptor.
 *
 * \param[in] self  The address of the TCP acceptor.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_acceptor_stop(svx_tcp_acceptor_t *self);

#ifdef __cplusplus
}
#endif

/* \} */

#endif
