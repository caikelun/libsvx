/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_process.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-12-07
 */

#ifndef SVX_PROCESS_H
#define SVX_PROCESS_H 1

#include <stdint.h>
#include <sys/types.h>
#include "svx_looper.h"
#include "svx_log.h"

/*!
 * \defgroup Process Process
 * \ingroup  Base
 *
 * \brief    This is a process helper module. It contains: watchdog, daemon, signal, 
 *           singleton, user/group setting.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Signature for process start and stop callback.
 *
 * \param[in] looper  The main thread's looper.
 * \param[in] arg     The argument which passed by \link svx_process_set_callbacks \endlink.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
typedef int (*svx_process_callback_t)(svx_looper_t *looper, void *arg);

/*!
 * Signature for process config reload callback.
 *
 * \param[in] looper      The main thread's looper.
 * \param[in] is_watcher  Whether the current process is watcher process.
 * \param[in] arg         The argument which passed by \link svx_process_set_callbacks \endlink.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
typedef int (*svx_process_callback_reload_t)(svx_looper_t *looper, int is_watcher, void *arg);

/*!
 * Set log module's arguments.
 *
 * \param[in] level_file                The priority level for log-to-file.
 * \param[in] level_syslog              The priority level for log-to-syslog.
 * \param[in] level_stdout              The priority level for log-to-stdout.
 * \param[in] dirname                   The name of the directory in which to save the log files.
 * \param[in] max_size_each             The maximum size of each file.
 * \param[in] max_size_total            The maximum size of total files for worker/single process.
 * \param[in] max_size_total_watcher    The maximum size of total files for watcher process.
 * \param[in] max_size_total_signaller  The maximum size of total files for signaller process.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_process_set_log(svx_log_level_t level_file, svx_log_level_t level_syslog, svx_log_level_t level_stdout,
                               const char *dirname, off_t max_size_each, off_t max_size_total,
                               off_t max_size_total_watcher, off_t max_size_total_signaller);

/*!
 * Set crash log module's arguments.
 *
 * \param[in] dirname              The name of the directory in which to save the crash log file.
 * \param[in] head_msg             The header message which write to the beginning of the crash log file.
 * \param[in] max_dumps            The maximum number of crash log file allowed worker/single process.
 * \param[in] max_dumps_watcher    The maximum number of crash log file allowed watcher process.
 * \param[in] max_dumps_signaller  The maximum number of crash log file allowed signaller process.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_process_set_crash(const char *dirname, const char *head_msg, size_t max_dumps,
                                 size_t max_dumps_watcher, size_t max_dumps_signaller);

/*!
 * Set process mode.
 *
 * \param[in] watch_mode   Whether to turn on the watcher/worker mode. The default value s TRUE.
 *                         \c 1 for TURE, \c 0 for FLASE.
 * \param[in] daemon_mode  Whether to turn on the daemon mode. The default value is TRUE.
 *                         \c 1 for TURE, \c 0 for FLASE.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_process_set_mode(int watch_mode, int daemon_mode);

/*!
 * Set current process's effective user ID and effective group ID.
 *
 * \param[in] user   The effective user name.
 * \param[in] group  The effective group name.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_process_set_user_group(const char *user, const char *group);

/*!
 * Set current process's PID file's pathname.
 *
 * \param[in] pid_file  PID file's pathname.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_process_set_pid_file(const char *pid_file);

/*!
 * Set current process's maximum number Of open files.
 *
 * \param[in] maxfds  The maximum number Of open files.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_process_set_maxfds(int maxfds);

/*!
 * Set current process's callbacks.
 *
 * \param[in] start_cb       Callback when process has started.
 * \param[in] start_cb_arg   Argument for start_cb.
 * \param[in] reload_cb      Callback when process config file reload.
 * \param[in] reload_cb_arg  Argument for reload_cb.
 * \param[in] stop_cb        Callback when process has stoped.
 * \param[in] stop_cb_arg    Argument for stop_cb.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_process_set_callbacks(svx_process_callback_t start_cb, void *start_cb_arg,
                                     svx_process_callback_reload_t reload_cb, void *reload_cb_arg,
                                     svx_process_callback_t stop_cb, void *stop_cb_arg);

/*!
 * Start process.
 *
 * \note  This function should be called by the watcher/single process.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_process_start();

/*!
 * Reload process config file.
 *
 * \note  This function should be called by the signaller process.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_process_reload();

/*!
 * Stop process.
 *
 * \note  This function should be called by the signaller process.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_process_stop();

/*!
 * Force stop process.
 *
 * \note  This function should be called by the signaller process.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_process_force_stop();

/*!
 * Check if process is running.
 *
 * \note  This function should be called by the signaller process.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_process_is_running();

#ifdef __cplusplus
}
#endif

/* \} */

#endif
