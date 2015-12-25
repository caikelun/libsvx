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
#include "svx_process.h"

#define TEST_EXIT do {printf("failed. line: %d. errno:%d.\n", __LINE__, errno); exit(1);} while(0)

static pid_t test_process_pid = -1;
static int   test_process_watch_mode = 0;
static int   test_process_daemon_mode = 0;

static int test_process_command(const char *cmd)
{
    pid_t pid;
    int   status, status2;
    int   r;
    
    switch(pid = fork())
    {
    case -1:
        
        TEST_EXIT;
        
    case 0: /* child */
        
        if(svx_process_set_mode(test_process_watch_mode, test_process_daemon_mode)) TEST_EXIT;
        if(0 == strcmp(cmd, "status"))
        {
            r = svx_process_is_running();
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
            exit(r);
        }
        else if(0 == strcmp(cmd, "start"))
        {
            if(svx_process_start()) TEST_EXIT;
        }
        else if(0 == strcmp(cmd, "reload"))
        {
            if(svx_process_reload()) TEST_EXIT;
        }
        else if(0 == strcmp(cmd, "stop"))
        {
            if(svx_process_stop()) TEST_EXIT;
        }
        else
        {
            TEST_EXIT;
        }
        
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        exit(0);
        
    default: /* parent */

        /* waiting for the short-command(except "start") process termination */
        if(0 == strcmp(cmd, "status") ||
           0 == strcmp(cmd, "reload") ||
           0 == strcmp(cmd, "stop"))
        {
            if(waitpid(pid, &status, 0) < 0) TEST_EXIT;
            if(WIFSIGNALED(status)) TEST_EXIT;
            if(!WIFEXITED(status)) TEST_EXIT;
        }

        /* checking process exit code(except "status" and "start") */
        if(0 == strcmp(cmd, "reload") ||
           0 == strcmp(cmd, "stop"))
        {
            if(0 != WEXITSTATUS(status)) TEST_EXIT;
        }
        
        /* waiting for the started test process termination */
        if(0 == strcmp(cmd, "stop"))
        {
            if(waitpid(test_process_pid, &status2, 0) < 0) TEST_EXIT;
            test_process_pid = -1;
            if(WIFSIGNALED(status2)) TEST_EXIT;
            if(!WIFEXITED(status2)) TEST_EXIT;
        }

        /* save the started process pid */
        if(0 == strcmp(cmd, "start"))
        {
            test_process_pid = pid;
        }

        if(0 == strcmp(cmd, "status"))
            return WEXITSTATUS(status);
        else
            return 0;
    }
}

static int test_process_status()
{
    return test_process_command("status");
}

static int test_process_start()
{
    return test_process_command("start");
}

static int test_process_reload()
{
    return test_process_command("reload");
}

static int test_process_stop()
{
    return test_process_command("stop");
}

static int test_process_go(int watch_mode, int daemon_mode)
{
    test_process_watch_mode = watch_mode;
    test_process_daemon_mode = daemon_mode;

    if(0 != test_process_status()) TEST_EXIT;
    
    if(test_process_start()) TEST_EXIT;
    usleep(1 * 1000 * 1000);
    if(1 != test_process_status()) TEST_EXIT;
    
    if(test_process_reload()) TEST_EXIT;
    if(1 != test_process_status()) TEST_EXIT;
    
    if(test_process_stop()) TEST_EXIT;
    usleep(1 * 1000 * 1000);
    if(0 != test_process_status()) TEST_EXIT;
    
    return 0;
}

int test_process_runner()
{
    if(test_process_go(0, 0)) TEST_EXIT;
    if(test_process_go(0, 1)) TEST_EXIT;
    if(test_process_go(1, 0)) TEST_EXIT;
    if(test_process_go(1, 1)) TEST_EXIT;
    
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    return 0;
}
