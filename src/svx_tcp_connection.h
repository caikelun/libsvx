/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_tcp_connection.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-12-11
 */

#ifndef SVX_TCP_CONNECTION_H
#define SVX_TCP_CONNECTION_H 1

#include <stdint.h>
#include <sys/types.h>
#include "svx_looper.h"
#include "svx_channel.h"
#include "svx_circlebuf.h"
#include "svx_inetaddr.h"

/*!
 * \defgroup TCP_connection TCP_connection
 * \ingroup  Network
 *
 * \brief    This module is for packaging TCP connection's file descriptor, read buffer,
 *           write buffer, event callbacks, user private data, \c channel and \c looper.
 *           \c TCP_server and \c TCP_client use the same \c TCP_connection module.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * The type for TCP connection.
 */
typedef struct svx_tcp_connection svx_tcp_connection_t;

/*!
 * Signature for TCP connection established callback.
 *
 * \param[in] conn  The address of the TCP connection.
 * \param[in] arg   The argument which passed by \link svx_tcp_connection_create \endlink.
 */
typedef void (*svx_tcp_connection_established_cb_t)(svx_tcp_connection_t *conn, void *arg);

/*!
 * Signature for TCP connection readable callback.
 *
 * \param[in] conn  The address of the TCP connection.
 * \param[in] buf   The TCP connection's read buffer.
 * \param[in] arg   The argument which passed by \link svx_tcp_connection_create \endlink.
 */
typedef void (*svx_tcp_connection_read_cb_t)(svx_tcp_connection_t *conn, svx_circlebuf_t *buf, void *arg);

/*!
 * Signature for TCP connection write completed callback. This is an asynchronous notify
 * which means data written by \link svx_tcp_connection_write \endlink has been successfully
 * written by write(2).
 *
 * \param[in] conn  The address of the TCP connection.
 * \param[in] arg   The argument which passed by \link svx_tcp_connection_create \endlink.
 */
typedef void (*svx_tcp_connection_write_completed_cb_t)(svx_tcp_connection_t *conn, void *arg);

/*!
 * Signature for TCP connection write completed callback. This is an asynchronous notify
 * which means this module's write buffer has reached the high water mark level.
 *
 * \param[in] conn        The address of the TCP connection.
 * \param[in] water_mark  The current water mark in bytes.
 * \param[in] arg         The argument which passed by \link svx_tcp_connection_create \endlink.
 */
typedef void (*svx_tcp_connection_high_water_mark_cb_t)(svx_tcp_connection_t *conn, size_t water_mark, void *arg);

/*!
 * Signature for TCP connection closed callback.
 *
 * \param[in] conn  The address of the TCP connection.
 * \param[in] arg   The argument which passed by \link svx_tcp_connection_create \endlink.
 */
typedef void (*svx_tcp_connection_closed_cb_t)(svx_tcp_connection_t *conn, void *arg);

/*!
 * The TCP connection's callback signature and arguments collection.
 */
typedef struct
{
    svx_tcp_connection_established_cb_t      established_cb;         /*!< Established callback. */
    void                                    *established_cb_arg;     /*!< Established callback's argument. */
    svx_tcp_connection_read_cb_t             read_cb;                /*!< Readable callback. */
    void                                    *read_cb_arg;            /*!< Readable callback's argument. */
    svx_tcp_connection_write_completed_cb_t  write_completed_cb;     /*!< Write completed callback. */
    void                                    *write_completed_cb_arg; /*!< Write completed callback's argument. */
    svx_tcp_connection_high_water_mark_cb_t  high_water_mark_cb;     /*!< Write buffer high water mark callback. */
    void                                    *high_water_mark_cb_arg; /*!< Write buffer high water mark callback's argument. */
    svx_tcp_connection_closed_cb_t           closed_cb;              /*!< Closed callback. */
    void                                    *closed_cb_arg;          /*!< Closed callback's argument. */
} svx_tcp_connection_callbacks_t;

/*!
 * Signature for notifying \c TCP_server or \c TCP_client to remove the \c TCP_connection
 * handler.
 *
 * \warning  This callback is only used internally.
 *
 * \param[in] conn  The address of the TCP connection.
 * \param[in] arg   The argument which passed by \link svx_tcp_connection_create \endlink.
 */
typedef void (*svx_tcp_connection_remove_cb_t)(svx_tcp_connection_t *conn, void *arg);


