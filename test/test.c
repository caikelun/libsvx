/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <libgen.h>
#include <getopt.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

typedef struct
{
    char  *name;
    int  (*runner)();
    int    ret;
} test_info_t;

/*********************************************************/
int test_slist_runner();
int test_list_runner();
int test_stailq_runner();
int test_tailq_runner();
int test_splaytree_runner();
int test_rbtree_runner();
int test_log_runner();
int test_threadpool_runner();
int test_notifier_runner();
int test_circlebuf_runner();
int test_plc_runner();
int test_tcp_runner();
int test_udp_runner();
int test_icmp_runner();
int test_crash_runner();
int test_process_runner();

test_info_t test_infos[] = {
    {"slist",      &test_slist_runner,      -1},
    {"list",       &test_list_runner,       -1},
    {"stailq",     &test_stailq_runner,     -1},
    {"tailq",      &test_tailq_runner,      -1},
    {"splaytree",  &test_splaytree_runner,  -1},
    {"rbtree",     &test_rbtree_runner,     -1},
    {"log",        &test_log_runner,        -1},
    {"threadpool", &test_threadpool_runner, -1},
    {"notifier",   &test_notifier_runner,   -1},
    {"circlebuf",  &test_circlebuf_runner,  -1},
    {"PLC",        &test_plc_runner,        -1},
    {"tcp",        &test_tcp_runner,        -1},
    {"udp",        &test_udp_runner,        -1},
    {"icmp",       &test_icmp_runner,       -1},
    {"crash",      &test_crash_runner,      -1},
    {"process",    &test_process_runner,    -1},
    {NULL,         NULL,                    -1}
};
/*********************************************************/

#define STYLE_HEADER_DELIMITER      "\033[1;36m"
#define STYLE_HEADER_TESTNAME       "\033[1;31m"
#define STYLE_HEADER_RESULT         "\033[1;31m"
#define STYLE_HEADER_RESULT_SUMMARY "\033[1;33m"
#define STYLE_TAILER                "\033[0m"
#define DELIMITER                   STYLE_HEADER_DELIMITER              \
    "=============================================================================="STYLE_TAILER"\n"

static void test_show_help(char *exe_pathname)
{
    int i = -1;
    
    printf("\n"                                                         \
           "USAGE:\n"                                                   \
           "  %s -h | -g [TEST-NAME] | -d [TEST-NAME]\n"                \
           "\n"                                                         \
           "DESCRIPTION:\n"                                             \
           "  Run test specified by TEST-NAME, or all tests if without TEST-NAME.\n" \
           "\n"                                                         \
           "OPTIONS:\n"                                                 \
           "  -h  Show this help\n"                                     \
           "  -g  Run test via valgrind\n"                              \
           "  -d  Run test directly (without valgrind)\n"               \
           "\n"                                                         \
           "available TEST-NAME:\n", basename(exe_pathname));
    while(test_infos[++i].name)
        printf("  %s\n", test_infos[i].name);
    printf("\n");
}

static const char *test_get_errmsg(int errnum)
{
    switch(errnum)
    {
    case 0  : return "Pass";
    case -1 : return "Test not run";
    case -2 : return "Test was terminated by a signal";
    case 200: return "Valgrind report error";
    default : return "Error from test itself";
    }
}

static void test_show_result(test_info_t *test)
{
    int i = -1;
    int name_max = 0, name_cur = 0;
    int have_error = 0;

    printf(DELIMITER);

    if(NULL == test)
    {
        while(test_infos[++i].name)
        {
            name_cur = strlen(test_infos[i].name);
            if(name_cur > name_max)
                name_max = name_cur;
        }
        i = -1;
        while(test_infos[++i].name)
        {
            test = &(test_infos[i]);
            printf(STYLE_HEADER_RESULT"[[ %-*s ]] return: %-5d ==> %s"STYLE_TAILER"\n", 
                   name_max, test->name, test->ret, test_get_errmsg(test->ret));
            if(0 != test->ret) have_error = 1;
        }
    }
    else
    {
        printf(STYLE_HEADER_RESULT"[[ %s ]] return: %d ==> %s"STYLE_TAILER"\n",
               test->name, test->ret, test_get_errmsg(test->ret));
        if(0 != test->ret) have_error = 1;
    }

    printf(DELIMITER);
    printf(STYLE_HEADER_RESULT_SUMMARY"%s"STYLE_TAILER"\n", have_error ? "Failed" : "Pass");
    printf(DELIMITER);
}

