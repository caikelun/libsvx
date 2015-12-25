/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_tcp_server.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-12-15
 */

#ifndef SVX_TCP_SERVER_H
#define SVX_TCP_SERVER_H 1

#include <stdint.h>
#include <sys/types.h>
#include "svx_looper.h"
#include "svx_inetaddr.h"
#include "svx_tcp_connection.h"

/*!
 * \defgroup TCP_server TCP_server
 * \ingroup  Network
 *
 * \brief    This module is used to start a TCP server, which handle 
 *           the incoming TCP client connections.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * The type for TCP server.
 */
typedef struct svx_tcp_server svx_tcp_server_t;

/*!
 * To create a new TCP server.
 *
 * \param[out] self         The pointer for return the TCP server object.
 * \param[in]  looper       The looper which the TCP server associate with.
 *                          This is the main looper for the TCP server.
 * \param[in]  listen_addr  The listen address.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_server_create(svx_tcp_server_t **self, 
                                 svx_looper_t *looper, 
                                 svx_inetaddr_t listen_addr);

/*!
 * To destroy a TCP server.
 *
 * \param[in, out] self  The second rank pointer of the TCP server.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_server_destroy(svx_tcp_server_t **self);

/*!
 * Set the I/O thread/looper's number for the TCP server.
 *
 * \param[in] self            The address of the TCP server.
 * \param[in] io_loopers_num  The I/O thread/looper's number. (default value is \c 0)
 *                            - \c 0 means no other thread will be created,
 *                              all I/O and TCP accecpt/close in the main looper.
 *                            - \c N(>0) means a thread pool with N threads will be created,
 *                              all I/O in the loopers from the thread pool,
 *                              all TCP accecpt/close in the main looper.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_server_set_io_loopers_num(svx_tcp_server_t *self,
                                             int io_loopers_num);

/*!
 * Set the TCP keepalive option.
 *
 * \param[in] self     The address of the TCP server.
 * \param[in] idle_s   The time (in seconds) the connection needs to remain idle before
 *                     TCP starts sending keepalive probes. \c 0 means disable TCP keepalive,
 *                     this is the default value.
 * \param[in] intvl_s  The time (in seconds) between individual keepalive probes.
 * \param[in] cnt      The maximum number of keepalive probes TCP should send before
 *                     dropping the connection.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_server_set_keepalive(svx_tcp_server_t *self, 
                                        time_t idle_s, 
                                        time_t intvl_s, 
                                        unsigned int cnt);

/*!
 * Set the SO_REUSEPORT for listen fd. (SO_REUSEPORT was added from Linux kernel 3.9.0)
 *
 * \param[in] self           The address of the TCP server.
 * \param[in] if_reuse_port  Whether to enable SO_REUSEPORT for the listen fd.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_server_set_reuse_port(svx_tcp_server_t *self,
                                         int if_reuse_port);

/*!
 * Set the read buffer length for all TCP connections.
 *
 * \param[in] self     The address of the TCP server.
 * \param[in] min_len  The read buffer's minimum length in bytes.
 * \param[in] max_len  The read buffer's maximum length in bytes.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_server_set_read_buf_len(svx_tcp_server_t *self, 
                                           size_t min_len,
                                           size_t max_len);

/*!
 * Set the write buffer length for all TCP connections.
 *
 * \warning  The write buffer's length is unlimited in order not to lose data.
 *           You can use \link svx_tcp_server_set_high_water_mark_cb \endlink
 *           to set a high water mark alarm value, when exceeding this mark, 
 *           a callback function will be called to notify you.
 *
 * \param[in] self     The address of the TCP server.
 * \param[in] min_len  The write buffer's minimum length in bytes.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_server_set_write_buf_len(svx_tcp_server_t *self, 
                                            size_t min_len);

/*!
 * Set all TCP connection's established callback.
 *
 * \param[in] self    The address of the TCP server.
 * \param[in] cb      The established callback.
 * \param[in] cb_arg  The established callback's argument.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_server_set_established_cb(svx_tcp_server_t *self, 
                                             svx_tcp_connection_established_cb_t cb,
                                             void *cb_arg);

/*!
 * Set all TCP connection's readable callback.
 *
 * \param[in] self    The address of the TCP server.
 * \param[in] cb      The readable callback.
 * \param[in] cb_arg  The readable callback's argument.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_server_set_read_cb(svx_tcp_server_t *self, 
                                      svx_tcp_connection_read_cb_t cb,
                                      void *cb_arg);

/*!
 * Set all TCP connection's write completed callback.
 *
 * \param[in] self    The address of the TCP server.
 * \param[in] cb      The write completed callback.
 * \param[in] cb_arg  The write completed callback's argument.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_server_set_write_completed_cb(svx_tcp_server_t *self, 
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
extern int svx_tcp_server_set_high_water_mark_cb(svx_tcp_server_t *self, 
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
extern int svx_tcp_server_set_closed_cb(svx_tcp_server_t *self, 
                                        svx_tcp_connection_closed_cb_t cb,
                                        void *cb_arg);

/*!
 * Start the TCP server.
 *
 * \param[in] self  The address of the TCP server.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_server_start(svx_tcp_server_t *self);

/*!
 * Stop the TCP server.
 *
 * \param[in] self  The address of the TCP server.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_server_stop(svx_tcp_server_t *self);

#ifdef __cplusplus
}
#endif

/* \} */

#endif
