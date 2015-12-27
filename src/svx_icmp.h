/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_icmp.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-12-11
 */

#ifndef SVX_ICMP_H
#define SVX_ICMP_H 1

#include <stdint.h>
#include <sys/types.h>
#include "svx_looper.h"
#include "svx_inetaddr.h"

/*!
 * \defgroup ICMP ICMP
 * \ingroup  Network
 *
 * \brief    This is a PLC(poller, looper and channel) compatible module
 *           for handling ICMP port unreachable and echo message.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Signature for icmp module callback.
 *
 * \param[in] addr  The peer address.
 * \param[in] arg   The argument which passed by \link svx_icmp_set_echoreply_cb \endlink or
 *                  \link svx_icmp_set_unreach_port_cb \endlink.
 */
typedef void (*svx_icmp_cb_t)(svx_inetaddr_t addr, void *arg);

/*!
 * The type for icmp.
 */
typedef struct svx_icmp svx_icmp_t;

/*!
 * To create a new icmp module.
 *
 * \param[out] self    The pointer for return the icmp module object.
 * \param[in]  looper  The looper which the icmp module associate with.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_icmp_create(svx_icmp_t **self, svx_looper_t *looper);

/*!
 * To destroy a icmp module.
 *
 * \param[in, out] self  The second rank pointer of the icmp module.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_icmp_destroy(svx_icmp_t **self);

/*!
 * Set a callback for ICMP echo reply.
 * 
 * \param[in] self    The address of the icmp module.
 * \param[in] cb      The callback function for ICMP echo reply.
 * \param[in] cb_arg  The argument pass the callback function.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_icmp_set_echoreply_cb(svx_icmp_t *self, svx_icmp_cb_t cb, void *cb_arg);

/*!
 * Set a callback for ICMP port unreachable.
 * 
 * \param[in] self    The address of the icmp module.
 * \param[in] cb      The callback function for ICMP port unreachable.
 * \param[in] cb_arg  The argument pass the callback function.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_icmp_set_unreach_port_cb(svx_icmp_t *self, svx_icmp_cb_t cb, void *cb_arg);

/*!
 * Start the icmp module.
 *
 * \param[in] self  The address of the icmp module.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_icmp_start(svx_icmp_t *self);

/*!
 * Stop the icmp module.
 *
 * \param[in] self  The address of the icmp module.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_icmp_stop(svx_icmp_t *self);

/*!
 * Send a ICMP echo to a specified address.
 *
 * \param[in] self  The address of the icmp module.
 * \param[in] addr  Send ICMP echo to this address.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_icmp_send_echo(svx_icmp_t *self, svx_inetaddr_t *addr);

#ifdef __cplusplus
}
#endif

/* \} */

#endif
