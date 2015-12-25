/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#define _GNU_SOURCE
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <dirent.h>
#include <fnmatch.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/stat.h>
#include "svx_log.h"
#include "svx_errno.h"
#include "svx_queue.h"
#include "svx_util.h"
#include "svx_notifier.h"

//#define SVX_LOG_SELF_DEBUG_FLAG
#if defined(SVX_LOG_SELF_DEBUG_FLAG)
#define SVX_LOG_SELF_DEBUG_PRINT(fmt, ...) printf("%s.%d: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define SVX_LOG_SELF_DEBUG_PRINT(fmt, ...)
#endif

#define SVX_LOG_FILE_DISCARDED_MSG_FMT        "! %ju msgs discarded here\n"
#define SVX_LOG_DEFAULT_FILE_DIRNAME          "./"
#define SVX_LOG_DEFAULT_FILE_SUFFIX           "log"
#define SVX_LOG_DEFAULT_FILE_SIZE_MAX_EACH    (10 * 1024 * 1024)
#define SVX_LOG_DEFAULT_FILE_SIZE_MAX_TOTAL   (1 * 1024 * 1024 * 1024)
#define SVX_LOG_DEFAULT_FILE_CACHE_SIZE_EACH  (1 * 1024 * 1024)
#define SVX_LOG_DEFAULT_FILE_CACHE_COUNT_MAX  64
#define SVX_LOG_DEFAULT_FILE_FLUSH_INTERVAL_S 3
#define SVX_LOG_STDOUT_STYLE_TAILER           "\033[0m\n"

typedef struct
{
    char    abbr;
    int     syslog_level;
    char   *stdout_style_header;
} svx_log_level_info_t;
static svx_log_level_info_t svx_log_level_info[] = {
    {'M', LOG_EMERG,   "\033[7;34m"},
    {'A', LOG_ALERT,   "\033[1;34m"},
    {'C', LOG_CRIT,    "\033[1;35m"},
    {'E', LOG_ERR,     "\033[1;31m"},
    {'W', LOG_WARNING, "\033[1;33m"},
    {'N', LOG_NOTICE,  "\033[1;36m"},
    {'I', LOG_INFO,    "\033[1;32m"},
    {'D', LOG_DEBUG,   "\033[0m"}
};

typedef struct svx_log_file_cache
{
    char           *buf;
    size_t          buf_used;
    struct timeval  first_msg_tv;
    TAILQ_ENTRY(svx_log_file_cache,) link;
} svx_log_file_cache_t;
typedef TAILQ_HEAD(svx_log_file_cache_queue, svx_log_file_cache,) svx_log_file_cache_queue_t;

