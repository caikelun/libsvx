/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fnmatch.h>
#include <limits.h>
#include <sys/types.h>
#include "svx_util.h"
#include "svx_errno.h"
#include "svx_log.h"

#define TEST_LOG_THD_CNT                     16
#define TEST_LOG_FILE_DIRNAME                "./"
#define TEST_LOG_FILE_PREFIX                 "tmp_log"
#define TEST_LOG_FILE_SUFFIX                 "txt"
#define TEST_LOG_FILE_PATTERN                TEST_LOG_FILE_PREFIX".*."TEST_LOG_FILE_SUFFIX
#define TEST_LOG_FILE_SIZE_MAX_EACH          (1 * 16 * 1024)
#define TEST_LOG_FILE_SIZE_MAX_TOTAL         (4 * 16 * 1024)
#define TEST_LOG_FILE_CACHE_SIZE_EACH        (1 *  1 * 1024)
#define TEST_LOG_FILE_CACHE_SIZE_TOTAL_LARGE (6 * 16 * 1024)
#define TEST_LOG_FILE_CACHE_SIZE_TOTAL_SMALL (3 *  1 * 1024)
#define TEST_LOG_FILE_FLUSH_INTERVAL_S       2

typedef enum
{
    TEST_LOG_MODE_BASE,     /* No special case occurs */
    TEST_LOG_MODE_TOO_MUCH, /* We log more bytes than the file-size-max-total */
    TEST_LOG_MODE_TOO_FAST  /* We log too fast(file-cache-size-total is too small), 
                               causing the file-cache to overflow */
} test_log_mode_t;

typedef struct
{
    pthread_t    thd;
    unsigned int thd_idx;
    unsigned int msg_idx_max;
    int          msg_idx_cur;
    unsigned int msg_cnt_cur;
    int          missing_front_b;
    int          consequent_b;
} test_log_thd_info_t;

static test_log_thd_info_t test_log_thds[TEST_LOG_THD_CNT];
static char                test_log_file_dirname[PATH_MAX] = "\0";

static int test_log_file_filter(const struct dirent *entry)
{
    int r;

    switch(r = fnmatch(TEST_LOG_FILE_PATTERN, entry->d_name, 0))
    {
    case 0           : return 1;
    case FNM_NOMATCH : return 0;
    default          : printf("fnmatch() failed. return:%d\n", r); return 0;
    }
}

static int test_log_clean()
{
    struct dirent **entry;
    int             n = 0;
    char            pathname[PATH_MAX];
    struct stat     st;

    if((n = scandir(test_log_file_dirname, &entry, test_log_file_filter, alphasort)) < 0)
    {
        printf("scandir() failed(). errno:%d\n", errno);
        return errno;
    }

    while(n--)
    {
        snprintf(pathname, sizeof(pathname), "%s%s", test_log_file_dirname, entry[n]->d_name);
        free(entry[n]);
        if(0 != lstat(pathname, &st))
        {
            printf("lstat() failed. errno:%d. clean: %s\n", errno, pathname);
            continue;
        }
        if(!S_ISREG(st.st_mode))
        {
            printf("!S_ISREG. clean: %s\n", pathname);
            continue;
        }
        if(0 != remove(pathname))
        {
            printf("remove() failed. errno:%d. remove: %s\n", errno, pathname);
        }
    }
    free(entry);
    return 0;
}

