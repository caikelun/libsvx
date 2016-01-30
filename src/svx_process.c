/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <errno.h>
#include <execinfo.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include "svx_process.h"
#include "svx_util.h"
#include "svx_looper.h"
#include "svx_notifier.h"
#include "svx_channel.h"
#include "svx_errno.h"
#include "svx_log.h"
#include "svx_crash.h"

#define SVX_PROCESS_DEFAULT_LOG_SIZE_MAX_EACH            (10 * 1024 * 1024)
#define SVX_PROCESS_DEFAULT_LOG_SIZE_MAX_TOTAL           (1 * 1024 * 1024 * 1024)
#define SVX_PROCESS_DEFAULT_LOG_SIZE_MAX_TOTAL_WATCHER   (100 * 1024 * 1024)
#define SVX_PROCESS_DEFAULT_LOG_SIZE_MAX_TOTAL_SIGNALLER (20 * 1024 * 1024)
#define SVX_PROCESS_DEFAULT_CRASH_MAX_DUMPS              1000
#define SVX_PROCESS_DEFAULT_CRASH_MAX_DUMPS_WATCHER      100
#define SVX_PROCESS_DEFAULT_CRASH_MAX_DUMPS_SIGNALLER    100
#define SVX_PROCESS_PREFIX_PID                           ".pid"

static void svx_process_signal_handler(int sig);

typedef enum
{
    SVX_PROCESS_ROLE_SIGNALLER = 0,
    SVX_PROCESS_ROLE_SINGLE,
    SVX_PROCESS_ROLE_WATCHER,
    SVX_PROCESS_ROLE_WORKER
} svx_process_role_t;

typedef struct
{
    svx_looper_t                  *looper;
    svx_notifier_t                *notifier;
    svx_channel_t                 *notifier_chn;
    int                            notifier_fd;
    svx_process_role_t             role;
    svx_log_level_t                log_level_file;
    svx_log_level_t                log_level_syslog;
    svx_log_level_t                log_level_stdout;
    char                          *log_dirname;
    off_t                          log_max_size_each;
    off_t                          log_max_size_total;
    off_t                          log_max_size_total_watcher;
    off_t                          log_max_size_total_signaller;
    char                          *crash_dirname;
    char                          *crash_head_msg;
    size_t                         crash_max_dumps;
    size_t                         crash_max_dumps_watcher;
    size_t                         crash_max_dumps_signaller;
    int                            watch_mode;
    int                            daemon_mode;
    char                           pid_file[PATH_MAX];
    int                            pid_fd;
    char                          *user;
    char                          *group;
    int                            maxfds;
    svx_process_callback_t         start_cb;
    void                          *start_cb_arg;
    svx_process_callback_reload_t  reload_cb;
    void                          *reload_cb_arg;
    svx_process_callback_t         stop_cb;
    void                          *stop_cb_arg;
} svx_process_t;

static svx_process_t svx_process_obj = {
    .looper                       = NULL,
    .notifier                     = NULL,
    .notifier_chn                 = NULL,
    .notifier_fd                  = -1,
    .role                         = SVX_PROCESS_ROLE_WATCHER,
    .log_level_file               = SVX_LOG_LEVEL_ERR,
    .log_level_syslog             = SVX_LOG_LEVEL_NONE,
    .log_level_stdout             = SVX_LOG_LEVEL_ERR,
    .log_dirname                  = NULL,
    .log_max_size_each            = SVX_PROCESS_DEFAULT_LOG_SIZE_MAX_EACH,
    .log_max_size_total           = SVX_PROCESS_DEFAULT_LOG_SIZE_MAX_TOTAL,
    .log_max_size_total_watcher   = SVX_PROCESS_DEFAULT_LOG_SIZE_MAX_TOTAL_WATCHER,
    .log_max_size_total_signaller = SVX_PROCESS_DEFAULT_LOG_SIZE_MAX_TOTAL_SIGNALLER,
    .crash_dirname                = NULL,
    .crash_head_msg               = NULL,
    .crash_max_dumps              = SVX_PROCESS_DEFAULT_CRASH_MAX_DUMPS,
    .crash_max_dumps_watcher      = SVX_PROCESS_DEFAULT_CRASH_MAX_DUMPS_WATCHER,
    .crash_max_dumps_signaller    = SVX_PROCESS_DEFAULT_CRASH_MAX_DUMPS_SIGNALLER,
    .watch_mode                   = 1,
    .daemon_mode                  = 1,
    .pid_file                     = "",
    .pid_fd                       = -1,
    .user                         = NULL,
    .group                        = NULL,
    .maxfds                       = 0,
    .start_cb                     = NULL,
    .start_cb_arg                 = NULL,
    .reload_cb                    = NULL,
    .reload_cb_arg                = NULL,
    .stop_cb                      = NULL,
    .stop_cb_arg                  = NULL
};

