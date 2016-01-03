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
#include <netinet/tcp.h>
#include <pwd.h>
#include <grp.h>
#include "svx_util.h"
#include "svx_errno.h"
#include "svx_log.h"

static __thread pid_t svx_util_tid_cache = 0;
static pthread_mutex_t svx_util_tid_mutex = PTHREAD_MUTEX_INITIALIZER;

pid_t svx_util_get_tid()
{
    if(0 == svx_util_tid_cache)
    {
        pthread_mutex_lock(&svx_util_tid_mutex);
        if(0 == svx_util_tid_cache)
        {
            svx_util_tid_cache = syscall(SYS_gettid);
        }
        pthread_mutex_unlock(&svx_util_tid_mutex);
    }

    return svx_util_tid_cache;
}

int svx_util_is_root()
{
    return (0 == getuid()) ? 1 : 0;
}

int svx_util_get_exe_pathname(char *pathname, size_t len, size_t *result_len)
{
    ssize_t rslt;

    if(NULL == pathname || 0 == len)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "pathname:%p, len:%zu\n", pathname, len);

    if((rslt = readlink("/proc/self/exe", pathname, len - 1)) < 0)
        SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    else if((size_t)rslt > len - 1)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOBUF, NULL);
    else
    {
        pathname[rslt] = '\0';
        if(NULL != result_len) *result_len = (size_t)rslt;
        return 0;
    }
}

int svx_util_get_exe_basename(char *basename, size_t len, size_t *result_len)
{
    char    buf[PATH_MAX];
    size_t  rlen = 0;
    char   *p;
    int     r;

    if(NULL == basename || 0 == len)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "basename:%p, len:%zu\n", basename, len);

    if(0 != (r = svx_util_get_exe_pathname(buf, sizeof(buf), &rlen)))
        SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    
    p = strrchr(buf, '/');
    if(NULL == p || '\0' == *(p + 1))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NODATA, "readlink(/proc/self/exe) return:%s\n", buf);
    if(rlen - (p - buf) - 1 > len - 1)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOBUF, NULL);
    
    strncpy(basename, p + 1, len);
    basename[len - 1] = '\0';
    if(NULL != result_len) *result_len = rlen - (p - buf) - 1;
    return 0;
}

int svx_util_get_exe_dirname(char *dirname, size_t len, size_t *result_len)
{
    char    buf[PATH_MAX];
    size_t  rlen = 0;
    char   *p;
    int     r;

    if(NULL == dirname || 0 == len)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "dirname:%p, len:%zu\n", dirname, len);

    if(0 != (r = svx_util_get_exe_pathname(buf, sizeof(buf), &rlen)))
        SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    p = strrchr(buf, '/');
    if(NULL == p || '\0' == *(p + 1))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NODATA, "readlink(/proc/self/exe) return:%s\n", buf);
    *(p + 1) = '\0';
    if((size_t)(p - buf + 1) > len - 1)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOBUF, NULL);

    strncpy(dirname, buf, len);
    dirname[len - 1] = '\0';
    if(NULL != result_len) *result_len = p - buf + 1;
    return 0;
}

int svx_util_get_absolute_path(const char *path, char *buf, size_t buf_len)
{
    size_t rlen = 0;
    int    r;

    if(NULL == path || NULL == buf || 0 == buf_len)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "path:%p, buf:%p, buf_len:%zu\n", path, buf, buf_len);

    if('/' == path[0])
    {
        /* absolute path */
        strncpy(buf, path, buf_len - 1);
        buf[buf_len - 1] = '\0';
    }
    else
    {
        /* relative path */
        if(0 != (r = svx_util_get_exe_dirname(buf, buf_len, &rlen))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
        strncat(buf, path, buf_len - rlen - 1);
    }

    return 0;
}

int svx_util_set_nonblocking(int fd)
{
    int opts;

    if(fd < 0) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d\n", fd);

    if(-1 == (opts = fcntl(fd, F_GETFL))) SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);
    opts |= O_NONBLOCK;
    if(-1 == fcntl(fd, F_SETFL, opts)) SVX_LOG_ERRNO_RETURN_ERR(errno, "fd:%d\n", fd);
    return 0;
}

int svx_util_unset_nonblocking(int fd)
{
    int opts;

    if(fd < 0) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "fd:%d\n", fd);

    if(-1 == (opts = fcntl(fd, F_GETFL))) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    opts &= ~O_NONBLOCK;
    if(-1 == fcntl(fd, F_SETFL, opts)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    return 0;
}

