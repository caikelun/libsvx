/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_notifier.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-12-10
 */

#ifndef SVX_NOTIFIER_H
#define SVX_NOTIFIER_H 1

#include <stdint.h>
#include <sys/types.h>

/*!
 * \defgroup Notifier Notifier
 * \ingroup  Network
 *
 * \brief    A Notifier which can be activated by a file descriptor's write event.
 *           The module use eventfd(2) or pipe(2) to achieve according to
 *           the kernel version.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * The type for notifier.
 */
typedef struct svx_notifier svx_notifier_t;

/*!
 * To create a new notifier.
 *
 * \param[out] self  The pointer for return the notifier object.
 * \param[out] fd    Return the file descriptor associate with the notifier.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_notifier_create(svx_notifier_t **self, int *fd);

/*!
 * To destroy a notifier.
 *
 * \param[in, out] self  The second rank pointer of the notifier.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_notifier_destroy(svx_notifier_t **self);

/*!
 * Send a notify. This operation will cause the file descriptor returned by 
 * \link svx_notifier_create \endlink become readable.
 *
 * \param[in] self  The address of the notifier.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_notifier_send(svx_notifier_t *self);

/*!
 * Receive the notify. This operation will cause the file descriptor no longer readable.
 *
 * \param[in] self  The address of the notifier.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_notifier_recv(svx_notifier_t *self);

#ifdef __cplusplus
}
#endif

/* \} */

#endif