typedef struct
{
    int    signo;
    char  *signame;
    void (*handler)(int signo);
} svx_process_signal_info_t;

svx_process_signal_info_t svx_process_signal_infos[] =
{
    { SIGHUP,  "SIGHUP",  svx_process_signal_handler },
    { SIGTERM, "SIGTERM", svx_process_signal_handler },
    { SIGINT,  "SIGINT",  svx_process_signal_handler },
    { SIGCHLD, "SIGCHLD", svx_process_signal_handler },
    { SIGPIPE, "SIGPIPE", SIG_IGN                    },
    { 0,       NULL,      NULL                       }
};

static sig_atomic_t svx_process_watcher_recv_sig_reload = 0;
static sig_atomic_t svx_process_watcher_recv_sig_stop   = 0;
static sig_atomic_t svx_process_watcher_recv_sig_child  = 0;

static void svx_process_crash_cb(int fd, void *arg)
{
    SVX_UTIL_UNUSED(fd);
    SVX_UTIL_UNUSED(arg);
    
    svx_log_file_flush(1000);
}

static int svx_process_init_log(svx_process_role_t role, char *prefix, off_t log_max_size_total)
{
    int r;
    
    if(SVX_PROCESS_ROLE_SIGNALLER == role) return 0;
    
    /* reset for worker process */
    if(SVX_PROCESS_ROLE_WORKER == role)
        if(0 != (r = svx_log_file_uninit())) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
        
    if(0 != (r = svx_log_file_init())) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_log_file_set_size_max(svx_process_obj.log_max_size_each, log_max_size_total))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(svx_process_obj.log_dirname) if(0 != (r = svx_log_file_set_dirname(svx_process_obj.log_dirname))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_log_file_set_prefix(prefix))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    /* synchronous mode only for watcher process and signaller process,
       single mode will switch to async mode after svx_util_daemonize() */
    if(SVX_PROCESS_ROLE_WORKER == role)
        if(0 != (r = svx_log_file_to_async_mode())) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    svx_log_level_file   = svx_process_obj.log_level_file;
    svx_log_level_syslog = svx_process_obj.log_level_syslog;
    svx_log_level_stdout = svx_process_obj.daemon_mode ? SVX_LOG_LEVEL_NONE : svx_process_obj.log_level_stdout;

    return 0;
}

static int svx_process_init_crash(svx_process_role_t role, char *prefix, size_t crash_max_dumps)
{
    int r;

    /* need not re-register signal handler for worker process */
    if(SVX_PROCESS_ROLE_WORKER == role) return 0;
    
    if(0 != (r = svx_crash_set_callback(svx_process_crash_cb, NULL))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_crash_set_prefix(prefix))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_crash_set_max_dumps(crash_max_dumps))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(svx_process_obj.crash_head_msg) if(0 != (r = svx_crash_set_head_msg(svx_process_obj.crash_head_msg))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(svx_process_obj.crash_dirname) if(0 != (r = svx_log_file_set_dirname(svx_process_obj.crash_dirname))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_crash_register_signal_handler())) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    return 0;
}

