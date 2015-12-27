/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_tcp_client.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-12-22
 */

#ifndef SVX_TCP_CLIENT_H
#define SVX_TCP_CLIENT_H 1

#include <stdint.h>
#include <sys/types.h>
#include "svx_looper.h"
#include "svx_inetaddr.h"
#include "svx_tcp_connection.h"

/*!
 * \defgroup TCP_client TCP_client
 * \ingroup  Network
 *
 * \brief    This module is used to start a TCP client, which handle 
 *           the TCP connection.
 *           This module will only keep one TCP connection.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * The type for TCP client.
 */
typedef struct svx_tcp_client svx_tcp_client_t;

/*!
 * To create a new TCP client.
 *
 * \param[out] self         The pointer for return the TCP client object.
 * \param[in]  looper       The looper which the TCP client associate with.
 * \param[in]  server_addr  The TCP server's listen address.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_client_create(svx_tcp_client_t **self, 
                                 svx_looper_t *looper, 
                                 svx_inetaddr_t server_addr);

/*!
 * To destroy a TCP client.
 *
 * \param[in, out] self  The second rank pointer of the TCP client.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_client_destroy(svx_tcp_client_t **self);

/*!
 * Set the local address for TCP client.
 *
 * \note  If not set the local address, this module will let the kernel to select 
 *        local IP and port automatically.
 *
 * \param[in] self         The address of the TCP client.
 * \param[in] client_addr  The local address.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_client_set_client_addr(svx_tcp_client_t *self,
                                          svx_inetaddr_t client_addr);

/*!
 * Set the time delay (in milliseconds) when connecting failed.
 *
 * \param[in] self           The address of the TCP client.
 * \param[in] init_delay_ms  The initial time delay (in milliseconds) between each retrying.
 *                           The default value is \c 500 milliseconds.
 * \param[in] max_delay_ms   The maximum time delay (in milliseconds) between each retrying.
 *                           The default value is \c 10,000 milliseconds (\c 10 seconds).
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_client_set_reconnect_delay(svx_tcp_client_t *self, 
                                              int64_t init_delay_ms, 
                                              int64_t max_delay_ms);

/*!
 * Set the read buffer length for TCP connection.
 *
 * \param[in] self     The address of the TCP server.
 * \param[in] min_len  The read buffer's minimum length in bytes.
 * \param[in] max_len  The read buffer's maximum length in bytes.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_client_set_read_buf_len(svx_tcp_client_t *self, 
                                           size_t min_len,
                                           size_t max_len);

/*!
 * Set the write buffer length for TCP connection.
 *
 * \warning  The write buffer's length is unlimited in order not to lose data.
 *           You can use \link svx_tcp_client_set_high_water_mark_cb \endlink
 *           to set a high water mark alarm value, when exceeding this mark, 
 *           a callback function will be called to notify you.
 *
 * \param[in] self     The address of the TCP server.
 * \param[in] min_len  The write buffer's minimum length in bytes.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_client_set_write_buf_len(svx_tcp_client_t *self, 
                                            size_t min_len);

/*!
 * Set TCP connection's established callback.
 *
 * \param[in] self    The address of the TCP server.
 * \param[in] cb      The established callback.
 * \param[in] cb_arg  The established callback's argument.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_client_set_established_cb(svx_tcp_client_t *self, 
                                             svx_tcp_connection_established_cb_t cb,
                                             void *cb_arg);

/*!
 * Set TCP connection's readable callback.
 *
 * \param[in] self    The address of the TCP server.
 * \param[in] cb      The readable callback.
 * \param[in] cb_arg  The readable callback's argument.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_client_set_read_cb(svx_tcp_client_t *self, 
                                      svx_tcp_connection_read_cb_t cb,
                                      void *cb_arg);

/*!
 * Set TCP connection's write completed callback.
 *
 * \param[in] self    The address of the TCP server.
 * \param[in] cb      The write completed callback.
 * \param[in] cb_arg  The write completed callback's argument.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_client_set_write_completed_cb(svx_tcp_client_t *self, 
                                                 svx_tcp_connection_write_completed_cb_t cb,
                                                 void *cb_arg);

/*!
 * Set all TCP connection's write buffer high water mark callback.
 *
 * \param[in] self             The address of the TCP server.
 * \param[in] cb               The write buffer high water mark callback.
 * \param[in] cb_arg           The write buffer high water mark callback's argument.
 * \param[in] high_water_mark  The write buffer's high water mark in bytes.
 *                             If the water mark is higher than this valus, the \c cb
 *                             will be called.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_client_set_high_water_mark_cb(svx_tcp_client_t *self, 
                                                 svx_tcp_connection_high_water_mark_cb_t cb,
                                                 void *cb_arg,
                                                 size_t high_water_mark);

/*!
 * Set all TCP connection's closed callback.
 *
 * \param[in] self    The address of the TCP server.
 * \param[in] cb      The closed callback.
 * \param[in] cb_arg  The closed callback's argument.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_client_set_closed_cb(svx_tcp_client_t *self, 
                                        svx_tcp_connection_closed_cb_t cb,
                                        void *cb_arg);

/*!
 * Start to connect. This function will return immediately. After connection is established
 * a callback function (set by \link svx_tcp_client_set_established_cb \endlink) will be called.
 *
 * \param[in] self  The address of the TCP client.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_client_connect(svx_tcp_client_t *self);

/*!
 * Cancel the connecting process.
 *
 * \param[in] self  The address of the TCP client.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_client_cancel(svx_tcp_client_t *self);

/*!
 * Close the established connection, or cancel the connecting process.
 *
 * \param[in] self  The address of the TCP client.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_client_disconnect(svx_tcp_client_t *self);

/*!
 * Close the established connection, or cancel the connecting process.
 * Then start to connect again.
 *
 * \param[in] self  The address of the TCP client.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_client_reconnect(svx_tcp_client_t *self);

#ifdef __cplusplus
}
#endif

/* \} */

#endif