svx_log_level_t                    svx_log_level_file                   = SVX_LOG_LEVEL_NONE;
svx_log_level_t                    svx_log_level_syslog                 = SVX_LOG_LEVEL_NONE;
svx_log_level_t                    svx_log_level_stdout                 = SVX_LOG_LEVEL_DEBUG;
static pthread_mutex_t             svx_log_mutex                        = PTHREAD_MUTEX_INITIALIZER;
static svx_notifier_t             *svx_log_notifier_write               = NULL;
static int                         svx_log_notifier_fd_write            = -1;
static svx_notifier_t             *svx_log_notifier_flush_response      = NULL;
static int                         svx_log_notifier_fd_flush_response   = -1;
static svx_notifier_t             *svx_log_notifier_flush_request       = NULL;
static int                         svx_log_notifier_fd_flush_request    = -1;
static svx_log_timezone_mode_t     svx_log_timezone_mode                = SVX_LOG_TIMEZONE_MODE_GMT;
static char                        svx_log_timezone[6]                  = "+0000";
static svx_log_errno_mode_t        svx_log_errno_mode                   = SVX_LOG_ERRNO_MODE_NUM_STR;
static svx_log_errno_to_str_t      svx_log_errno_to_str                 = svx_errno_to_str;
static pthread_t                   svx_log_file_thread;
static int                         svx_log_file_thread_running          = 0;
static char                        svx_log_file_hostname[HOST_NAME_MAX] = "unknownhost";
static char                        svx_log_file_dirname[PATH_MAX]       = "";
static char                        svx_log_file_prefix[NAME_MAX]        = "";
static char                        svx_log_file_suffix[NAME_MAX]        = "";
static char                        svx_log_file_pattern[NAME_MAX]       = "";
static uintmax_t                   svx_log_file_discarded_msg_count     = 0;
static size_t                      svx_log_file_discarded_msg_len_max   = 64;
static off_t                       svx_log_file_size_max_each           = SVX_LOG_DEFAULT_FILE_SIZE_MAX_EACH;
static off_t                       svx_log_file_size_cur_each           = 0;
static off_t                       svx_log_file_size_max_total          = SVX_LOG_DEFAULT_FILE_SIZE_MAX_TOTAL;
static off_t                       svx_log_file_size_cur_total          = -1;
static int                         svx_log_file_fd                      = -1;
static svx_log_file_cache_t       *svx_log_file_cache_cur               = NULL;
static svx_log_file_cache_queue_t  svx_log_file_cache_queue_empty       = TAILQ_HEAD_INITIALIZER(svx_log_file_cache_queue_empty);
static svx_log_file_cache_queue_t  svx_log_file_cache_queue_full        = TAILQ_HEAD_INITIALIZER(svx_log_file_cache_queue_full);
static size_t                      svx_log_file_cache_size_each         = SVX_LOG_DEFAULT_FILE_CACHE_SIZE_EACH;
static size_t                      svx_log_file_cache_count_max         = SVX_LOG_DEFAULT_FILE_CACHE_COUNT_MAX;
static size_t                      svx_log_file_cache_count_cur         = 0;
static int                         svx_log_file_cache_flush_interval_s  = SVX_LOG_DEFAULT_FILE_FLUSH_INTERVAL_S;

static void svx_log_file_write_to_cache(const char *msg, size_t len, struct timeval tv)
{
    /* the "current file cache" does not have enough space, move it to the full file cache queue */
    if(svx_log_file_cache_cur && 
       svx_log_file_cache_cur->buf_used > 0 &&
       svx_log_file_cache_cur->buf_used + svx_log_file_discarded_msg_len_max + len > svx_log_file_cache_size_each)
    {
        SVX_LOG_SELF_DEBUG_PRINT("cur cache -> full queue\n");
        TAILQ_INSERT_TAIL(&svx_log_file_cache_queue_full, svx_log_file_cache_cur, link);
        svx_log_file_cache_cur = NULL;
        svx_notifier_send(svx_log_notifier_write);
    }
    
    /* the "current file cache" is NULL, we must create or move an empty file cache to here */
    if(NULL == svx_log_file_cache_cur)
    {
        /* move an empty file cache to the "current file cache" */
        if(!TAILQ_EMPTY(&svx_log_file_cache_queue_empty))
        {
            SVX_LOG_SELF_DEBUG_PRINT("empty queue -> cur cache\n");
            svx_log_file_cache_cur = TAILQ_FIRST(&svx_log_file_cache_queue_empty);
            TAILQ_REMOVE(&svx_log_file_cache_queue_empty, svx_log_file_cache_cur, link);
            svx_log_file_cache_cur->buf_used = 0;
        }
        /* create(malloc) an empty file cache for the "current file cache" */
        else
        {
            /* check the file cache count limit */
            SVX_LOG_SELF_DEBUG_PRINT("malloc() -> cur cache\n");
            if(svx_log_file_cache_count_cur < svx_log_file_cache_count_max)
            {
                if(NULL != (svx_log_file_cache_cur = malloc(sizeof(svx_log_file_cache_t))))
                {
                    if(NULL != (svx_log_file_cache_cur->buf = malloc(svx_log_file_cache_size_each)))
                    {
                        /* create successfully */
                        svx_log_file_cache_cur->buf_used = 0;
                        timerclear(&(svx_log_file_cache_cur->first_msg_tv));
                        svx_log_file_cache_count_cur++;
                    }
                    else
                    {
                        free(svx_log_file_cache_cur);
                        svx_log_file_cache_cur = NULL;
                    }
                }
            }
        }
    }

    /* we have a "current file cache", and it have enough space, we can use it */
    if(svx_log_file_cache_cur && 
       svx_log_file_cache_cur->buf_used + svx_log_file_discarded_msg_len_max + len <= svx_log_file_cache_size_each)
    {
        /* append a extra message, to record the discarded count of message(s) */
        if(svx_log_file_discarded_msg_count > 0)
        {
            SVX_LOG_SELF_DEBUG_PRINT("discarded message(%ju) -> cur cache\n", svx_log_file_discarded_msg_count);
            svx_log_file_cache_cur->buf_used += 
                snprintf(svx_log_file_cache_cur->buf + svx_log_file_cache_cur->buf_used,
                         svx_log_file_cache_size_each - svx_log_file_cache_cur->buf_used,
                         SVX_LOG_FILE_DISCARDED_MSG_FMT, svx_log_file_discarded_msg_count);
            svx_log_file_discarded_msg_count = 0;
        }

        /* append the log message */
        SVX_LOG_SELF_DEBUG_PRINT("log message -> cur cache\n");
        memcpy(svx_log_file_cache_cur->buf + svx_log_file_cache_cur->buf_used, msg, len);
        svx_log_file_cache_cur->buf_used += len;

        /* save the first message's time */
        if(!timerisset(&(svx_log_file_cache_cur->first_msg_tv)))
            svx_log_file_cache_cur->first_msg_tv = tv;
    }
    /* we had to discard this message, increase the number of discarded message(s) */
    else
    {
        SVX_LOG_SELF_DEBUG_PRINT("dropped message count ++\n");
        svx_log_file_discarded_msg_count++;
    }
}

