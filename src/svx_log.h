/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

/*!
 * \file   svx_log.h
 * \brief  
 *
 * \author Alan Choi
 * \date   2015-12-07
 */

#ifndef SVX_LOG_H
#define SVX_LOG_H 1

#include <stdint.h>
#include <sys/types.h>

/*!
 * \defgroup Log Log
 * \ingroup  Base
 *
 * \brief    This module can save logs to file, syslog and stdout.
 *           The log to file mechanism use asynchronous writing mode that won't 
 *           block the calling thread.
 *
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * The priority level for log. (The same as Linux's syslog(3))
 */
typedef enum
{
    SVX_LOG_LEVEL_NONE = -1, /*!< Log nothing. */
    SVX_LOG_LEVEL_EMERG,     /*!< System is unusable. */
    SVX_LOG_LEVEL_ALERT,     /*!< Action must be taken immediately. */
    SVX_LOG_LEVEL_CRIT,      /*!< Critical conditions. */
    SVX_LOG_LEVEL_ERR,       /*!< Error conditions. */
    SVX_LOG_LEVEL_WARNING,   /*!< Warning conditions. */
    SVX_LOG_LEVEL_NOTICE,    /*!< Normal, but significant, condition. */
    SVX_LOG_LEVEL_INFO,      /*!< Informational message. */
    SVX_LOG_LEVEL_DEBUG      /*!< Febug-level message. */
} svx_log_level_t;

/*!
 * The timezone mode for log.
 */
typedef enum
{
    SVX_LOG_TIMEZONE_MODE_GMT,  /*!< The GMT/UTC timezone mode. */
    SVX_LOG_TIMEZONE_MODE_LOCAL /*!< The local timezone mode. */
} svx_log_timezone_mode_t;

/*!
 * The errno mode for log.
 */
typedef enum
{
    SVX_LOG_ERRNO_MODE_NONE,   /*!< Neither error number nor error message. */
    SVX_LOG_ERRNO_MODE_NUM,    /*!< Only error number. */
    SVX_LOG_ERRNO_MODE_NUM_STR /*!< Error number and error message. */
} svx_log_errno_mode_t;

/*!
 * Signature for get a string that describes the error number.
 *
 * \param[in]  errnum  The error number.
 * \param[out] buf     The address of a buffer for holding the error message string.
 * \param[in]  buflen  The error message buffer's length.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
typedef int (*svx_log_errno_to_str_t)(int errnum, char *buf, size_t buflen);

/*! The priority level for log-to-file. */
extern svx_log_level_t svx_log_level_file;

/*! The priority level for log-to-syslog. */
extern svx_log_level_t svx_log_level_syslog;

/*! The priority level for log-to-stdout. */
extern svx_log_level_t svx_log_level_stdout;

/*!
 * Initialize the log module. 
 *
 * \note The log-to-file will run in synchronous writing mode.
 *       You can call \link svx_log_file_to_async_mode \endlink
 *       switch to asynchronous writing mode.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_log_file_init();

/*!
 * Uninitialize the log module. 
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_log_file_uninit();

/*!
 * Forces a write of all buffered logs to disk. 
 * This is a synchronous call with a timeout in millisecond.
 *
 * \param[in] timeout_ms  The timeout in millisecond.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_log_file_flush(int timeout_ms);

/*!
 * Set the name of the directory in which to save the log files.
 *
 * \param[in] dirname  The directory name.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_log_file_set_dirname(const char *dirname);

/*!
 * Set the log file's prefix.
 *
 * \param[in] prefix  The prefix.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_log_file_set_prefix(const char *prefix);

/*!
 * Set the log file's suffix.
 *
 * \param[in] suffix  The suffix.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_log_file_set_suffix(const char *suffix);

/*!
 * Set the maximum size of each file and total files.
 *
 * \param[in] each   The maximum size of each file.
 * \param[in] total  The maximum size of total files.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_log_file_set_size_max(off_t each, off_t total);

/*!
 * Set the cache size of each block and total blocks.
 *
 * \warning These parameters will affect the performance and reliability.
 *          Don't change the default value unless you known what you are doing.
 *
 * \param[in] each   The cache size of each block.
 * \param[in] total  The cache size of total blocks.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_log_file_set_cache_size(size_t each, size_t total);

/*!
 * Set the interval in seconds for automatic flushing cache to disk file. 
 *
 * \param[in] seconds  The interval in seconds.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_log_file_set_cache_flush_interval(int seconds);

/*!
 * Switch to asynchronous writing mode for log-to-file.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_log_file_to_async_mode();

/*!
 * Check if current log-to-file mode is asynchronous writing mode.
 *
 * \return  Return \c 1 for true, \c 0 for false.
 */
