/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_errno.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-11-12
 */

#ifndef SVX_ERRNO_H
#define SVX_ERRNO_H 1

#include <errno.h>

/*!
 * \defgroup Errno Errno
 * \ingroup  Base
 *
 * \brief    Error numbers for libsvx.
 *
 * | RANGE            | MEAN                  |
 * |------------------|-----------------------|
 * | [INT_MIN, -1]    | (UNUSED)              |
 * | 0                | OK                    |
 * | [1, 9999]        | system's error number |
 * | 10000            | (UNUSED)              |
 * | [10001, 19999]   | libsvx's error number |
 * | [20000, INT_MAX] | (UNUSED)              |
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

#define SVX_ERRNO_BASE_SVX 10000 /*!< The base of libsvx errno */

#define SVX_ERRNO_UNKNOWN    (SVX_ERRNO_BASE_SVX + 1)  /*!< Unknown error. */
#define SVX_ERRNO_INVAL      (SVX_ERRNO_BASE_SVX + 2)  /*!< Invalid argument. */
#define SVX_ERRNO_NOMEM      (SVX_ERRNO_BASE_SVX + 3)  /*!< Out of memory. */
#define SVX_ERRNO_RANGE      (SVX_ERRNO_BASE_SVX + 4)  /*!< Out of range. */
#define SVX_ERRNO_NODATA     (SVX_ERRNO_BASE_SVX + 5)  /*!< No data. */
#define SVX_ERRNO_PERM       (SVX_ERRNO_BASE_SVX + 6)  /*!< Operation not permitted. */
#define SVX_ERRNO_PIDLCK     (SVX_ERRNO_BASE_SVX + 7)  /*!< PID file locked. */
#define SVX_ERRNO_NOBUF      (SVX_ERRNO_BASE_SVX + 8)  /*!< No buffer. */
#define SVX_ERRNO_TIMEDOUT   (SVX_ERRNO_BASE_SVX + 9)  /*!< Timed out. */
#define SVX_ERRNO_REACH      (SVX_ERRNO_BASE_SVX + 10) /*!< Limit reached. */
#define SVX_ERRNO_REPEAT     (SVX_ERRNO_BASE_SVX + 11) /*!< ID Repeated. */
#define SVX_ERRNO_NOTFND     (SVX_ERRNO_BASE_SVX + 12) /*!< Not found. */
#define SVX_ERRNO_NOTSPT     (SVX_ERRNO_BASE_SVX + 13) /*!< Not support . */
#define SVX_ERRNO_NOTCONN    (SVX_ERRNO_BASE_SVX + 14) /*!< Not connected. */
#define SVX_ERRNO_INPROGRESS (SVX_ERRNO_BASE_SVX + 15) /*!< Operation now in progress. */
#define SVX_ERRNO_NOTRUN     (SVX_ERRNO_BASE_SVX + 16) /*!< Not running. */
#define SVX_ERRNO_FORMAT     (SVX_ERRNO_BASE_SVX + 17) /*!< Format error. */

/*!
 * Get a string that describes the error number.
 *
 * \param[in]  errnum  The error number.
 * \param[out] buf     The address of a buffer for holding the error message string.
 * \param[in]  buflen  The error message buffer's length.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_errno_to_str(int errnum, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

/* \} */

#endif