static int svx_process_init()
{
    char   basename[NAME_MAX];
    char   prefix[NAME_MAX];
    off_t  log_max_size_total;
    size_t crash_max_dumps;
    int  r;

    if(0 != (r = svx_util_get_exe_basename(basename, sizeof(basename), NULL))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    switch(svx_process_obj.role)
    {
    case SVX_PROCESS_ROLE_SINGLE:
        snprintf(prefix, sizeof(prefix), "%s.single", basename);
        log_max_size_total = svx_process_obj.log_max_size_total;
        crash_max_dumps = svx_process_obj.crash_max_dumps;
        break;
    case SVX_PROCESS_ROLE_WATCHER:
        snprintf(prefix, sizeof(prefix), "%s.watcher", basename);
        log_max_size_total = svx_process_obj.log_max_size_total_watcher;
        crash_max_dumps = svx_process_obj.crash_max_dumps_watcher;
        break;
    case SVX_PROCESS_ROLE_WORKER:
        snprintf(prefix, sizeof(prefix), "%s.worker", basename);
        log_max_size_total = svx_process_obj.log_max_size_total;
        crash_max_dumps = svx_process_obj.crash_max_dumps;
        break;
    case SVX_PROCESS_ROLE_SIGNALLER:
        snprintf(prefix, sizeof(prefix), "%s.signaller", basename);
        log_max_size_total = svx_process_obj.log_max_size_total_signaller;
        crash_max_dumps = svx_process_obj.crash_max_dumps_signaller;
        break;
    default:
        return 0;
    }

    if(0 != (r = svx_process_init_log(svx_process_obj.role, prefix, log_max_size_total))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_process_init_crash(svx_process_obj.role, prefix, crash_max_dumps))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    return 0;
}

static int svx_process_uninit()
{
    int r;

    if(0 != (r = svx_log_file_uninit())) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r =  svx_crash_uregister_signal_handler())) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    return 0;
}

static void svx_process_signal_handler(int sig)
{
    switch(svx_process_obj.role)
    {
    case SVX_PROCESS_ROLE_SINGLE:
    case SVX_PROCESS_ROLE_WORKER:
        switch(sig)
        {
        case SIGHUP:
            svx_notifier_send(svx_process_obj.notifier);
            break;
        case SIGTERM:
        case SIGINT:
            svx_looper_quit(svx_process_obj.looper);
            break;
        case SIGCHLD:
        default:
            break;
        }
        break;
    case SVX_PROCESS_ROLE_WATCHER:
        switch(sig)
        {
        case SIGHUP:
            svx_process_watcher_recv_sig_reload = 1;
            break;
        case SIGTERM:
        case SIGINT:
            svx_process_watcher_recv_sig_stop = 1;
            break;
        case SIGCHLD:
            svx_process_watcher_recv_sig_child = 1;
            break;
        default:
            break;
        }
        break;
    case SVX_PROCESS_ROLE_SIGNALLER:
    default:
        break;
    }
}