extern int svx_log_file_is_async_mode();

/*!
 * Set the timezone mode for log.
 *
 * \param[in] mode  The timezone mode.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_log_set_timezone_mode(svx_log_timezone_mode_t mode);

/*!
 * Set the errno mode for log.
 *
 * \param[in] mode  The errno mode.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_log_set_errno_mode(svx_log_errno_mode_t mode);

/*!
 * Set the callback function for convert error number to error message.
 *
 * \note  The default callback function is \link svx_errno_to_str \endlink.
 *
 * \param[in] errno_to_str  The callback function.
 *
 * \return  On success, return zero; on error, return an error number greater than zero.
 */
extern int svx_log_set_errno_to_str(svx_log_errno_to_str_t errno_to_str);

/*!
 * To append a new log.
 *
 * \param[in] level     The priority level.
 * \param[in] file      The matching file name.
 * \param[in] line      The matching line number.
 * \param[in] function  The matching function name.
 * \param[in] errnum    The current error number.
 * \param[in] format    The message format same as printf(3).
 * \param[in]  ...      The arguments same as printf(3).
 */
extern void svx_log_errno_msg(svx_log_level_t level, const char *file, int line, const char *function,
                              int errnum, const char *format, ...);

/*! Check range by priority level. */
#define SVX_LOG_RANGE_BASE(levelname)                                   \
    if(svx_log_level_file   >= SVX_LOG_LEVEL_##levelname ||             \
       svx_log_level_syslog >= SVX_LOG_LEVEL_##levelname ||             \
       svx_log_level_stdout >= SVX_LOG_LEVEL_##levelname)

/*! Append new log with priority level. */
#define SVX_LOG_ERRNO_BASE(levelname, errnum, format, ...)              \
    do                                                                  \
    {                                                                   \
        SVX_LOG_RANGE_BASE(levelname)                                   \
        {                                                               \
            svx_log_errno_msg(SVX_LOG_LEVEL_##levelname,                \
                              __BASE_FILE__, __LINE__, __FUNCTION__,    \
                              errnum, format, ##__VA_ARGS__);           \
        }                                                               \
        else                                                            \
        {                                                               \
            (void)errnum;                                               \
        }                                                               \
    } while(0)

/*! Append new log with priority level then RETURN. */
#define SVX_LOG_ERRNO_RETURN_BASE(levelname, errnum, format, ...)       \
    do                                                                  \
    {                                                                   \
        SVX_LOG_ERRNO_BASE(levelname, errnum, format, ##__VA_ARGS__);   \
        return errnum;                                                  \
    } while(0)

/*! Append new log with priority level then GOTO. */
#define SVX_LOG_ERRNO_GOTO_BASE(tag, levelname, errnum, format, ...)    \
    do                                                                  \
    {                                                                   \
        SVX_LOG_ERRNO_BASE(levelname, errnum, format, ##__VA_ARGS__);   \
        goto tag;                                                       \
    } while(0)

/*! Check range by EMERG priority level. */
#define SVX_LOG_RANGE_EMERG   SVX_LOG_RANGE_BASE(EMERG)
/*! Check range by ALERT priority level. */
#define SVX_LOG_RANGE_ALERT   SVX_LOG_RANGE_BASE(ALERT)
/*! Check range by CRIT priority level. */
#define SVX_LOG_RANGE_CRIT    SVX_LOG_RANGE_BASE(CRIT)
/*! Check range by ERR priority level. */
#define SVX_LOG_RANGE_ERR     SVX_LOG_RANGE_BASE(ERR)
/*! Check range by WARNING priority level. */
#define SVX_LOG_RANGE_WARNING SVX_LOG_RANGE_BASE(WARNING)
/*! Check range by NOTICE priority level. */
#define SVX_LOG_RANGE_NOTICE  SVX_LOG_RANGE_BASE(NOTICE)
/*! Check range by INFO priority level. */
#define SVX_LOG_RANGE_INFO    SVX_LOG_RANGE_BASE(INFO)
/*! Check range by DEBUG priority level. */
#define SVX_LOG_RANGE_DEBUG   SVX_LOG_RANGE_BASE(DEBUG)

/*! Append new log with EMERG priority level. (without error number) */
#define SVX_LOG_EMERG(format, ...)   SVX_LOG_ERRNO_BASE(EMERG,   -1, (format), ##__VA_ARGS__)
/*! Append new log with ALERT priority level. (without error number) */
#define SVX_LOG_ALERT(format, ...)   SVX_LOG_ERRNO_BASE(ALERT,   -1, (format), ##__VA_ARGS__)
/*! Append new log with CRIT priority level. (without error number) */
#define SVX_LOG_CRIT(format, ...)    SVX_LOG_ERRNO_BASE(CRIT,    -1, (format), ##__VA_ARGS__)
/*! Append new log with ERR priority level. (without error number) */
#define SVX_LOG_ERR(format, ...)     SVX_LOG_ERRNO_BASE(ERR,     -1, (format), ##__VA_ARGS__)
/*! Append new log with WARNING priority level. (without error number) */
#define SVX_LOG_WARNING(format, ...) SVX_LOG_ERRNO_BASE(WARNING, -1, (format), ##__VA_ARGS__)
/*! Append new log with NOTICE priority level. (without error number) */
#define SVX_LOG_NOTICE(format, ...)  SVX_LOG_ERRNO_BASE(NOTICE,  -1, (format), ##__VA_ARGS__)
/*! Append new log with INFO priority level. (without error number) */
#define SVX_LOG_INFO(format, ...)    SVX_LOG_ERRNO_BASE(INFO,    -1, (format), ##__VA_ARGS__)
/*! Append new log with DEBUG priority level. (without error number) */
#define SVX_LOG_DEBUG(format, ...)   SVX_LOG_ERRNO_BASE(DEBUG,   -1, (format), ##__VA_ARGS__)

/*! Append new log with EMERG priority level. */
#define SVX_LOG_ERRNO_EMERG(errnum, format, ...)   SVX_LOG_ERRNO_BASE(EMERG,   (errnum), (format), ##__VA_ARGS__)
/*! Append new log with ALERT priority level. */
#define SVX_LOG_ERRNO_ALERT(errnum, format, ...)   SVX_LOG_ERRNO_BASE(ALERT,   (errnum), (format), ##__VA_ARGS__)
/*! Append new log with CRIT priority level. */
#define SVX_LOG_ERRNO_CRIT(errnum, format, ...)    SVX_LOG_ERRNO_BASE(CRIT,    (errnum), (format), ##__VA_ARGS__)
/*! Append new log with ERR priority level. */
#define SVX_LOG_ERRNO_ERR(errnum, format, ...)     SVX_LOG_ERRNO_BASE(ERR,     (errnum), (format), ##__VA_ARGS__)
/*! Append new log with WARNING priority level. */
#define SVX_LOG_ERRNO_WARNING(errnum, format, ...) SVX_LOG_ERRNO_BASE(WARNING, (errnum), (format), ##__VA_ARGS__)
/*! Append new log with NOTICE priority level. */
#define SVX_LOG_ERRNO_NOTICE(errnum, format, ...)  SVX_LOG_ERRNO_BASE(NOTICE,  (errnum), (format), ##__VA_ARGS__)
/*! Append new log with INFO priority level. */
#define SVX_LOG_ERRNO_INFO(errnum, format, ...)    SVX_LOG_ERRNO_BASE(INFO,    (errnum), (format), ##__VA_ARGS__)
/*! Append new log with DEBUG priority level. */
#define SVX_LOG_ERRNO_DEBUG(errnum, format, ...)   SVX_LOG_ERRNO_BASE(DEBUG,   (errnum), (format), ##__VA_ARGS__)

/*! Append new log with EMERG priority level then RETURN. */
#define SVX_LOG_ERRNO_RETURN_EMERG(errnum, format, ...)   SVX_LOG_ERRNO_RETURN_BASE(EMERG,   (errnum), (format), ##__VA_ARGS__)
/*! Append new log with ALERT priority level then RETURN. */
#define SVX_LOG_ERRNO_RETURN_ALERT(errnum, format, ...)   SVX_LOG_ERRNO_RETURN_BASE(ALERT,   (errnum), (format), ##__VA_ARGS__)
/*! Append new log with CRIT priority level then RETURN. */
#define SVX_LOG_ERRNO_RETURN_CRIT(errnum, format, ...)    SVX_LOG_ERRNO_RETURN_BASE(CRIT,    (errnum), (format), ##__VA_ARGS__)
/*! Append new log with ERR priority level then RETURN. */
#define SVX_LOG_ERRNO_RETURN_ERR(errnum, format, ...)     SVX_LOG_ERRNO_RETURN_BASE(ERR,     (errnum), (format), ##__VA_ARGS__)
/*! Append new log with WARNING priority level then RETURN. */
#define SVX_LOG_ERRNO_RETURN_WARNING(errnum, format, ...) SVX_LOG_ERRNO_RETURN_BASE(WARNING, (errnum), (format), ##__VA_ARGS__)
/*! Append new log with NOTICE priority level then RETURN. */
#define SVX_LOG_ERRNO_RETURN_NOTICE(errnum, format, ...)  SVX_LOG_ERRNO_RETURN_BASE(NOTICE,  (errnum), (format), ##__VA_ARGS__)
/*! Append new log with INFO priority level then RETURN. */
#define SVX_LOG_ERRNO_RETURN_INFO(errnum, format, ...)    SVX_LOG_ERRNO_RETURN_BASE(INFO,    (errnum), (format), ##__VA_ARGS__)
/*! Append new log with DEBUG priority level then RETURN. */
#define SVX_LOG_ERRNO_RETURN_DEBUG(errnum, format, ...)   SVX_LOG_ERRNO_RETURN_BASE(DEBUG,   (errnum), (format), ##__VA_ARGS__)

/*! Append new log with EMERG priority level then GOTO. */
#define SVX_LOG_ERRNO_GOTO_EMERG(tag, errnum, format, ...)   SVX_LOG_ERRNO_GOTO_BASE(tag, EMERG,   (errnum), (format), ##__VA_ARGS__)
/*! Append new log with ALERT priority level then GOTO. */
#define SVX_LOG_ERRNO_GOTO_ALERT(tag, errnum, format, ...)   SVX_LOG_ERRNO_GOTO_BASE(tag, ALERT,   (errnum), (format), ##__VA_ARGS__)
/*! Append new log with CRIT priority level then GOTO. */
#define SVX_LOG_ERRNO_GOTO_CRIT(tag, errnum, format, ...)    SVX_LOG_ERRNO_GOTO_BASE(tag, CRIT,    (errnum), (format), ##__VA_ARGS__)
/*! Append new log with ERR priority level then GOTO. */
#define SVX_LOG_ERRNO_GOTO_ERR(tag, errnum, format, ...)     SVX_LOG_ERRNO_GOTO_BASE(tag, ERR,     (errnum), (format), ##__VA_ARGS__)
/*! Append new log with WARNING priority level then GOTO. */
#define SVX_LOG_ERRNO_GOTO_WARNING(tag, errnum, format, ...) SVX_LOG_ERRNO_GOTO_BASE(tag, WARNING, (errnum), (format), ##__VA_ARGS__)
/*! Append new log with NOTICE priority level then GOTO. */
#define SVX_LOG_ERRNO_GOTO_NOTICE(tag, errnum, format, ...)  SVX_LOG_ERRNO_GOTO_BASE(tag, NOTICE,  (errnum), (format), ##__VA_ARGS__)
/*! Append new log with INFO priority level then GOTO. */
#define SVX_LOG_ERRNO_GOTO_INFO(tag, errnum, format, ...)    SVX_LOG_ERRNO_GOTO_BASE(tag, INFO,    (errnum), (format), ##__VA_ARGS__)
/*! Append new log with DEBUG priority level then GOTO. */
#define SVX_LOG_ERRNO_GOTO_DEBUG(tag, errnum, format, ...)   SVX_LOG_ERRNO_GOTO_BASE(tag, DEBUG,   (errnum), (format), ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

/* \} */

#endif