static int svx_log_file_filter(const struct dirent *entry)
{
    int r;

    switch(r = fnmatch(svx_log_file_pattern, entry->d_name, 0))
    {
    case 0           : return 1;
    case FNM_NOMATCH : return 0;
    default          : SVX_LOG_SELF_DEBUG_PRINT("fnmatch() failed. return:%d\n", r); return 0;
    }
}

static void svx_log_file_write_to_file(const char *buf, size_t buf_len, struct timeval tv)
{
    struct dirent **entry;
    struct stat     st;
    struct tm       tm;
    char            pathname[PATH_MAX];
    int             remove_older_files = 0;
    int             n = 0;
    off_t           cur_total = 0;
    ssize_t         len;

    /* remove the older files, if the total file size will be exceed the max total file size limit */
    if(svx_log_file_size_cur_total < 0 || svx_log_file_size_cur_total + buf_len > svx_log_file_size_max_total)
    {
        if((n = scandir(svx_log_file_dirname, &entry, svx_log_file_filter, alphasort)) >= 0)
        {
            while(n--)
            {
                snprintf(pathname, sizeof(pathname), "%s%s", svx_log_file_dirname, entry[n]->d_name);
                free(entry[n]);
                if(0 != lstat(pathname, &st)) continue;
                if(!S_ISREG(st.st_mode)) continue;

                if(!remove_older_files)
                {
                    if(cur_total + st.st_size + buf_len <= svx_log_file_size_max_total)
                    {
                        cur_total += st.st_size;
                        continue;
                    }
                    else
                        remove_older_files = 1;
                }

                if(remove_older_files)
                {
                    SVX_LOG_SELF_DEBUG_PRINT("remove: %s\n", pathname);
                    remove(pathname);
                }
            }
            free(entry);
            svx_log_file_size_cur_total = cur_total;
        }
    }

    /* close the current FD, if the current file size will be exceed the max each file size limit */
    if(svx_log_file_fd >= 0)
    {
        if(svx_log_file_size_cur_each + buf_len > svx_log_file_size_max_each)
        {
            SVX_LOG_SELF_DEBUG_PRINT("close\n");
            close(svx_log_file_fd);
            svx_log_file_fd = -1;
            svx_log_file_size_cur_each = 0;
        }
    }

    /* create a new file */
    if(svx_log_file_fd < 0)
    {
        if(SVX_LOG_TIMEZONE_MODE_GMT == svx_log_timezone_mode)
            gmtime_r((time_t*)(&(tv.tv_sec)), &tm);
        else
            localtime_r((time_t*)(&(tv.tv_sec)), &tm);

        snprintf(pathname, sizeof(pathname), "%s%s.%04d%02d%02d.%02d%02d%02d.%06ld.%s.%s.%d.%s", 
                 svx_log_file_dirname, svx_log_file_prefix, tm.tm_year + 1900, tm.tm_mon + 1, 
                 tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec, svx_log_timezone,
                 svx_log_file_hostname, getpid(), svx_log_file_suffix);

        SVX_LOG_SELF_DEBUG_PRINT("open: %s\n", pathname);
        svx_log_file_fd = open(pathname, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    }

    /* write to file */
    if(svx_log_file_fd >= 0)
    {
        if((len = write(svx_log_file_fd, buf, buf_len)) > 0)
        {
            svx_log_file_size_cur_each += len;
            svx_log_file_size_cur_total += len;
        }
        SVX_LOG_SELF_DEBUG_PRINT("write to file: %zu, write return: %zd\n", buf_len, len);
    }
}

static void *svx_log_file_thread_func(void *arg)
{
    svx_log_file_cache_t       *cache = NULL, *tmp = NULL;
    svx_log_file_cache_queue_t  queue_swap = TAILQ_HEAD_INITIALIZER(queue_swap);
    struct pollfd               events[2];
    int                         n;
    int                         flushing = 0;

    events[0].fd      = svx_log_notifier_fd_write;
    events[0].events  = POLLIN;
    events[0].revents = 0;
    events[1].fd      = svx_log_notifier_fd_flush_request;
    events[1].events  = POLLIN;
    events[1].revents = 0;

    while(svx_log_file_thread_running)
    {
        /* wait for a notice (write-to-file, flush or timeout) */
        n = poll(events, 2, svx_log_file_cache_flush_interval_s * 1000);
        if(n < 0)
        {
            if(EINTR != errno) usleep(200 * 1000);
            continue;
        }
        else if(n > 0)
        {
            if(events[0].revents & POLLIN)
                svx_notifier_recv(svx_log_notifier_write);
            if(events[1].revents & POLLIN)
            {
                svx_notifier_recv(svx_log_notifier_flush_request);
                flushing = 1; /* this is a flush from flush() call */
            }
        }

        /* check if there are cached messages need to be written, and swap them to the swap-queue */
        pthread_mutex_lock(&svx_log_mutex);
        if(NULL != svx_log_file_cache_cur && svx_log_file_cache_cur->buf_used > 0)
        {
            SVX_LOG_SELF_DEBUG_PRINT("cur cache(%zu bytes) -> full queue\n", svx_log_file_cache_cur->buf_used);
            TAILQ_INSERT_TAIL(&svx_log_file_cache_queue_full, svx_log_file_cache_cur, link);
            svx_log_file_cache_cur = NULL;
        }
        if(!TAILQ_EMPTY(&svx_log_file_cache_queue_full))
        {
            SVX_LOG_SELF_DEBUG_PRINT("full queue -> swap queue\n");
            TAILQ_SWAP(&queue_swap, &svx_log_file_cache_queue_full, svx_log_file_cache, link);
        }
        pthread_mutex_unlock(&svx_log_mutex);

        /* write cached messages to file, and give back the cache blocks to the empty_queue */
        if(!TAILQ_EMPTY(&queue_swap))
        {
            TAILQ_FOREACH_SAFE(cache, &queue_swap, link, tmp)
            {
                SVX_LOG_SELF_DEBUG_PRINT("swap queue cache(%zu bytes) -> disk file\n", cache->buf_used);
                svx_log_file_write_to_file(cache->buf, cache->buf_used, cache->first_msg_tv);
                cache->buf_used = 0;
                timerclear(&(cache->first_msg_tv));

                SVX_LOG_SELF_DEBUG_PRINT("swap queue cache -> empty queue\n");
                TAILQ_REMOVE(&queue_swap, cache, link);
                pthread_mutex_lock(&svx_log_mutex);
                TAILQ_INSERT_TAIL(&svx_log_file_cache_queue_empty, cache, link);
                pthread_mutex_unlock(&svx_log_mutex);
            }
        }

        /* give a notice back to flush() */
        if(flushing)
        {
            flushing = 0;
            svx_notifier_send(svx_log_notifier_flush_response);
        }
    }

    return NULL;
}

int svx_log_file_init()
{
    int  r = 0;
    char s[1024];

    pthread_mutex_lock(&svx_log_mutex);

    /* save the max length of discarded message */
    svx_log_file_discarded_msg_len_max = snprintf(s, sizeof(s), SVX_LOG_FILE_DISCARDED_MSG_FMT, UINTMAX_MAX);

    /* save hostname */
    if(0 != gethostname(svx_log_file_hostname, sizeof(svx_log_file_hostname)))
        SVX_LOG_ERRNO_GOTO_ERR(end, r = errno, NULL);

    /* save file absolute dirname*/
    if(0 != (r = svx_util_get_absolute_path(SVX_LOG_DEFAULT_FILE_DIRNAME, svx_log_file_dirname, 
                                            sizeof(svx_log_file_dirname))))
        SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);

    /* save file prefix */
    if(0 != (r = svx_util_get_exe_basename(svx_log_file_prefix, sizeof(svx_log_file_prefix), NULL)))
        SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);

    /* save file suffix */
    strncpy(svx_log_file_suffix, SVX_LOG_DEFAULT_FILE_SUFFIX, sizeof(svx_log_file_suffix));
    svx_log_file_suffix[sizeof(svx_log_file_suffix) - 1] = '\0';

    /* save file pattern */
    snprintf(svx_log_file_pattern, sizeof(svx_log_file_pattern), "%s.*.%s", 
             svx_log_file_prefix, svx_log_file_suffix);
    
 end:
    pthread_mutex_unlock(&svx_log_mutex);
    return r;
}