static int test_log_check(test_log_mode_t mode)
{
    int             r = 0;
    struct dirent **entry;
    int             n = 0;
    int             i = 0;
    char            pathname[PATH_MAX];
    struct stat     st;
    FILE           *f = NULL;
    char            msg[1024];
    char            parse_level;
    unsigned int    parse_thd_idx;
    unsigned int    parse_msg_idx;
    unsigned int    parse_missing_random_cnt;
    char           *p;
    int             format_err_b = 0;
    off_t           file_size_total = 0;
    off_t           file_size_each_max = 0;
    unsigned int    missing_random_cnt = 0;
    unsigned int    total_cnt = 0;
    unsigned int    total_cnt_writen = 0;

    /* read data, parse data */
    if((n = scandir(test_log_file_dirname, &entry, test_log_file_filter, alphasort)) < 0)
    {
        printf("scandir() failed(). errno:%d\n", errno);
        return errno;
    }
    for(i = 0; i < n; i++)
    {
        snprintf(pathname, sizeof(pathname), "%s%s", test_log_file_dirname, entry[i]->d_name);
        free(entry[i]);
        entry[i] = NULL;
        if(0 != lstat(pathname, &st))
        {
            printf("lstat() failed. errno:%d. check: %s\n", errno, pathname);
            r = errno;
            goto end;
        }
        if(!S_ISREG(st.st_mode))
        {
            printf("!S_ISREG. check: %s\n", pathname);
            r = 1;
            goto end;
        }

        if(st.st_size > file_size_each_max)
            file_size_each_max = st.st_size;
        file_size_total += st.st_size;

        if(NULL == (f = fopen(pathname, "r")))
        {
            printf("fopen() failed. errno:%d. check: %s\n", errno, pathname);
            r = errno;
            goto end;
        }
        while(NULL != fgets(msg, sizeof(msg), f))
        {
            format_err_b = 0;
            switch(parse_level = *msg)
            {
            case 'D':
                if(!(p = strrchr(msg, '-')) || (2 != sscanf(p, "- %u %u\n", &parse_thd_idx, &parse_msg_idx)))
                {
                    format_err_b = 1;
                    break;
                }
                if(parse_thd_idx >= TEST_LOG_THD_CNT || parse_msg_idx > test_log_thds[parse_thd_idx].msg_idx_max)
                {
                    format_err_b = 1;
                    break;
                }
                test_log_thds[parse_thd_idx].msg_cnt_cur++;
                if(-1 == test_log_thds[parse_thd_idx].msg_idx_cur && 0 != parse_msg_idx)
                    test_log_thds[parse_thd_idx].missing_front_b = 1;
                else if(test_log_thds[parse_thd_idx].msg_idx_cur + 1 != parse_msg_idx)
                    test_log_thds[parse_thd_idx].consequent_b = 0;
                test_log_thds[parse_thd_idx].msg_idx_cur = parse_msg_idx;
                break;
            case '!':
                if((1 != sscanf(msg, "! %u", &parse_missing_random_cnt)) || 0 == parse_missing_random_cnt)
                {
                    format_err_b = 1;
                    break;
                }
                missing_random_cnt += parse_missing_random_cnt;
                break;
            default:
                format_err_b = 1;
                break;
            }
            if(format_err_b)
            {
                printf("msg format err: %s\n", msg);
                r = 1;
                goto end;
            }
        }
        fclose(f);
        f = NULL;
    }
    if(file_size_each_max > TEST_LOG_FILE_SIZE_MAX_EACH)
    {
        printf("check file_size_each_max failed\n");
        r = 1;
        goto end;
    }
    if(file_size_total > TEST_LOG_FILE_SIZE_MAX_TOTAL)
    {
        printf("check file_size_total failed\n");
        r = 1;
        goto end;
    }

    /* get result */
    switch(mode)
    {
    case TEST_LOG_MODE_BASE:
        for(i = 0; i < TEST_LOG_THD_CNT; i++)
        {
            if(test_log_thds[i].msg_idx_max != test_log_thds[i].msg_idx_cur)
            {
                printf("mode: BASE. i:%d. msg_idx_max:%u, msg_idx_cur:%d\n", i, 
                       test_log_thds[i].msg_idx_max, test_log_thds[i].msg_idx_cur);
                r = 1;
                goto end;
            }
            if(test_log_thds[i].msg_idx_max + 1 != test_log_thds[i].msg_cnt_cur)
            {
                printf("mode: BASE. i:%d. msg_idx_max:%u, msg_cnt_cur:%u\n", i, 
                       test_log_thds[i].msg_idx_max, test_log_thds[i].msg_cnt_cur);
                r = 1;
                goto end;
            }
            if(1 == test_log_thds[i].missing_front_b)
            {
                printf("mode: BASE. i:%d. missing_front_b:%d\n", i, test_log_thds[i].missing_front_b);
                r = 1;
                goto end;
            }
            if(0 == test_log_thds[i].consequent_b)
            {
                printf("mode: BASE. i:%d. consequent_b:%d\n", i, test_log_thds[i].consequent_b);
                r = 1;
                goto end;
            }
        }
        if(missing_random_cnt > 0)
        {
            printf("mode: BASE. missing_random_cnt:%u\n", missing_random_cnt);
            r = 1;
            goto end;
        }
        break;
    case TEST_LOG_MODE_TOO_MUCH:
        if(missing_random_cnt > 0)
        {
            printf("mode: TOO_MUCH. missing_random_cnt:%u\n",  missing_random_cnt);
            r = 1;
            goto end;
        }
        for(i = 0; i < TEST_LOG_THD_CNT; i++)
        {
            if(0 == test_log_thds[i].consequent_b)
            {
                printf("mode: TOO_MUCH. i:%d. consequent_b:%d\n", i, test_log_thds[i].consequent_b);
                r = 1;
                goto end;
            }
        }
        break;
    case TEST_LOG_MODE_TOO_FAST:
        if(0 == missing_random_cnt)
        {
            printf("mode: TOO_FAST. Message discard not happen, try to reduced the file cache size and run again\n");
            r = 1;
            goto end;
        }
        for(i = 0; i < TEST_LOG_THD_CNT; i++)
        {
            total_cnt += (test_log_thds[i].msg_idx_max + 1);
            total_cnt_writen += test_log_thds[i].msg_cnt_cur;
        }
        if(total_cnt_writen + missing_random_cnt != total_cnt)
        {
            printf("mode: TOO_FAST. total_cnt_writen:%u, missing_random_cnt:%u, total_cnt:%u\n",
                   total_cnt_writen, missing_random_cnt, total_cnt);
            r = 1;
            goto end;
        }
        break;        
    default:
        printf("mode err\n");
        r = 1;
        goto end;
    }

 end:
    if(f) fclose(f);
    for(i = 0; i < n; i++)
    {
        if(entry[i])
            free(entry[i]);
    }
    free(entry);
    return r;
}