/*!
 * To create a new TCP connection.
 *
 * \param[out] self                       The pointer for return the TCP connection object. 
 * \param[in]  looper                     The looper which the TCP connection associate with.
 * \param[in]  fd                         The file descriptor which the TCP connection associate with.
 * \param[in]  read_buf_min_len           The read buffer's minimum length in bytes.
 * \param[in]  read_buf_max_len           The read buffer's maximum length in bytes.
 * \param[in]  write_buf_min_len          The write buffer's minimum length in bytes.
 * \param[in]  write_buf_high_water_mark  The write buffer's high water mark in bytes.
 * \param[in]  callbacks                  The callback collections.
 * \param[in]  remove_cb                  The callback to notify \c TCP_connection 's owner the remove it.
 * \param[in]  remove_cb_arg              The \c remove_cb callback's argument.
 * \param[in]  info                       The internal private data for current TCP connection.
 *                                        The data can be retrieved by \link svx_tcp_connection_get_info \endlink.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_create(svx_tcp_connection_t **self,
                                     svx_looper_t *looper, 
                                     int fd, 
                                     size_t read_buf_min_len, 
                                     size_t read_buf_max_len, 
                                     size_t write_buf_min_len, 
                                     size_t write_buf_high_water_mark,
                                     svx_tcp_connection_callbacks_t *callbacks,
                                     svx_tcp_connection_remove_cb_t remove_cb,
                                     void *remove_cb_arg,
                                     void *info);

/*!
 * To destroy a TCP connection.
 *
 * \param[in] self  The address of the TCP connection.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_destroy(svx_tcp_connection_t *self);

/*!
 * start the TCP connection. This function will add the file descriptor the \c poller
 * and start to watch the read event.
 *
 * \param[in] self  The address of the TCP connection.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_start(svx_tcp_connection_t *self);

/*!
 * Get local address.
 *
 * \param[in]  self  The address of the TCP connection.
 * \param[out] addr  Return local address.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_get_local_addr(svx_tcp_connection_t *self, svx_inetaddr_t *addr);

/*!
 * Get peer address.
 *
 * \param[in]  self  The address of the TCP connection.
 * \param[out] addr  Return peer address.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_get_peer_addr(svx_tcp_connection_t *self, svx_inetaddr_t *addr);

/*!
 * Get the user private data.
 *
 * \warning  This function is for internal use.
 *
 * \param[in]  self  The address of the TCP connection.
 * \param[out] info  Return the internal private data.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_get_info(svx_tcp_connection_t *self, void **info);

/*!
 * Set the user private data.
 *
 * \param[in] self     The address of the TCP connection.
 * \param[in] context  The user private data.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_set_context(svx_tcp_connection_t *self, void *context);

/*!
 * Get the user private data.
 *
 * \param[in]  self     The address of the TCP connection.
 * \param[out] context  Return user private data.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_get_context(svx_tcp_connection_t *self, void **context);

/*!
 * Make the TCP connection's reference count plus one.
 *
 * \note  TCP connection's initial reference count is \c 1.
 *
 * \param[in] self  The address of the TCP connection.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_add_ref(svx_tcp_connection_t *self);

/*!
 * Make the TCP connection's reference count minus one.
 *
 * \note  When the TCP connection's reference count is reduced to \c 1,
 *        the TCP connection will be destroy automatically and safely.
 *
 * \param[in] self  The address of the TCP connection.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_del_ref(svx_tcp_connection_t *self);

/*!
 * To enable the read event callback. It is enabled by default when you give a effective
 * \link svx_tcp_connection_read_cb_t \endlink in \link svx_tcp_connection_create \endlink.
 *
 * \param[in] self  The address of the TCP connection.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_enable_read(svx_tcp_connection_t *self);

/*!
 * To disable the read event callback.
 *
 * \param[in] self  The address of the TCP connection.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_disable_read(svx_tcp_connection_t *self);

/*!
 * To enable the write completed callback. It is enabled by default when you give a effective
 * \link svx_tcp_connection_write_completed_cb_t \endlink in \link svx_tcp_connection_create \endlink.
 *
 * \param[in] self  The address of the TCP connection.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_enable_write_completed(svx_tcp_connection_t *self);

/*!
 * To disable the write completed callback.
 *
 * \param[in] self  The address of the TCP connection.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_disable_write_completed(svx_tcp_connection_t *self);

/*!
 * To enable the high water mark callback. It is enabled by default when you give a effective
 * \link svx_tcp_connection_high_water_mark_cb_t \endlink in \link svx_tcp_connection_create \endlink.
 *
 * \param[in] self  The address of the TCP connection.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_enable_high_water_mark(svx_tcp_connection_t *self);

/*!
 * To disable the high water mark callback.
 *
 * \param[in] self  The address of the TCP connection.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_disable_high_water_mark(svx_tcp_connection_t *self);

/*!
 * To shrink the read buffer.
 *
 * \param[in] self            The address of the TCP connection.
 * \param[in] freespace_keep  The free space length we should keep after shrinking the buffer.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_shrink_read_buf(svx_tcp_connection_t *self, size_t freespace_keep);

/*!
 * To shrink the write buffer.
 *
 * \param[in] self            The address of the TCP connection.
 * \param[in] freespace_keep  The free space length we should keep after shrinking the buffer.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_shrink_write_buf(svx_tcp_connection_t *self, size_t freespace_keep);

/*!
 * Send the data via the TCP connection.
 *
 * \note  This function will be return immediately. The unsend data will be saved in the 
 * \c TCP_connection module's internal write buffer. These data will be send automatically
 * when it could be.
 *
 * \param[in] self  The address of the TCP connection.
 * \param[in] buf   The data buffer.
 * \param[in] len   The length of data you want to send.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_write(svx_tcp_connection_t *self, const uint8_t *buf, size_t len);

/*!
 * Shut down the write part of the TCP connection.
 *
 * \param[in] self  The address of the TCP connection.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_shutdown_wr(svx_tcp_connection_t *self);

/*!
 * Close the TCP connection.
 *
 * \param[in] self  The address of the TCP connection.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_tcp_connection_close(svx_tcp_connection_t *self);

#ifdef __cplusplus
}
#endif

/* \} */

#endif