int svx_log_file_uninit()
{
    svx_log_file_cache_t *cache = NULL, *tmp = NULL;
    char                  discarded_msg[1024];
    size_t                discarded_msg_len = 0;
    struct timeval        tv;

    /* join the thread */
    if(svx_log_file_thread_running)
    {
        svx_log_file_flush(-1);
        svx_log_file_thread_running = 0;
        svx_notifier_send(svx_log_notifier_write);
        pthread_join(svx_log_file_thread, NULL);
    }

    pthread_mutex_lock(&svx_log_mutex);

    /* append a extra message, to record the discarded count of message(s) */
    if(svx_log_file_discarded_msg_count > 0)
    {
        SVX_LOG_SELF_DEBUG_PRINT("discarded message(%ju) -> disk file\n", svx_log_file_discarded_msg_count);
        gettimeofday(&tv, NULL);
        discarded_msg_len = snprintf(discarded_msg, sizeof(discarded_msg),
                                     SVX_LOG_FILE_DISCARDED_MSG_FMT, svx_log_file_discarded_msg_count);
        svx_log_file_write_to_file(discarded_msg, discarded_msg_len, tv);
        svx_log_file_discarded_msg_count = 0;
    }
        
    /* free all file caches */
    if(NULL != svx_log_file_cache_cur)
    {
        free(svx_log_file_cache_cur->buf);
        free(svx_log_file_cache_cur);
        svx_log_file_cache_cur = NULL;
    }
    TAILQ_FOREACH_SAFE(cache, &svx_log_file_cache_queue_empty, link, tmp)
    {
        TAILQ_REMOVE(&svx_log_file_cache_queue_empty, cache, link);
        free(cache->buf);
        free(cache);
    }
    TAILQ_FOREACH_SAFE(cache, &svx_log_file_cache_queue_full, link, tmp)
    {
        TAILQ_REMOVE(&svx_log_file_cache_queue_full, cache, link);
        free(cache->buf);
        free(cache);
    }

    /* close the file FD */
    if(svx_log_file_fd >= 0)
    {
        close(svx_log_file_fd);
        svx_log_file_fd = -1;
    }

    /* destroy notifiers */
    if(svx_log_notifier_write) svx_notifier_destroy(&svx_log_notifier_write);
    if(svx_log_notifier_flush_request) svx_notifier_destroy(&svx_log_notifier_flush_request);
    if(svx_log_notifier_flush_response) svx_notifier_destroy(&svx_log_notifier_flush_response);
    svx_log_notifier_fd_write = -1;
    svx_log_notifier_fd_flush_request = -1;
    svx_log_notifier_fd_flush_response = -1;

    svx_log_file_size_cur_each   = 0;
    svx_log_file_size_cur_total  = -1;
    svx_log_file_cache_count_cur = 0;

    pthread_mutex_unlock(&svx_log_mutex);

    pthread_mutex_init(&svx_log_mutex, NULL);
    
    return 0;
}