static void *test_log_thd_func(void *arg)
{
    test_log_thd_info_t *ti = (test_log_thd_info_t *)arg;
    unsigned int i = 0;

    for(i = 0; i <= ti->msg_idx_max; i++)
        SVX_LOG_DEBUG("%u %u\n", ti->thd_idx, i);

    return NULL;
}

static int test_log_write(test_log_mode_t mode)
{
    int r;
    unsigned int i;
    unsigned int msg_idx_max;

    switch(mode)
    {
    case TEST_LOG_MODE_BASE:
        msg_idx_max = 32 - 1;
        break;
    case TEST_LOG_MODE_TOO_MUCH:
        msg_idx_max = 64 - 1;
        break;
    case TEST_LOG_MODE_TOO_FAST:
        msg_idx_max = 32 - 1;
        break;
    default:
        printf("mode err\n");
        return 1;
    }

    for(i = 0; i < TEST_LOG_THD_CNT; i++)
    {
        test_log_thds[i].thd_idx = i;
        test_log_thds[i].msg_idx_max = msg_idx_max;
        test_log_thds[i].msg_idx_cur = -1;
        test_log_thds[i].msg_cnt_cur = 0;
        test_log_thds[i].missing_front_b = 0;
        test_log_thds[i].consequent_b = 1;
        if(0 != (r = pthread_create(&(test_log_thds[i].thd), NULL, &test_log_thd_func, &(test_log_thds[i]))))
        {
            while(i)
            {
                pthread_join(test_log_thds[i].thd, NULL);
                i--;
            }
            printf("pthread_create() failed. errno:%d\n", r);
            return r;
        }
    }
    for(i = 0; i < TEST_LOG_THD_CNT; i++)
    {
        pthread_join(test_log_thds[i].thd, NULL);
    }

    return 0;
}