static int svx_process_register_signal()
{
    svx_process_signal_info_t *sig_info;
    struct sigaction           sa;

    for(sig_info = svx_process_signal_infos; sig_info->signo != 0; sig_info++)
    {
        memset(&sa, 0, sizeof(struct sigaction));
        sa.sa_handler = sig_info->handler;
        sigemptyset(&sa.sa_mask);
        if(0 != sigaction(sig_info->signo, &sa, NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    }

    return 0;
}

/* called by single and worker process */
static void svx_process_notifier_read_callback(void *arg)
{
    int r;

    SVX_UTIL_UNUSED(arg);
    
    svx_notifier_recv(svx_process_obj.notifier);

    if(NULL != svx_process_obj.reload_cb)
        if(0 != (r = svx_process_obj.reload_cb(svx_process_obj.looper, 0, svx_process_obj.reload_cb_arg)))
            SVX_LOG_ERRNO_ERR(r, NULL);        
}

static int svx_process_start_single()
{
    int r = 0;

    /* create the main looper */
    if(0 != (r = svx_looper_create(&(svx_process_obj.looper)))) SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);

    /* create a notifier for looper to handle SIGHUP(reload) signal */
    if(0 != (r = svx_notifier_create(&(svx_process_obj.notifier), &(svx_process_obj.notifier_fd)))) SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);
    if(0 != (r = svx_channel_create(&(svx_process_obj.notifier_chn), svx_process_obj.looper, svx_process_obj.notifier_fd, SVX_CHANNEL_EVENT_READ))) SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);
    if(0 != (r = svx_channel_set_read_callback(svx_process_obj.notifier_chn, svx_process_notifier_read_callback, NULL))) SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);

    /* call this user's start callback function */
    if(svx_process_obj.start_cb)
        if(0 != (r = svx_process_obj.start_cb(svx_process_obj.looper, svx_process_obj.start_cb_arg)))
            SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);

    /* run the looper (blocked here until svx_looper_quit()) */
    if(0 != svx_looper_loop(svx_process_obj.looper)) SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);

    /* call this user's stop callback function */
    if(svx_process_obj.stop_cb)
    {
        if(0 != (r = svx_process_obj.stop_cb(svx_process_obj.looper, svx_process_obj.stop_cb_arg)))
            SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);
    }

 end:
    /* destroy the notifier, channel and looper */
    if(NULL != svx_process_obj.notifier_chn)
        if(0 != (r = svx_channel_destroy(&(svx_process_obj.notifier_chn)))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(NULL != svx_process_obj.notifier)
        if(0 != (r = svx_notifier_destroy(&(svx_process_obj.notifier)))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(NULL != svx_process_obj.looper)
        if(0 != (r = svx_looper_destroy(&(svx_process_obj.looper)))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    
    return r;
}

static int svx_process_start_worker(int *pid)
{
    sigset_t set;
    int      r;
    
    sigemptyset(&set);
    if(0 > sigprocmask(SIG_SETMASK, &set, NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

    switch(*pid = fork())
    {
    case -1:
        SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
        break;
    case 0:  /* child */
        svx_process_obj.role = SVX_PROCESS_ROLE_WORKER;
        if(0 != (r = svx_process_init()))
        {
            SVX_LOG_ERRNO_ERR(r, NULL);
            exit(1);
        }
        if(0 != (r = svx_process_start_single()))
        {
            SVX_LOG_ERRNO_ERR(r, NULL);
            exit(1);
        }
        if(svx_process_obj.pid_fd >= 0)
        {
            if(0 != close(svx_process_obj.pid_fd))
            {
                SVX_LOG_ERRNO_ERR(errno, NULL);
                exit(1);
            }
            svx_process_obj.pid_fd = -1;
        }
        if(0 != (r = svx_process_uninit()))
        {
            SVX_LOG_ERRNO_ERR(r, NULL);
            exit(1);
        }
        close(STDIN_FILENO); /* let valgrind happy */
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        exit(0);
    default: /* parent */
        break;
    }

    return 0;
}

static int svx_process_start_watcher()
{
    sigset_t set;
    pid_t    worker_pid, pid;
    int      status;
    int      r;
    
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGPIPE);
    if(0 > sigprocmask(SIG_BLOCK, &set, NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    sigemptyset(&set);

    if(0 != (r = svx_process_start_worker(&worker_pid))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    while(1)
    {
        sigsuspend(&set); /* parent process suspend here, waiting for a signal */

        if(svx_process_watcher_recv_sig_reload)
        {
            svx_process_watcher_recv_sig_reload = 0;
            if(0 != kill(worker_pid, SIGHUP)) SVX_LOG_ERRNO_ERR(errno, NULL);
            if(NULL != svx_process_obj.reload_cb)
                if(0 != (r = svx_process_obj.reload_cb(svx_process_obj.looper, 1, svx_process_obj.reload_cb_arg)))
                    SVX_LOG_ERRNO_ERR(r, NULL);
        }
        
        if(svx_process_watcher_recv_sig_stop)
        {
            if(0 != kill(worker_pid, SIGTERM)) SVX_LOG_ERRNO_ERR(errno, NULL);

            /* wait worker process exit */
            if(worker_pid != (pid = waitpid(worker_pid, &status, 0)))
                SVX_LOG_ERRNO_ERR(SVX_ERRNO_UNKNOWN, "worker_pid:%d, waitpid return:%d\n", worker_pid, pid);

            break;
        }
        
        if(svx_process_watcher_recv_sig_child)
        {
            svx_process_watcher_recv_sig_child = 0;

            if(worker_pid != (pid = waitpid(worker_pid, &status, WNOHANG)))
                SVX_LOG_ERRNO_ERR(SVX_ERRNO_UNKNOWN, "worker_pid:%d, waitpid return:%d\n", worker_pid, pid);

            if(WIFSIGNALED(status))
                SVX_LOG_ERRNO_CRIT(SVX_ERRNO_UNKNOWN, "worker process(PID:%d) exited on signal %d%s\n",
                                   worker_pid, WTERMSIG(status), WCOREDUMP(status) ? " (core dumped)" : "");
            else if(WIFEXITED(status))
                SVX_LOG_ERRNO_NOTICE(SVX_ERRNO_UNKNOWN, "worker process(PID:%d) exited with code %d\n",
                                     worker_pid, WEXITSTATUS(status));
            else
                SVX_LOG_ERRNO_ERR(SVX_ERRNO_UNKNOWN, "worker process(PID:%d) exited with status %d\n",
                                     worker_pid, status);
            
            usleep(100 * 1000);
            if(0 != (r = svx_process_start_worker(&worker_pid))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
        }
    }

    return 0;
}

int svx_process_set_log(svx_log_level_t level_file, svx_log_level_t level_syslog, svx_log_level_t level_stdout,
                        const char *dirname, off_t max_size_each, off_t max_size_total,
                        off_t max_size_total_watcher, off_t max_size_total_signaller)
{
    svx_process_obj.log_level_file               = level_file;
    svx_process_obj.log_level_syslog             = level_syslog;
    svx_process_obj.log_level_stdout             = level_stdout;
    svx_process_obj.log_max_size_each            = max_size_each;
    svx_process_obj.log_max_size_total           = max_size_total;
    svx_process_obj.log_max_size_total_watcher   = max_size_total_watcher;
    svx_process_obj.log_max_size_total_signaller = max_size_total_signaller;
    
    if(NULL != dirname)
    {
        if(svx_process_obj.log_dirname) free(svx_process_obj.log_dirname);
        if(NULL == (svx_process_obj.log_dirname = strdup(dirname))) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    }
    
    return 0;
}

int svx_process_set_crash(const char *dirname, const char *head_msg, size_t max_dumps,
                          size_t max_dumps_watcher, size_t max_dumps_signaller)
{
    svx_process_obj.crash_max_dumps           = max_dumps;
    svx_process_obj.crash_max_dumps_watcher   = max_dumps_watcher;
    svx_process_obj.crash_max_dumps_signaller = max_dumps_signaller;
    
    if(NULL != dirname)
    {
        if(svx_process_obj.crash_dirname) free(svx_process_obj.crash_dirname);
        if(NULL == (svx_process_obj.crash_dirname = strdup(dirname))) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    }
    if(NULL != head_msg)
    {
        if(svx_process_obj.crash_head_msg) free(svx_process_obj.crash_head_msg);
        if(NULL == (svx_process_obj.crash_head_msg = strdup(head_msg))) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    }
    
    return 0;
}

int svx_process_set_mode(int watch_mode, int daemon_mode)
{
    svx_process_obj.watch_mode  = watch_mode;
    svx_process_obj.daemon_mode = daemon_mode;

    return 0;
}

int svx_process_set_user_group(const char *user, const char *group)
{
    if(NULL != user)
    {
        if(svx_process_obj.user) free(svx_process_obj.user);
        if(NULL == (svx_process_obj.user = strdup(user))) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    }
    if(NULL != group)
    {
        if(svx_process_obj.group) free(svx_process_obj.group);
        if(NULL == (svx_process_obj.group = strdup(group))) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    }

    return 0;
}

int svx_process_set_pid_file(const char *pid_file)
{
    int r;
    
    if(NULL != pid_file)
    {
        if(0 != (r = svx_util_get_absolute_path(pid_file, svx_process_obj.pid_file, sizeof(svx_process_obj.pid_file))))
            SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    }

    return 0;
}

int svx_process_set_maxfds(int maxfds)
{
    svx_process_obj.maxfds = maxfds;

    return 0;
}

int svx_process_set_callbacks(svx_process_callback_t start_cb, void *start_cb_arg,
                              svx_process_callback_reload_t reload_cb, void *reload_cb_arg,
                              svx_process_callback_t stop_cb, void *stop_cb_arg)
{
    svx_process_obj.start_cb      = start_cb;
    svx_process_obj.start_cb_arg  = start_cb_arg;
    svx_process_obj.reload_cb     = reload_cb;
    svx_process_obj.reload_cb_arg = reload_cb_arg;
    svx_process_obj.stop_cb       = stop_cb;
    svx_process_obj.stop_cb_arg   = stop_cb_arg;

    return 0;
}

static int svx_process_set_default_pid_file()
{
    size_t len;
    int    r;
    
    if('\0' == svx_process_obj.pid_file[0])
    {
        if(0 != (r = svx_util_get_exe_pathname(svx_process_obj.pid_file, sizeof(svx_process_obj.pid_file), &len))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
        if(len + strlen(SVX_PROCESS_PREFIX_PID) > sizeof(svx_process_obj.pid_file) - 1) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOBUF, NULL);
        strncat(svx_process_obj.pid_file, SVX_PROCESS_PREFIX_PID, sizeof(svx_process_obj.pid_file) - len - 1);
    }
    
    return 0;
}

int svx_process_start()
{
    int r, r2;

    svx_process_obj.role = (svx_process_obj.watch_mode ? SVX_PROCESS_ROLE_WATCHER : SVX_PROCESS_ROLE_SINGLE);

    if(0 != (r = svx_process_init())) SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);
    
    /* register signal */
    if(0 != (r = svx_process_register_signal())) SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);

    /* set max FDs */
    if(svx_process_obj.maxfds > 0 && svx_util_is_root())
        if(0 != (r = svx_util_set_maxfds(svx_process_obj.maxfds))) SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);

    /* to daemon mode */
    if(svx_process_obj.daemon_mode)
    {
        if(0 != (r = svx_util_daemonize())) SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);
        svx_log_level_stdout = SVX_LOG_LEVEL_NONE;
    }

    /* open PID file */
    if(0 != (r = svx_process_set_default_pid_file())) SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);
    if(0 != (r = svx_util_pid_file_open(svx_process_obj.pid_file, &(svx_process_obj.pid_fd))))
        SVX_LOG_ERRNO_GOTO_ERR(end, r, "pid_file:%s\n", svx_process_obj.pid_file);
    
    /* set effective user ID and group ID (only for root user) */
    if(NULL != svx_process_obj.user && NULL != svx_process_obj.group && svx_util_is_root())
        if(0 != (r = svx_util_set_user_group(svx_process_obj.user, svx_process_obj.group)))
            SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);

    if(svx_process_obj.watch_mode)
    {
        if(0 != (r = svx_process_start_watcher())) SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);
    }
    else
    {
        if(0 != (r = svx_log_file_to_async_mode())) SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);
        if(0 != (r = svx_process_start_single())) SVX_LOG_ERRNO_GOTO_ERR(end, r, NULL);
    }

 end:
    if(svx_process_obj.pid_file[0] && svx_process_obj.pid_fd >= 0)
        if(0 != (r2 = svx_util_pid_file_close(svx_process_obj.pid_file, &(svx_process_obj.pid_fd))))
            SVX_LOG_ERRNO_ERR(r2, NULL);

    if(0 != (r2 = svx_process_uninit())) SVX_LOG_ERRNO_ERR(r2, NULL);

    return r;
}

