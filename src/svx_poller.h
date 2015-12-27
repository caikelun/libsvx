/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_poller.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-12-09
 */

#ifndef SVX_POLLER_H
#define SVX_POLLER_H 1

#include <stdint.h>
#include <sys/types.h>
#include "svx_channel.h"

/*!
 * \defgroup Poller Poller
 * \ingroup  Network
 *
 * \brief    The interface of I/O multiplexing.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Signature collection for a realization of I/O multiplexing.
 */
typedef struct
{
    /*!
     * To create a new poller.
     *
     * \param[out] self  The pointer for return the poller object.
     *
     * \return  On success, return zero; on error, return an error number greater than zero.
     */
    int (*create)(void **self);
    
    /*!
     * To initialize the given channel for the current poller.
     *
     * \param[in] self     The address of the poller.
     * \param[in] channel  The channel we want to initialize.
     *
     * \return  On success, return zero; on error, return an error number greater than zero.
     */
    int (*init_channel)(void *self, svx_channel_t *channel);

    /*!
     * To update the poller's status according to the given channel.
     *
     * \param[in] self     The address of the poller.
     * \param[in] channel  The channel with the newest status.
     *
     * \return  On success, return zero; on error, return an error number greater than zero.
     */
    int (*update_channel)(void *self, svx_channel_t *channel);

    /*!
     * Wait for some event on the given channels.
     *
     * \param[in]  self                  The address of the poller.
     * \param[out] active_channels       Return avtive channels.
     * \param[in]  active_channels_size  The \c active_channels maximum size.
     * \param[out] active_channels_used  Return the avtive channels's count in \c active_channels.
     * \param[in]  timeout_ms            specifies the number of milliseconds that \c poll will block.
     *                                   Specifying a timeout of -1 causes \c poll to  block  indefinitely,
     *                                   while specifying a timeout equal to zero cause \c poll to return 
     *                                   immediately, even if no events are available.
     *
     * \return  On success, return zero; on error, return an error number greater than zero.
     */
    int (*poll)(void *self, svx_channel_t **active_channels, size_t active_channels_size, 
                size_t *active_channels_used, int timeout_ms);

    /*!
     * To destroy a poller.
     *
     * \param[in, out] self  The second rank pointer of the poller.
     *
     * \return  On success, return zero; on error, return an error number greater than zero.
     */
    int (*destroy)(void **self);
} svx_poller_handlers_t;

/*!
 * The type for poller.
 */
typedef struct svx_poller svx_poller_t;

/*!
 * To create a new poller.
 *
 * \param[out] self  The pointer for return the poller object.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_poller_create(svx_poller_t **self);

/*!
 * To initialize the given channel for the current poller.
 *
 * \param[in] self     The address of the poller.
 * \param[in] channel  The channel we want to initialize.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_poller_init_channel(svx_poller_t *self, svx_channel_t *channel);

/*!
 * To update the poller's status according to the given channel.
 *
 * \param[in] self     The address of the poller.
 * \param[in] channel  The channel with the newest status.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_poller_update_channel(svx_poller_t *self, svx_channel_t *channel);

/*!
 * Wait for some event on the given channels.
 *
 * \param[in]  self                  The address of the poller.
 * \param[out] active_channels       Return avtive channels.
 * \param[in]  active_channels_size  The \c active_channels maximum size.
 * \param[out] active_channels_used  Return the avtive channels's count in \c active_channels.
 * \param[in]  timeout_ms            specifies the number of milliseconds that \c poll will block.
 *                                   Specifying a timeout of -1 causes \c poll to  block  indefinitely,
 *                                   while specifying a timeout equal to zero cause \c poll to return 
 *                                   immediately, even if no events are available.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_poller_poll(svx_poller_t *self, svx_channel_t **active_channels, size_t active_channels_size, 
                           size_t *active_channels_used, int timeout_ms);

/*!
 * To destroy a poller.
 *
 * \param[in, out] self  The second rank pointer of the poller.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_poller_destroy(svx_poller_t **self);

/*!
 * The type for fix a specific poller.
 *
 * \warning  The type is only used for test. Do NOT use this in a real program.
 */
typedef enum
{
    SVX_POLLER_FIXED_NONE,  /*!< Automatic election. */
    SVX_POLLER_FIXED_EPOLL, /*!< Use epoll. */
    SVX_POLLER_FIXED_POLL,  /*!< Use poll. */
    SVX_POLLER_FIXED_SELECT /*!< Use select. */
} svx_poller_fixed_t;

/*!
 * To choose a specific poller to use.
 *
 * \warning  The variable is only used for test. Do NOT use this in a real program.
 */
extern svx_poller_fixed_t svx_poller_fixed;

#ifdef __cplusplus
}
#endif

/* \} */

#endif
