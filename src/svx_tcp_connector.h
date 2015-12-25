/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_tcp_connector.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-12-11
 */

#ifndef SVX_TCP_CONNECTOR_H
#define SVX_TCP_CONNECTOR_H 1

#include <stdint.h>
#include <sys/types.h>
#include "svx_looper.h"
#include "svx_inetaddr.h"

/*!
 * \defgroup TCP_connector TCP_connector
 * \ingroup  Network
 *
 * \brief    This module used to do TCP connect and execute a callback function 
 *           when connected.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Signature for connected callback.
 *
 * \param[in] fd   The new TCP connection's file descriptor.
 * \param[in] arg  The argument which passed by \link svx_tcp_connector_create \endlink.
 */
typedef void (*svx_tcp_connector_connected_callback_t)(int fd, void *arg);

/*!
 * The type for TCP connector.
 */
typedef struct svx_tcp_connector svx_tcp_connector_t;

/*!
 * To create a new TCP connector.
 *
 * \param[out] self              The pointer for return the TCP connector object. 
 * \param[in]  looper            The looper which the TCP acceptor associate with.
 * \param[in]  server_addr       The server(peer) address we need to connect to it.
 * \param[in]  client_addr       The client(local) address we bind to it.
 * \param[in]  init_delay_ms     The initial time delay on milliseconds for retry 
 *                               when connect failed.
 * \param[in]  max_delay_ms      The maximum time delay on milliseconds for retry 
 *                               when connect failed.
 * \param[in]  connected_cb      The callback function for new TCP connection.
 * \param[in]  connected_cb_arg  The argument pass the callback function.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connector_create(svx_tcp_connector_t **self, 
                                    svx_looper_t *looper, 
                                    svx_inetaddr_t *server_addr,
                                    svx_inetaddr_t *client_addr,
                                    int64_t init_delay_ms,
                                    int64_t max_delay_ms,
                                    svx_tcp_connector_connected_callback_t connected_cb,
                                    void *connected_cb_arg);

/*!
 * To destroy a TCP connector.
 *
 * \param[in, out] self  The second rank pointer of the TCP connector.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connector_destroy(svx_tcp_connector_t **self);

/*!
 * Set client(local) address.
 *
 * \param[in] self         The address of the TCP connector.
 * \param[in] client_addr  The client(local) address we bind to it.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connector_set_client_addr(svx_tcp_connector_t *self, 
                                             svx_inetaddr_t *client_addr);

/*!
 * Set the initial and maximum time delay on milliseconds for retry when connect failed.
 *
 * \param[in] self           The address of the TCP connector.
 * \param[in] init_delay_ms  The initial time delay on milliseconds for retry 
 *                           when connect failed.
 * \param[in] max_delay_ms   The maximum time delay on milliseconds for retry 
 *                           when connect failed.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connector_set_retry_delay_ms(svx_tcp_connector_t *self, 
                                                int64_t init_delay_ms, 
                                                int64_t max_delay_ms);

/*!
 * Start to connect. This function will return immediately. After a connection is established
 * a callback function will be called.
 *
 * \param[in] self  The address of the TCP connector.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connector_connect(svx_tcp_connector_t *self);

/*!
 * Cancel the connecting process.
 *
 * \param[in] self  The address of the TCP connector.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connector_cancel(svx_tcp_connector_t *self);

#ifdef __cplusplus
}
#endif

/* \} */

#endif