static int svx_process_init_signaller(pid_t *pid)
{
    pid_t   target_pid;
    char    target_realpath[PATH_MAX];
    char    signaller_realpath[PATH_MAX];
    char    buf[64];
    ssize_t len;
    int     r;

    /* get the target pid */
    if(0 != (r = svx_process_set_default_pid_file())) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_util_pid_file_getpid(svx_process_obj.pid_file, &target_pid)))
    {
        if(ENOENT == r)
            return r;
        else
            SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    }
    
    /* get the target realpath */
    snprintf(buf, sizeof(buf), "/proc/%d/exe", target_pid);
    if((len = readlink(buf, target_realpath, sizeof(target_realpath) - 1)) < 0)
        SVX_LOG_ERRNO_RETURN_ERR(r = errno, NULL);
    target_realpath[len] = '\0';

    /* get the signaller realpath */
    if((len = readlink("/proc/self/exe", signaller_realpath, sizeof(signaller_realpath) - 1)) < 0)    
        SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    signaller_realpath[len] = '\0';

    /* check if running */
    if(0 != strcmp(target_realpath, signaller_realpath) && NULL == strstr(target_realpath, "valgrind"))
        SVX_LOG_ERRNO_RETURN_ERR(r = SVX_ERRNO_NOTRUN, "target:%s, signaller:%s\n", target_realpath, signaller_realpath);
    
    if(NULL != pid) *pid = target_pid;
    return 0;    
}

