/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "svx_errno.h"

#define SVX_ERRNO_MAX_SYS          9999
#define SVX_ERRNO_MAX_SVX          19999
#define SVX_ERRNO_TYPE_STR_OK      "OK:Success"
#define SVX_ERRNO_TYPE_STR_UNKNOWN "UNK:Unknown error"
#define SVX_ERRNO_TYPE_SYS         "SYS"
#define SVX_ERRNO_TYPE_SVX         "SVX"
#define SVX_ERRNO_STR_UNKNOWN      "Unknown error"

static char *svx_errno_str_svx_map[] = {
    NULL,
    "Unknown error",
    "Invalid argument",
    "Out of memory",
    "Out of range",
    "No data available",
    "Operation not permitted",
    "PID file was locked",
    "Buffer length is not enough",
    "Timed out",
    "Reach limit",
    "Repeat key",
    "Not found",
    "Not supported",
    "Not connected",
    "Operation now in progress",
    "Not running",
    "Format error"
};
static size_t svx_errno_str_svx_map_size = 
    sizeof(svx_errno_str_svx_map) / sizeof(svx_errno_str_svx_map[0]);

int svx_errno_to_str(int errnum, char *buf, size_t buflen)
{
    int  len = 0;
    int  errno_fixed;
    char errmsg[1024];

    if(NULL == buf) return SVX_ERRNO_INVAL;
    
    if(0 == errnum)
    {
        /* OK */
        if(snprintf(buf, buflen, "%s", SVX_ERRNO_TYPE_STR_OK) >= buflen) return SVX_ERRNO_RANGE;
        return 0;
    }
    else if(errnum > 0 && errnum <= SVX_ERRNO_MAX_SYS)
    {
        /* system errno */
        if((len = snprintf(buf, buflen, "%s:", SVX_ERRNO_TYPE_SYS)) >= buflen) return SVX_ERRNO_RANGE;
        if(snprintf(buf + len, buflen - len, "%s", strerror_r(errnum, errmsg, sizeof(errmsg))) >= buflen - len)
            return SVX_ERRNO_RANGE;
        return 0;
    }
    else if(errnum > SVX_ERRNO_BASE_SVX && errnum <= SVX_ERRNO_MAX_SVX)
    {
        /* libsvx errno */
        if((len = snprintf(buf, buflen, "%s:", SVX_ERRNO_TYPE_SVX)) >= buflen) return SVX_ERRNO_RANGE;
        errno_fixed = errnum - SVX_ERRNO_BASE_SVX;
        if(errno_fixed > 0 && errno_fixed < svx_errno_str_svx_map_size)
        {
            if(snprintf(buf + len, buflen - len, "%s", svx_errno_str_svx_map[errno_fixed]) >= buflen - len)
                return SVX_ERRNO_RANGE;
            return 0;
        }
        else
        {
            if(snprintf(buf, buflen, "%s", SVX_ERRNO_TYPE_STR_UNKNOWN) >= buflen) return SVX_ERRNO_RANGE;
            return SVX_ERRNO_INVAL;
        }
    }
    else
    {
        /* unknown errno */
        if(snprintf(buf, buflen, "%s", SVX_ERRNO_TYPE_STR_UNKNOWN) >= buflen) return SVX_ERRNO_RANGE;
        return SVX_ERRNO_INVAL;
    }
}