static int test_log_init(test_log_mode_t mode, int async_mode)
{
    int r;
    size_t cache_size_total = (TEST_LOG_MODE_TOO_FAST == mode ? 
                               TEST_LOG_FILE_CACHE_SIZE_TOTAL_SMALL : TEST_LOG_FILE_CACHE_SIZE_TOTAL_LARGE);

    if(0 != (r = svx_log_file_init()))
    {
        printf("svx_log_file_init() failed\n");
        return r;
    }
    if(0 != (r = svx_log_file_set_dirname(TEST_LOG_FILE_DIRNAME)))
    {
        printf("svx_log_file_set_dirname() failed\n");
        goto err;
    }
    if(0 != (r = svx_log_file_set_prefix(TEST_LOG_FILE_PREFIX)))
    {
        printf("svx_log_file_set_prefix() failed\n");
        goto err;
    }
    if(0 != (r = svx_log_file_set_suffix(TEST_LOG_FILE_SUFFIX)))
    {
        printf("svx_log_file_set_suffix() failed\n");
        goto err;
    }
    if(0 != (r = svx_log_file_set_size_max(TEST_LOG_FILE_SIZE_MAX_EACH, TEST_LOG_FILE_SIZE_MAX_TOTAL)))
    {
        printf("svx_log_file_set_size_max() failed\n");
        goto err;
    }
    if(0 != (r = svx_log_file_set_cache_size(TEST_LOG_FILE_CACHE_SIZE_EACH, cache_size_total)))
    {
        printf("svx_log_file_set_cache_size() failed\n");
        goto err;
    }
    if(0 != (r = svx_log_file_set_cache_flush_interval(TEST_LOG_FILE_FLUSH_INTERVAL_S)))
    {
        printf("svx_log_file_set_cache_flush_interval() failed\n");
        goto err;
    }
    if(0 != (r = svx_log_set_timezone_mode(SVX_LOG_TIMEZONE_MODE_LOCAL)))
    {
        printf("svx_log_set_timezone_mode() failed\n");
        goto err;
    }
    if(0 != (r = svx_log_set_errno_mode(SVX_LOG_ERRNO_MODE_NUM_STR)))
    {
        printf("svx_log_set_errno_mode() failed\n");
        goto err;
    }
    if(0 != (r = svx_log_set_errno_to_str(svx_errno_to_str)))
    {
        printf("svx_log_set_errno_to_str() failed\n");
        goto err;
    }
    if(async_mode)
    {
        if(0 != (r = svx_log_file_to_async_mode()))
        {
            printf("svx_log_file_to_async_mode() failed\n");
            goto err;
        }
    }
    return 0;

 err:
    if(0 != (r = svx_log_file_uninit()))
        printf("svx_log_file_uninit() failed\n");

    return r;
}

static int test_log_uninit()
{
    int r;

    if(0 != (r = svx_log_file_uninit()))
        printf("svx_log_file_uninit() failed\n");

    return r;
}

static int test_log_do(test_log_mode_t mode, int async_mode)
{
    int r = 0;

    if(0 != (r = test_log_clean())) return r;
    if(0 != (r = test_log_init(mode, async_mode))) return r;
    if(0 != (r = test_log_write(mode)))
    {
        test_log_uninit();
        return r;
    }
    if(0 != (r = test_log_uninit())) return r;
    if(0 != (r = test_log_check(mode))) return r;

    return 0;
}

int test_log_runner()
{
    int r = 0;

    /* save file absolute dirname*/
    if(0 != (r = svx_util_get_absolute_path(TEST_LOG_FILE_DIRNAME, test_log_file_dirname, 
                                            sizeof(test_log_file_dirname))))
    {
        printf("svx_util_get_absolute_path() failed\n");
        goto end;
    }
    
    /* log all message to file only */
    svx_log_level_file   = SVX_LOG_LEVEL_DEBUG;
    svx_log_level_syslog = SVX_LOG_LEVEL_NONE;
    svx_log_level_stdout = SVX_LOG_LEVEL_NONE;

    /* test async write to file */
    if(0 != (r = test_log_do(TEST_LOG_MODE_BASE, 1)))
    {
        printf("mode: BASE, ASYNC. failed\n");
        goto end;
    }
    if(0 != (r = test_log_do(TEST_LOG_MODE_TOO_MUCH, 1)))
    {
        printf("mode: TOO_MUCH, ASYNC. failed\n");
        goto end;
    }
    if(0 != (r = test_log_do(TEST_LOG_MODE_TOO_FAST, 1)))
    {
        printf("mode: TOO_FAST, ASYNC. failed\n");
        goto end;
    }

    /* test sync write to file */
    if(0 != (r = test_log_do(TEST_LOG_MODE_BASE, 0)))
    {
        printf("mode: BASE, SYNC. failed\n");
        goto end;
    }
    if(0 != (r = test_log_do(TEST_LOG_MODE_TOO_MUCH, 0)))
    {
        printf("mode: TOO_MUCH, SYNC. failed\n");
        goto end;
    }

 end:
    test_log_clean();
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    return r;
}