/* this is an async-signal-safe function */
int svx_log_file_flush(int timeout_ms)
{
    struct pollfd event;
    int           n = 0;

    if(!svx_log_file_thread_running) return 0;

    /* send a request for flush */
    svx_notifier_send(svx_log_notifier_flush_request);
    
    /* wait for flush completed  */
    event.fd      = svx_log_notifier_fd_flush_response;
    event.events  = POLLIN;
    event.revents = 0;
    do
        n = poll(&event, 1, timeout_ms);
    while(-1 == n && EINTR == errno);

    /* check result */
    if(n < 0) /* error */
    {
        if(0 != errno)
            return errno;
        else
            return SVX_ERRNO_UNKNOWN;
    }
    else if(0 == n)
    {
        if(0 == timeout_ms)
            return 0; /* OK */
        else
            return SVX_ERRNO_TIMEDOUT; /* timedout */
    }
    else
    {
        if(event.revents & POLLIN)
        {
            svx_notifier_recv(svx_log_notifier_flush_response);
            return 0; /* OK */
        }
        else
            return SVX_ERRNO_UNKNOWN;
    }
}

int svx_log_file_set_dirname(const char *dirname)
{
    int r = 0;

    if(NULL == dirname || '\0' == dirname[0] || '/' != dirname[strlen(dirname) - 1])
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "dirname:%s\n", dirname);
    
    pthread_mutex_lock(&svx_log_mutex);

    if(0 != (r = svx_util_get_absolute_path(dirname, svx_log_file_dirname, sizeof(svx_log_file_dirname))))
        SVX_LOG_ERRNO_ERR(r, NULL);
    
    pthread_mutex_unlock(&svx_log_mutex);

    return r;
}

