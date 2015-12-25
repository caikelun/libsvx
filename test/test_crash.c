/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <fnmatch.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "svx_log.h"
#include "svx_util.h"
#include "svx_crash.h"

#define TEST_CRASH_HEAD_MSG      "your_program v1.0.0 crash dump\n"
#define TEST_CRASH_EXTRA_MSG     "write your_program's internal status here\n"

static size_t test_crash_extra_msg_len = 0;
static pid_t  test_crash_pid = 0;

#define TEST_EXIT(c) do {SVX_LOG_ERR("exit(1). line: %d. errno:%d\n", __LINE__, errno); exit(c);} while(0)

int cc()
{
    int a = 10;
    int b = 0;
    int c = a / b; /* crash here */
    int *p = &c;

    return *p;
}

int bb()
{
    return cc();
}

int aa()
{
    return bb();
}

void cb(int fd, void *arg)
{
    /* append any messages to the dump file */
    if(fd >= 0)
    {
        write(fd, TEST_CRASH_EXTRA_MSG, test_crash_extra_msg_len);
    }

    /* do any thing your want, 
       your can only invoke async-signal-safe functions here, see man(7) signal */
    //write(STDOUT_FILENO, "crash dump file ...... created\n", 31);
}

int test_crash_do()
{
    test_crash_extra_msg_len = strlen(TEST_CRASH_EXTRA_MSG);
    
    /* we need not core dump file */
    struct rlimit rlim = {.rlim_cur = 0, .rlim_max = 0};
    if(setrlimit(RLIMIT_CORE, &rlim)) TEST_EXIT(2);

    if(svx_crash_set_callback(cb, NULL)) TEST_EXIT(2);
    if(svx_crash_set_head_msg(TEST_CRASH_HEAD_MSG)) TEST_EXIT(2);
    if(svx_crash_set_timezone_mode(SVX_CRASH_TIMEZONE_MODE_LOCAL)) TEST_EXIT(2);
    if(svx_crash_set_dirname("./")) TEST_EXIT(2);
    if(svx_crash_set_prefix("test_crash")) TEST_EXIT(2);
    if(svx_crash_set_suffix("log")) TEST_EXIT(2);
    if(svx_crash_set_max_dumps(1)) TEST_EXIT(2);
    if(svx_crash_register_signal_handler()) TEST_EXIT(2);

    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    
    return aa();
}

static int test_crash_filter(const struct dirent *entry)
{
    char buf[1024];
    
    snprintf(buf, sizeof(buf), "test_crash.*.%d.log", test_crash_pid);

    switch(fnmatch(buf , entry->d_name, 0))
    {
    case 0           : return 1;
    case FNM_NOMATCH : return 0;
    default          : TEST_EXIT(1);
    }
}

int test_crash_check(int status)
{
    char            dirname[PATH_MAX];
    char            pathname[PATH_MAX];
    struct dirent **entry;
    struct stat     st;
    int             fd;
    char            buf[1024 * 1024];
    ssize_t         len = 0;

    /*
     * *** NOTICE ***
     *
     *     Because child process was terminated by a signal, 
     * so WEXITSTATUS(status) will always return 0 even if 
     * there is a memory leak in the child process. 
     *
     *     This means: we lost the error-exitcode from valgrind,
     * we have to check valgrind's output message by ourself.
     */

    // This won't work. WEXITSTATUS(status) always 0 !!!
    //
    //if(200 == WEXITSTATUS(status))
    //    valgrind_found_leaks();

    /* check exit status */
    if(WIFEXITED(status)) TEST_EXIT(1); //terminated normally?
    if(!WIFSIGNALED(status)) TEST_EXIT(1); //not terminated by a signal?

    /* get crash dump file pathname */
    if(svx_util_get_exe_dirname(dirname, sizeof(dirname), NULL)) TEST_EXIT(1);
    if(1 != scandir(dirname, &entry, test_crash_filter, alphasort)) TEST_EXIT(1);
    snprintf(pathname, sizeof(pathname), "%s%s", dirname, entry[0]->d_name);
    free(entry[0]);
    free(entry);
    if(lstat(pathname, &st)) TEST_EXIT(1);
    if(!S_ISREG(st.st_mode)) TEST_EXIT(1);
    if(0 == st.st_size) TEST_EXIT(1);

    /* get crash dump file content */
    if(0 > (fd = open(pathname, O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH))) TEST_EXIT(1);
    if(st.st_size != (len = read(fd, buf, sizeof(buf)))) TEST_EXIT(1);
    buf[len] = '\0';
    close(fd);

    /* check crash dump file content */
    if(!strstr(buf, TEST_CRASH_HEAD_MSG)) TEST_EXIT(1);
    if(!strstr(buf, "*** Registers:")) TEST_EXIT(1);
    if(!strstr(buf, "*** Backtrace:")) TEST_EXIT(1);
    if(!strstr(buf, "*** Memory map:")) TEST_EXIT(1);
    if(!strstr(buf, TEST_CRASH_EXTRA_MSG)) TEST_EXIT(1);

    /* remove the crash dump file */
    remove(pathname);
    
    return 0;
}

int test_crash_runner()
{
    int status;

    switch(test_crash_pid = fork())
    {
    case -1:  /* error */
        TEST_EXIT(1);
    case 0:   /* child */
        test_crash_do();
        return 0;
    default:  /* parent */
        if(waitpid(test_crash_pid, &status, 0) < 0) TEST_EXIT(1);
        if(0 != test_crash_check(status)) TEST_EXIT(1);
        fclose(stdin);
        fclose(stdout);
        fclose(stderr);
        return 0;
    }
}