int svx_process_reload(int sig)
{
    pid_t pid;
    int   r;

    SVX_UTIL_UNUSED(sig);
    
    svx_process_obj.role = SVX_PROCESS_ROLE_SIGNALLER;
    if(0 != (r = svx_process_init())) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_process_init_signaller(&pid))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    
    if(0 != kill(pid, SIGHUP)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

    if(0 != (r = svx_process_uninit())) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    return 0;
}

int svx_process_stop(int sig)
{
    pid_t pid;
    int   r;

    SVX_UTIL_UNUSED(sig);

    svx_process_obj.role = SVX_PROCESS_ROLE_SIGNALLER;
    if(0 != (r = svx_process_init())) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_process_init_signaller(&pid))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    if(0 != kill(pid, SIGTERM)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

    if(0 != (r = svx_process_uninit())) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    return 0;
}

int svx_process_force_stop(int sig)
{
    pid_t pid, pgid;
    int   r;

    SVX_UTIL_UNUSED(sig);

    svx_process_obj.role = SVX_PROCESS_ROLE_SIGNALLER;
    if(0 != (r = svx_process_init())) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    if(0 != (r = svx_process_init_signaller(&pid))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    
    /* send SIGKILL to all process in the process-group */
    if(0 >= (pgid = getpgid(pid))) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(0 != kill(-pgid, SIGKILL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

    if(0 != (r = svx_process_uninit())) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    return 0;
}

int svx_process_is_running()
{
    int r;

    svx_process_obj.role = SVX_PROCESS_ROLE_SIGNALLER;
    if(0 != (r = svx_process_init())) SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    if(0 != (r = svx_process_init_signaller(NULL)))
    {
        if(ENOENT == r) goto err;
        else SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    }
    if(0 != (r = svx_process_uninit())) SVX_LOG_ERRNO_GOTO_ERR(err, r, NULL);
    return 1; /* running */

 err:
    return 0; /* not running */
}