int svx_log_file_set_prefix(const char *prefix)
{
    if(NULL == prefix || '\0' == prefix[0] || strchr(prefix, '/'))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "prefix:%s\n", prefix);
    
    pthread_mutex_lock(&svx_log_mutex);

    strncpy(svx_log_file_prefix, prefix, sizeof(svx_log_file_prefix));
    svx_log_file_prefix[sizeof(svx_log_file_prefix) - 1] = '\0';

    /* update file pattern */
    snprintf(svx_log_file_pattern, sizeof(svx_log_file_pattern), "%s.*.%s", 
             svx_log_file_prefix, svx_log_file_suffix);

    pthread_mutex_unlock(&svx_log_mutex);

    return 0;
}

int svx_log_file_set_suffix(const char *suffix)
{
    if(NULL == suffix || '\0' == suffix[0] || strchr(suffix, '/'))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "suffix:%s\n", suffix);
    
    pthread_mutex_lock(&svx_log_mutex);

    strncpy(svx_log_file_suffix, suffix, sizeof(svx_log_file_suffix));
    svx_log_file_suffix[sizeof(svx_log_file_suffix) - 1] = '\0';

    /* update file pattern */
    snprintf(svx_log_file_pattern, sizeof(svx_log_file_pattern), "%s.*.%s", 
             svx_log_file_prefix, svx_log_file_suffix);

    pthread_mutex_unlock(&svx_log_mutex);

    return 0;    
}

