/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_channel.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-12-10
 */

#ifndef SVX_CHANNEL_H
#define SVX_CHANNEL_H 1

#include <stdint.h>
#include <sys/types.h>

/*!
 * \defgroup Channel Channel
 * \ingroup  Network
 *
 * \brief    This module is for packaging file descriptor, event and event callbacks.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * No event.
 */
#define SVX_CHANNEL_EVENT_NULL  0

/*!
 * The read event.
 */
#define SVX_CHANNEL_EVENT_READ  (1 << 0)

/*!
 * The write event.
 */
#define SVX_CHANNEL_EVENT_WRITE (1 << 1)

/*!
 * The read and write event.
 */
#define SVX_CHANNEL_EVENT_ALL   (SVX_CHANNEL_EVENT_READ | SVX_CHANNEL_EVENT_WRITE)

/*!
 * Signature for event callback.
 *
 * \param[in] arg  The argument which passed by \link svx_channel_set_read_callback \endlink or
 *                 \link svx_channel_set_write_callback \endlink.
 */
typedef void (*svx_channel_callback_t)(void *arg);

/*!
 * The type for looper.
 */
typedef struct svx_looper svx_looper_t;

/*!
 * The type for channel.
 */
typedef struct svx_channel svx_channel_t;

/*!
 * To create a new channel.
 *
 * \param[out] self    The pointer for return the channel object.
 * \param[in]  looper  The looper which the channel associate with.
 * \param[in]  fd      The file descriptor which the channel associate with.
 * \param[in]  events  The initial events.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_channel_create(svx_channel_t **self, svx_looper_t *looper, int fd, uint8_t events);

/*!
 * To destroy a channel.
 *
 * \param[in, out] self  The second rank pointer of the channel.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_channel_destroy(svx_channel_t **self);

/*!
 * Add events to the channel.
 *
 * \param[in] self    The address of the channel. 
 * \param[in] events  The events would be added.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_channel_add_events(svx_channel_t *self, uint8_t events);

/*!
 * Delete events to the channel.
 *
 * \param[in] self    The address of the channel. 
 * \param[in] events  The events would be deleted.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_channel_del_events(svx_channel_t *self, uint8_t events);

/*!
 * Set a callback for channel read event.
 *
 * \param[in] self    The address of the channel.
 * \param[in] cb      The callback function for channel read event.
 * \param[in] cb_arg  The argument pass the callback function.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_channel_set_read_callback(svx_channel_t *self, svx_channel_callback_t cb, void *cb_arg);

/*!
 * Set a callback for channel write event.
 *
 * \param[in] self    The address of the channel.
 * \param[in] cb      The callback function for channel write event.
 * \param[in] cb_arg  The argument pass the callback function.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_channel_set_write_callback(svx_channel_t *self, svx_channel_callback_t cb, void *cb_arg);

/*!
 * Set the return-event from poller.
 *
 * \note  The function will be called by poller.
 *
 * \param[in] self     The address of the channel.
 * \param[in] revents  The return-event will be set.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_channel_set_revents(svx_channel_t *self, uint8_t revents);

/*!
 * Set the private data which used by poller.
 *
 * \note  The function will be called by poller.
 *
 * \param[in] self         The address of the channel.
 * \param[in] poller_data  The private data which used by poller.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_channel_set_poller_data(svx_channel_t *self, intmax_t poller_data);

/*!
 * Get the looper which associate with the channel.
 *
 * \param[in]  self    The address of the channel.
 * \param[out] looper  Return the looper.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_channel_get_looper(svx_channel_t *self, svx_looper_t **looper);

/*!
 * Get the file descriptor which associate with the channel.
 *
 * \param[in]  self  The address of the channel.
 * \param[out] fd    Return the file descriptor.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_channel_get_fd(svx_channel_t *self, int *fd);

/*!
 * Get the events from the channel.
 *
 * \param[in]  self    The address of the channel.
 * \param[out] events  Return the events.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_channel_get_events(svx_channel_t *self, uint8_t *events);

/*!
 * Get the private data from the channel.
 *
 * \param[in]  self         The address of the channel.
 * \param[out] poller_data  Return the private data.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_channel_get_poller_data(svx_channel_t *self, intmax_t *poller_data);

/*!
 * Handle all events which returned by poller. This operation may trigger the
 * read-event-callback and/or write-event-callback.
 *
 * \param[in] self  The address of the channel.
 *
 * \return 
 */
extern int svx_channel_handle_events(svx_channel_t *self);

#ifdef __cplusplus
}
#endif

/* \} */

#endif
