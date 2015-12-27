/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_crash.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-12-07
 */

#ifndef SVX_CRASH_H
#define SVX_CRASH_H 1

#include <stdint.h>
#include <sys/types.h>

/*!
 * \defgroup Crash Crash
 * \ingroup  Base
 *
 * \brief    This module will save a crash log When process received the following signal: 
 *           SIGSEGV, SIGFPE, SIGILL, SIGBUS, SIGABRT, SIGSTKFLT.
 *           The crash log contains: System version, crash time, host name, PID, signal code,
 *           fault addr, registers value, backtrace, memory map.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * The timezone mode for crash log.
 */
typedef enum
{
    SVX_CRASH_TIMEZONE_MODE_GMT,  /*!< The GMT/UTC timezone mode. */
    SVX_CRASH_TIMEZONE_MODE_LOCAL /*!< The local timezone mode. */
} svx_crash_timezone_mode_t;

/*!
 * Signature for the crashed callback.
 *
 * \warning You can only use Async-signal-safe functions in this callback.
 *          See man signal(7) for more information.
 *
 * \param[in] fd   The file descriptor of the current crash log file.
 *                 You can use write(2) to write anything you want to the crash log file.
 * \param[in] arg  The argument which passed by \link svx_crash_set_callback \endlink.
 */
typedef void (*svx_crash_callback_t)(int fd, void *arg);

/*!
 * Set the callback function for crashed.
 *
 * \param[in] cb   The callback function.
 * \param[in] arg  The user context data.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_crash_set_callback(svx_crash_callback_t cb, void *arg);

/*!
 * Set the header message which write to the beginning of the crash log file.
 *
 * \param[in] msg  The header message.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_crash_set_head_msg(const char *msg);

/*!
 * Set the timezone mode for crash log.
 *
 * \param[in] mode  The timezone mode.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_crash_set_timezone_mode(svx_crash_timezone_mode_t mode);

/*!
 * Set the name of the directory in which to save the crash log file.
 *
 * \param[in] dirname  The directory name.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_crash_set_dirname(const char *dirname);

/*!
 * Set the crash log file's prefix.
 *
 * \param[in] prefix  The prefix.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_crash_set_prefix(const char *prefix);

/*!
 * Set the crash log file's suffix.
 *
 * \param[in] suffix  The suffix.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_crash_set_suffix(const char *suffix);

/*!
 * Set the maximum number of crash log file allowed.
 *
 * \param[in] max  The maximum number.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_crash_set_max_dumps(size_t max);

/*!
 * Register signal handlers for signal. This will start the crash log mechanism.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_crash_register_signal_handler();

/*!
 * Unregister signal handlers for signal. This will stop the crash log mechanism.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_crash_uregister_signal_handler();

#ifdef __cplusplus
}
#endif

/* \} */

#endif
