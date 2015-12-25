/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_util.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-11-20
 */

#ifndef SVX_UTIL_H
#define SVX_UTIL_H 1

#include <stdint.h>
#include <sys/types.h>
#include <sys/resource.h>
#include "svx_errno.h"

/*!
 * \defgroup Util Util
 * \ingroup  Base
 *
 * \brief    An utility tools collection for libsvx.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Get the current thread's ID.
 *
 * \note
 * This is a wrapper of <tt> syscall(SYS_gettid) </tt>.
 *
 * \return The current thread's ID.
 */
extern pid_t svx_util_get_tid();

/*!
 * Check if current process's real user is ROOT.
 *
 * \note
 * Checked by <tt> (0 == getuid()) </tt>.
 *
 * \return  Return \c 1 for TURE, \c 0 for FLASE.
 */
extern int svx_util_is_root();

/*!
 * Get the pathname of the current executable file.
 *
 * \param[out] pathname    A pointer to the buffer for holding the pathname.
 * \param[in]  len         The size of pathname buffer.
 * \param[out] result_len  On return it will contain the string length of pathname.
 *
 * \return  On success, return zero; on error, return an error number greater than zero. 
 */
extern int svx_util_get_exe_pathname(char *pathname, size_t len, size_t *result_len);

/*!
 * Get the basename of the current executable file.
 *
 * \param[out] basename    A pointer to the buffer for holding the basename.
 * \param[in]  len         The size of basename buffer.
 * \param[out] result_len  On return it will contain the string length of basename.
 *
 * \return  On success, return zero; on error, return an error number greater than zero. 
 */
extern int svx_util_get_exe_basename(char *basename, size_t len, size_t *result_len);

/*!
 * Get the dirname of the current executable file.
 *
 * \param[out] dirname     A pointer to the buffer for holding the dirname.
 * \param[in]  len         The size of dirname buffer.
 * \param[out] result_len  On return it will contain the string length of dirname.
 *
 * \return  On success, return zero; on error, return an error number greater than zero. 
 */
extern int svx_util_get_exe_dirname(char *dirname, size_t len, size_t *result_len);

/*!
 * Get the absolute path of the given path.
 *
 * \param[in]  path     The given path.
 * \param[out] buf      A pointer to the buffer for holding the absolute path.
 * \param[out] buf_len  The size of the buffer.
 *
 * \return  On success, return zero; on error, return an error number greater than zero. 
 */
extern int svx_util_get_absolute_path(const char *path, char *buf, size_t buf_len);

/*!
 * Set the O_NONBLOCK file status flag on the given file descriptor.
 *
 * \param[in] fd  The file descriptor.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_util_set_nonblocking(int fd);

/*!
 * Unset the O_NONBLOCK file status flag on the given file descriptor.
 *
 * \param[in] fd  The file descriptor.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_util_unset_nonblocking(int fd);

/*!
 * Set the current process to daemon mode.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_util_daemonize();

/*!
 * Open and lock a PID file for the current process.
 *
 * \param[in]  pathname  PID file's pathname.
 * \param[out] fd        Return the file descriptor of the opened PID file.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_util_pid_file_open(const char *pathname, int *fd);

/*!
 * Get PID from the given PID file.
 *
 * \param[in]  pathname  PID file's pathname.
 * \param[out] pid       Return the PID from the PID file.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_util_pid_file_getpid(const char *pathname, pid_t *pid);

/*!
 * Close and unlink the PID file.
 *
 * \param[in] pathname  PID file's pathname.
 * \param[in] fd        Point to the file descriptor of the opened PID file.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_util_pid_file_close(const char *pathname, int *fd);

/*!
 * Set the maximum number Of open files for the current process.
 *
 * \param[in] maxfds  The maximum number Of open files for the current process.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_util_set_maxfds(rlim_t maxfds);

/*!
 * Set the effective user ID and effective group ID.
 *
 * \param[in] user   The effective user name.
 * \param[in] group  The effective group name.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_util_set_user_group(const char *user, const char *group);

#ifdef __cplusplus
}
#endif

/* \} */

#endif