int svx_util_daemonize()
{
    pid_t            pid;
    struct sigaction sa;
    int              fd;

    /* clear file creation mask */
    umask(0);

    /* ignore interactive signal */
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(0 != sigaction(SIGTTOU, &sa, NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(0 != sigaction(SIGTTIN, &sa, NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(0 != sigaction(SIGTSTP, &sa, NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    
    /* become a session leader to lose controlling TTY */
    if((pid = fork()) < 0) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    else if(pid != 0)
    {
        close(STDIN_FILENO); /* let valgrind happy */
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        exit(0); /* parent */
    }
    setsid();

    /* ensure future opens won't allocate controlling TTYs */
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(0 != sigaction(SIGHUP, &sa, NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if((pid = fork()) < 0) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    else if(pid != 0)
    {
        close(STDIN_FILENO); /* let valgrind happy */
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        exit(0); /* parent */
    }

    /* change the current working directory to the root */
    if(chdir("/") < 0) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

    /* attach file descriptors 0, 1, and 2 to /dev/null */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    if(0 > (fd = open("/dev/null", O_RDWR))) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(0 > dup2(fd, STDIN_FILENO)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(0 > dup2(fd, STDOUT_FILENO)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(0 > dup2(fd, STDERR_FILENO)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(fd > STDERR_FILENO) close(fd);

    return 0;
}

int svx_util_pid_file_open(const char *pathname, int *fd)
{
    struct flock fl;
    char         buf[32];
    ssize_t      len;
    int          r;

    if(NULL == pathname || NULL == fd) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "pathname:%p, fd:%p\n", pathname, fd);

    if(0 > (*fd = open(pathname, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)))
        SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);

    fl.l_type   = F_WRLCK;
    fl.l_start  = 0;
    fl.l_whence = SEEK_SET;
    fl.l_len    = 0;
    if(fcntl(*fd, F_SETLK, &fl) < 0)
    {
        if (EACCES == errno || EAGAIN == errno)
            SVX_LOG_ERRNO_GOTO_ERR(err, r = SVX_ERRNO_PIDLCK, "sys errno:%d\n", errno); /* PID file was locked */
        else
            SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL); /* other error */
    }
    
    if(0 != ftruncate(*fd, 0)) SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);
    len = snprintf(buf, sizeof(buf), "%d", getpid());
    if(0 > write(*fd, buf, len)) SVX_LOG_ERRNO_GOTO_ERR(err, r = errno, NULL);

    return 0;

 err:
    if(*fd >= 0)
    {
        close(*fd);
        *fd = -1;
    }
    return r;
}

int svx_util_pid_file_getpid(const char *pathname, pid_t *pid)
{
    int     fd = -1;
    char    buf[32];
    ssize_t len;
    int     r = 0;
    
    if(NULL == pathname || NULL == pid) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "pathname:%p, pid:%p\n", pathname, pid);

    if(0 > (fd = open(pathname, O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)))
    {
        if(ENOENT == errno)
        {
            r = errno;
            goto end;
        }
        else
        {
            SVX_LOG_ERRNO_GOTO_ERR(end, r = errno, NULL);
        }
    }

    if(0 > (len = read(fd, buf, sizeof(buf))))
        SVX_LOG_ERRNO_GOTO_ERR(end, r = errno, NULL);
    buf[len] = '\0';
    
    if(0 >= (*pid = atoi(buf)))
        SVX_LOG_ERRNO_GOTO_ERR(end, r = SVX_ERRNO_FORMAT, "pid in the pidfile is:%l\n", *pid);

 end:
    if(fd >= 0) close(fd);
    return r;
}

int svx_util_pid_file_close(const char *pathname, int *fd)
{
    if(NULL == pathname || NULL == fd) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "pathname:%p, fd:%p\n", pathname, fd);
    if(*fd < 0) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "*fd:%d\n", *fd);

    if(0 != close(*fd)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    *fd = -1;
    
    if(0 != unlink(pathname)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

    return 0;
}

int svx_util_set_maxfds(rlim_t maxfds)
{
    struct rlimit rlim;

    if(0 != getrlimit(RLIMIT_NOFILE, &rlim)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

    if(maxfds > rlim.rlim_cur || maxfds > rlim.rlim_max)
    {
        rlim.rlim_cur = maxfds;
        rlim.rlim_max = maxfds;
        if(0 != setrlimit(RLIMIT_NOFILE, &rlim)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    }

    return 0;
}

int svx_util_set_user_group(const char *user, const char *group)
{
    struct passwd *pwd;
    struct group  *grp;
        
    if(NULL == user || NULL == group) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "user:%p, group:%p\n", user, group);

    /* get user and group info */
    if(NULL == (pwd = getpwnam(user))) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(NULL == (grp = getgrnam(group))) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

    /* set group */
    if(-1 == setgid(grp->gr_gid)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(-1 == setgroups(0, NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(-1 == initgroups(user, grp->gr_gid)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

    /* set user */
    if(-1 == setuid(pwd->pw_uid)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

    /* set home dir */
    if(-1 == chroot(pwd->pw_dir)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(-1 == chdir("/")) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

    return 0;
}