static int test_run_valgrind(char *exe_pathname, test_info_t *test)
{
    pid_t pid;
    int   status;
    char* args[16];
    int   i = 0;

    switch(pid = fork())
    {
    case -1: /* error */
        printf("fork() failed - errno:%d - %s\n", errno, strerror(errno));
        return -1;
    case 0: /* child */
        printf(DELIMITER);
        printf(STYLE_HEADER_TESTNAME"[[ %s ]]"STYLE_TAILER"\n", test->name);
        args[i++] = "valgrind";
        args[i++] = "--error-exitcode=200";
        args[i++] = "--tool=memcheck";
        args[i++] = "--leak-check=full";
        args[i++] = "--show-reachable=yes";
        args[i++] = "--track-origins=yes";
        args[i++] = "--track-fds=yes";
        args[i++] = "--num-callers=100";
        args[i++] = exe_pathname;
        args[i++] = "-q";
        args[i++] = test->name;
        args[i++] = NULL;
        execvp(args[0], args);
        if(ENOENT == errno)
            printf("\n\n\n***** valgrind NOT found! *****\n\n\n");
        else
            printf("execvp() failed - errno:%d - %s\n", errno, strerror(errno));
        exit(1);
    default: /* parent */
        if(waitpid(pid, &status, 0) < 0)
        {
            printf("waitpid() failed - errno:%d - %s\n", errno, strerror(errno));
            return -1;
        }
        test->ret = WIFEXITED(status) ? WEXITSTATUS(status) : -2;
        return 0;
    }
}

static int test_run_valgrind_all(char *exe_pathname)
{
    int i = -1;

    while(test_infos[++i].name)
    {
        test_run_valgrind(exe_pathname, &(test_infos[i]));
    }
    test_show_result(NULL);
    return 0;
}

int test_run_valgrind_by_name(char *exe_pathname, const char *test_name)
{
    int i = -1;

    while(test_infos[++i].name)
    {
        if(0 == strcmp(test_name, test_infos[i].name))
        {
            test_run_valgrind(exe_pathname, &(test_infos[i]));
            test_show_result(&(test_infos[i]));
            return test_infos[i].ret;
        }
    }
    test_show_help(exe_pathname);
    return 1;
}

static int test_run_directly(char *exe_pathname, test_info_t *test)
{
    pid_t pid;
    int   status;
    char* args[16];
    int   i = 0;

    printf("run test directly for: %s ...\n", test->name);
    switch(pid = fork())
    {
    case -1: /* error */
        printf("fork() failed - errno:%d - %s\n", errno, strerror(errno));
        return -1;
    case 0: /* child */
        args[i++] = exe_pathname;
        args[i++] = "-q";
        args[i++] = test->name;
        args[i++] = NULL;
        execvp(args[0], args);
        exit(1);
    default: /* parent */
        if(waitpid(pid, &status, 0) < 0)
        {
            printf("waitpid() failed - errno:%d - %s\n", errno, strerror(errno));
            return -1;
        }
        test->ret = WIFEXITED(status) ? WEXITSTATUS(status) : -2;
        return 0;
    }
}

static int test_run_directly_all(char *exe_pathname)
{
    int i = -1;

    while(test_infos[++i].name)
    {
        test_run_directly(exe_pathname, &(test_infos[i]));
    }
    test_show_result(NULL);
    return 0;
}

int test_run_directly_by_name(char *exe_pathname, const char *test_name)
{
    int i = -1;

    while(test_infos[++i].name)
    {
        if(0 == strcmp(test_name, test_infos[i].name))
        {
            test_run_directly(exe_pathname, &(test_infos[i]));
            test_show_result(&(test_infos[i]));
            return test_infos[i].ret;
        }
    }
    test_show_help(exe_pathname);
    return 1;
}

int test_run_directly_by_name_quiet(const char *test_name)
{
    int i = -1;

    while(test_infos[++i].name)
    {
        if(0 == strcmp(test_name, test_infos[i].name))
        {
            /* when call by valgrind, return the test itself's return-value to valgrind */
            return test_infos[i].runner();
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    if(argc == 2)
    {
        if(0 == strcmp(argv[1], "-g"))
            return test_run_valgrind_all(argv[0]);
        else if(0 == strcmp(argv[1], "-d"))
            return test_run_directly_all(argv[0]);    }
    else if(argc == 3)
    {
        if(0 == strcmp(argv[1], "-g"))
            return test_run_valgrind_by_name(argv[0], argv[2]);
        else if(0 == strcmp(argv[1], "-d"))
            return test_run_directly_by_name(argv[0], argv[2]);
        else if(0 == strcmp(argv[1], "-q"))
            return test_run_directly_by_name_quiet(argv[2]);
    }

    test_show_help(argv[0]);
    exit(1);
}