int svx_log_file_set_size_max(off_t each, off_t total)
{
    if(each <= 0 || total <= 0 || each > total)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "each:%jd, total:%jd\n", (intmax_t)each, (intmax_t)total);
    
    pthread_mutex_lock(&svx_log_mutex);

    svx_log_file_size_max_each = each;
    svx_log_file_size_max_total = total;

    pthread_mutex_unlock(&svx_log_mutex);

    return 0;
}

int svx_log_file_set_cache_size(size_t each, size_t total)
{
    if(0 == each || 0 == total || each > total)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "each:%zu, total:%zu\n", each, total);

    pthread_mutex_lock(&svx_log_mutex);

    svx_log_file_cache_size_each = each;
    svx_log_file_cache_count_max = total / each;

    pthread_mutex_unlock(&svx_log_mutex);

    return 0;
}

int svx_log_file_set_cache_flush_interval(int seconds)
{
    if(seconds <= 0) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "seconds:%d\n", seconds);

    pthread_mutex_lock(&svx_log_mutex);

    svx_log_file_cache_flush_interval_s = seconds;

    pthread_mutex_unlock(&svx_log_mutex);

    return 0;
}

int svx_log_set_timezone_mode(svx_log_timezone_mode_t mode)
{
    struct timeval tv;
    struct tm      tm;

    pthread_mutex_lock(&svx_log_mutex);

    svx_log_timezone_mode = mode;

    if(SVX_LOG_TIMEZONE_MODE_GMT == mode)
    {
        strncpy(svx_log_timezone, "+0000", sizeof(svx_log_timezone));
        svx_log_timezone[sizeof(svx_log_timezone) - 1] = '\0';
    }
    else
    {
        if(0 != gettimeofday(&tv, NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
        if(NULL == localtime_r((time_t*)(&(tv.tv_sec)), &tm)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
        snprintf(svx_log_timezone, sizeof(svx_log_timezone), "%c%02d%02d",
#ifdef __USE_BSD
                 tm.tm_gmtoff < 0 ? '-' : '+', abs(tm.tm_gmtoff / 3600), abs(tm.tm_gmtoff % 3600)
#else
                 tm.__tm_gmtoff < 0 ? '-' : '+', abs(tm.__tm_gmtoff / 3600), abs(tm.__tm_gmtoff % 3600)
#endif
                 );        
    }

    pthread_mutex_unlock(&svx_log_mutex);
    
    return 0;
}

int svx_log_set_errno_mode(svx_log_errno_mode_t mode)
{
    pthread_mutex_lock(&svx_log_mutex);

    svx_log_errno_mode = mode;

    pthread_mutex_unlock(&svx_log_mutex);

    return 0;
}

int svx_log_set_errno_to_str(svx_log_errno_to_str_t errno_to_str)
{
    if(NULL == errno_to_str) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "errno_to_str:%p\n", errno_to_str);
    
    pthread_mutex_lock(&svx_log_mutex);

    svx_log_errno_to_str = errno_to_str;

    pthread_mutex_unlock(&svx_log_mutex);

    return 0;
}

int svx_log_file_to_async_mode()
{
    int r;
    
    if(1 == svx_log_file_thread_running) return 0;

    pthread_mutex_lock(&svx_log_mutex);
    
    /* create notifiers */
    if(0 != (r = svx_notifier_create(&svx_log_notifier_write, &svx_log_notifier_fd_write)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    if(0 != (r = svx_notifier_create(&svx_log_notifier_flush_request, &svx_log_notifier_fd_flush_request)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    if(0 != (r = svx_notifier_create(&svx_log_notifier_flush_response, &svx_log_notifier_fd_flush_response)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);

    /* create a thread for async write-to-file */
    if(0 != (r = pthread_create(&svx_log_file_thread, NULL, &svx_log_file_thread_func, NULL)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    
    svx_log_file_thread_running = 1;
    pthread_mutex_unlock(&svx_log_mutex);
    return 0;
    
 err:
    if(svx_log_notifier_write) svx_notifier_destroy(&svx_log_notifier_write);
    if(svx_log_notifier_flush_request) svx_notifier_destroy(&svx_log_notifier_flush_request);
    if(svx_log_notifier_flush_response) svx_notifier_destroy(&svx_log_notifier_flush_response);
    svx_log_notifier_fd_write = -1;
    svx_log_notifier_fd_flush_request = -1;
    svx_log_notifier_fd_flush_response = -1;
    
    pthread_mutex_unlock(&svx_log_mutex);
    return r;
}

int svx_log_file_is_async_mode()
{
    return 1 == svx_log_file_thread_running ? 1 : 0;
}

void svx_log_errno_msg(svx_log_level_t level, const char *file, int line, const char *function,
                       int errnum, const char *format, ...)
{
    va_list        ap;
    struct timeval tv;
    struct tm      tm;
    char           buf[4096];
    char           errstr[256] = "\0";
    size_t         len = 0;
    int            errno_saved = errno;

    pthread_mutex_lock(&svx_log_mutex); /* In order to guarantee the time sequence of messages */

    /* get current time */
    gettimeofday(&tv, NULL);
    if(SVX_LOG_TIMEZONE_MODE_GMT == svx_log_timezone_mode)
        gmtime_r((time_t*)(&(tv.tv_sec)), &tm);
    else
        localtime_r((time_t*)(&(tv.tv_sec)), &tm);

    /* build the log message */
    len = snprintf(buf, sizeof(buf), "%c %04d-%02d-%02d %02d:%02d:%02d.%06ld%s %d.%d %s:%d:%s",
                   svx_log_level_info[level].abbr, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                   tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec, svx_log_timezone,
                   getpid(), svx_util_get_tid(), file, line, function);
    if(errnum >=0 && SVX_LOG_ERRNO_MODE_NONE != svx_log_errno_mode)
    {
        if(SVX_LOG_ERRNO_MODE_NUM_STR == svx_log_errno_mode && NULL != svx_log_errno_to_str)
        {
            svx_log_errno_to_str(errnum, errstr, sizeof(errstr));
            len += snprintf(buf + len, sizeof(buf) - len, " - (%d:%s)", errnum, errstr);
        }
        else 
        {
            len += snprintf(buf + len, sizeof(buf) - len, " - (%d)", errnum);
        }
    }
    if(NULL == format || '\0' == format[0] || '\n' == format[0])
    {
        len += snprintf(buf + len, sizeof(buf) - len, "\n");
    }
    else
    {
        len += snprintf(buf + len, sizeof(buf) - len, " - ");
        va_start(ap, format);
        len += vsnprintf(buf + len, sizeof(buf) - len, format, ap);
        va_end(ap);
    }

    /* write to file */
    if(level <= svx_log_level_file)
    {
        if(svx_log_file_thread_running)
        {
            /* async write to file */
            SVX_LOG_SELF_DEBUG_PRINT("msg(%zu) -> cur cache\n", len);
            svx_log_file_write_to_cache(buf, len, tv);
        }
        else
        {
            /* sync write to file */
            SVX_LOG_SELF_DEBUG_PRINT("msg(%zu) -> file (sync)\n", len);
            svx_log_file_write_to_file(buf, len, tv);
        }
    }

    /* write to syslog */
    if(level <= svx_log_level_syslog)
        syslog(svx_log_level_info[level].syslog_level, "%s", buf);

    /* write to stdout */
    if(level <= svx_log_level_stdout)
    {
        if('\n' == buf[len - 1]) buf[len - 1] = '\0';
        flockfile(stdout);
        printf("%s", svx_log_level_info[level].stdout_style_header);
        printf("%s", buf);
        printf(SVX_LOG_STDOUT_STYLE_TAILER);
        fflush(stdout);
        funlockfile(stdout);
    }

    pthread_mutex_unlock(&svx_log_mutex);

    errno = errno_saved;
}
