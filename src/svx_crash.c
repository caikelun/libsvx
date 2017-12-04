/*
 * This source code has been dedicated to the public domain by the authors.
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
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <dirent.h>
#include <fnmatch.h>
#include <execinfo.h>
#include <limits.h>
#include <ucontext.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include "svx_crash.h"
#include "svx_log.h"
#include "svx_errno.h"
#include "svx_util.h"

#define SVX_CRASH_OS_ARCH_NOT_SPT "OS/arch not supported\n"
#define SVX_CRASH_DEFAULT_DIRNAME "./"
#define SVX_CRASH_DEFAULT_SUFFIX  "crash"
#define SVX_CRASH_DELIMITER       "**********************************************************************\n"

static struct sigaction           svx_crash_old_sigact_segv;
static struct sigaction           svx_crash_old_sigact_fpe;
static struct sigaction           svx_crash_old_sigact_ill;
static struct sigaction           svx_crash_old_sigact_bus;
static struct sigaction           svx_crash_old_sigact_abrt;
#ifdef SIGSTKFLT
static struct sigaction           svx_crash_old_sigact_stkflt;
#endif

static svx_crash_callback_t       svx_crash_cb                      = NULL;
static void                      *svx_crash_cb_arg                  = NULL;
static char                       svx_crash_head_msg[1024]          = "\0";
static svx_crash_timezone_mode_t  svx_crash_timezone_mode           = SVX_CRASH_TIMEZONE_MODE_GMT;
static long                       svx_crash_timezone_off            = 0;
static char                       svx_crash_timezone[6]             = "+0000";
static char                       svx_crash_hostname[HOST_NAME_MAX] = "unknownhost";
static char                       svx_crash_dirname[PATH_MAX]       = "\0";
static char                       svx_crash_prefix[NAME_MAX]        = "\0";
static char                       svx_crash_suffix[NAME_MAX]        = "\0";
static char                       svx_crash_pattern[NAME_MAX]       = "\0";
static size_t                     svx_crash_max_dumps               = 0;
static uint8_t                    svx_crash_stack[SIGSTKSZ * 10];

/**********************************************************************/
/* svx_crash_time2tm() - convert time_t to struct tm
   This code is taken from GLibC under terms of LGPLv2+
*/

/* Nonzero if YEAR is a leap year (every 4 years,
   except every 100th isn't, and every 400th is).  */
#define SVX_CRASH_ISLEAP(year)         ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))

#define SVX_CRASH_SECS_PER_HOUR        (60 * 60)
#define SVX_CRASH_SECS_PER_DAY         (SVX_CRASH_SECS_PER_HOUR * 24)
#define SVX_CRASH_DIV(a, b)            ((a) / (b) - ((a) % (b) < 0))
#define SVX_CRASH_LEAPS_THRU_END_OF(y) (SVX_CRASH_DIV(y, 4) - SVX_CRASH_DIV(y, 100) + SVX_CRASH_DIV(y, 400))

static const unsigned short int svx_crash_mon_yday[2][13] =
{
    /* Normal years.  */
    { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
    /* Leap years.  */
    { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
};

/* Compute the `struct tm' representation of *T,
   offset GMTOFF seconds east of UTC,
   and store year, yday, mon, mday, wday, hour, min, sec into *RESULT.
   Return RESULT if successful.  */
static struct tm *svx_crash_time2tm(const time_t *timep, long gmtoff, struct tm *result)
{
    time_t days, rem, y;
    const unsigned short int *ip;

    if(NULL == timep || NULL == result) return NULL;

    result->tm_gmtoff = gmtoff;

    days = *timep / SVX_CRASH_SECS_PER_DAY;
    rem = *timep % SVX_CRASH_SECS_PER_DAY;
    rem += gmtoff;
    while (rem < 0)
    {
        rem += SVX_CRASH_SECS_PER_DAY;
        --days;
    }
    while (rem >= SVX_CRASH_SECS_PER_DAY)
    {
        rem -= SVX_CRASH_SECS_PER_DAY;
        ++days;
    }
    result->tm_hour = rem / SVX_CRASH_SECS_PER_HOUR;
    rem %= SVX_CRASH_SECS_PER_HOUR;
    result->tm_min = rem / 60;
    result->tm_sec = rem % 60;
    /* January 1, 1970 was a Thursday.  */
    result->tm_wday = (4 + days) % 7;
    if (result->tm_wday < 0)
        result->tm_wday += 7;
    y = 1970;

    while (days < 0 || days >= (SVX_CRASH_ISLEAP(y) ? 366 : 365))
    {
        /* Guess a corrected year, assuming 365 days per year.  */
        time_t yg = y + days / 365 - (days % 365 < 0);

        /* Adjust DAYS and Y to match the guessed year.  */
        days -= ((yg - y) * 365
                 + SVX_CRASH_LEAPS_THRU_END_OF (yg - 1)
                 - SVX_CRASH_LEAPS_THRU_END_OF (y - 1));

        y = yg;
    }
    result->tm_year = y - 1900;
    if (result->tm_year != y - 1900)
    {
        /* The year cannot be represented due to overflow.  */
        errno = EOVERFLOW;
        return NULL;
    }
    result->tm_yday = days;
    ip = svx_crash_mon_yday[SVX_CRASH_ISLEAP(y)];
    for (y = 11; days < (long int) ip[y]; --y)
        continue;
    days -= ip[y];
    result->tm_mon = y;
    result->tm_mday = days + 1;
    return result;
}
/* the end of svx_crash_time2tm() */
/**********************************************************************/

/**********************************************************************/
/* svx_crash_buf_t - buffer in the stack */

typedef struct
{
    char   *buf;
    size_t  buf_used;
    size_t  buf_size;
} svx_crash_buf_t;
#define SVX_CRASH_BUF_INITIALIZER {NULL, 0, 0}

static void svx_crash_buf_init(svx_crash_buf_t *self, char *buf, size_t buf_size)
{
    self->buf      = buf;
    self->buf[0]   = '\0';
    self->buf_used = 0;
    self->buf_size = buf_size;
}

static void svx_crash_buf_reset(svx_crash_buf_t *self)
{
    self->buf[0]   = '\0';
    self->buf_used = 0;
}

static char *svx_crash_buf_get_buf(svx_crash_buf_t *self)
{
    return self->buf;
}

static size_t svx_crash_buf_get_buflen(svx_crash_buf_t *self)
{
    return self->buf_used;
}

static void svx_crash_buf_append_str(svx_crash_buf_t *self, const char *str)
{
    while(self->buf_size - self->buf_used > 0 && *str)
    {
        *(self->buf + self->buf_used) = *str;
        str++;
        self->buf_used++;
    }
    *(self->buf + self->buf_used) = '\0';
}

static void svx_crash_buf_append_num(svx_crash_buf_t *self, uintmax_t value, int negative, 
                                     unsigned int base, size_t len)
{
    const char *digits_map = "0123456789abcdefghijklmnopqrstuvwxyz";
    unsigned int digits = 0, digits_tmp = 0;
    uintmax_t value_tmp = value;
    char *buf_lim;
    int signbit = (negative ? 1 : 0); /* negative number or positive number */

    if(base > 36) return;
    if(len > 0 && len > self->buf_size - self->buf_used - signbit - 1) return;

    /* get digits */
    do 
        digits++;
    while(0 != (value_tmp /= base));
    if(digits > self->buf_size - self->buf_used - signbit - 1) return;

    /* output sign bit */
    if(signbit)
    {
        *(self->buf + self->buf_used) = '-';
        self->buf_used++;
    }

    /* fill zeros */
    digits_tmp = digits;
    while(self->buf_size - self->buf_used > 0 && len > digits_tmp)
    {
        *(self->buf + self->buf_used) = '0';
        self->buf_used++;
        digits_tmp++;
    }

    /* output digits */
    buf_lim = self->buf + self->buf_used + digits;
    do
    {
        *--buf_lim = digits_map[value % base];
        self->buf_used++;
    }
    while(0 != (value /= base));
    *(self->buf + self->buf_used) = '\0';
}

static void svx_crash_buf_append_uint(svx_crash_buf_t *self, uintmax_t value, unsigned int base, size_t len)
{
    svx_crash_buf_append_num(self, value, 0, base, len);
}

static void svx_crash_buf_append_int(svx_crash_buf_t *self, intmax_t value, unsigned int base, size_t len)
{
    svx_crash_buf_append_num(self, value < 0 ? (uintmax_t)(-value) : (uintmax_t)value, 
                             value < 0 ? 1 : 0, base, len);
}

static void svx_crash_buf_append_ptr(svx_crash_buf_t *self, uintptr_t value)
{
    svx_crash_buf_append_num(self, (uintmax_t)value, 0, 16, sizeof(uintptr_t) * 2);
}

/* the end of svx_crash_buf_t */
/**********************************************************************/

/**********************************************************************/
/* arch dependent code */

#if defined(__linux__) && defined(__x86_64__)

static uintptr_t svx_crash_get_pc(ucontext_t *uc)
{
    return (uintptr_t)(uc->uc_mcontext.gregs[REG_RIP]);
}

static void svx_crash_append_registers(svx_crash_buf_t *b, ucontext_t *uc)
{
    svx_crash_buf_append_str(b, "RAX: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_RAX]));
    svx_crash_buf_append_str(b, "  RBX: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_RBX]));
    svx_crash_buf_append_str(b, "\n");
    svx_crash_buf_append_str(b, "RCX: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_RCX]));
    svx_crash_buf_append_str(b, "  RDX: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_RDX]));
    svx_crash_buf_append_str(b, "\n");
    svx_crash_buf_append_str(b, "RDI: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_RDI]));
    svx_crash_buf_append_str(b, "  RSI: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_RSI]));
    svx_crash_buf_append_str(b, "\n");
    svx_crash_buf_append_str(b, "RBP: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_RBP]));
    svx_crash_buf_append_str(b, "  RSP: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_RSP]));
    svx_crash_buf_append_str(b, "\n");
    svx_crash_buf_append_str(b, "R8 : ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_R8]));
    svx_crash_buf_append_str(b, "  R9 : ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_R9]));
    svx_crash_buf_append_str(b, "\n");
    svx_crash_buf_append_str(b, "R10: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_R10]));
    svx_crash_buf_append_str(b, "  R11: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_R11]));
    svx_crash_buf_append_str(b, "\n");
    svx_crash_buf_append_str(b, "R12: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_R12]));
    svx_crash_buf_append_str(b, "  R13: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_R13]));
    svx_crash_buf_append_str(b, "\n");
    svx_crash_buf_append_str(b, "R14: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_R14]));
    svx_crash_buf_append_str(b, "  R15: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_R15]));
    svx_crash_buf_append_str(b, "\n");
    svx_crash_buf_append_str(b, "RIP: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_RIP]));
    svx_crash_buf_append_str(b, "  EFL: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_EFL]));
    svx_crash_buf_append_str(b, "\n");
    svx_crash_buf_append_str(b, "CSGSFS: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_CSGSFS]));
    svx_crash_buf_append_str(b, "\n");
}

#elif defined(__linux__) && defined(__i386__)

static uintptr_t svx_crash_get_pc(ucontext_t *uc)
{
    return (uintptr_t)(uc->uc_mcontext.gregs[REG_EIP]);
}

static void svx_crash_append_registers(svx_crash_buf_t *b, ucontext_t *uc)
{
    svx_crash_buf_append_str(b, "EAX: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_EAX]));
    svx_crash_buf_append_str(b, "  EBX: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_EBX]));
    svx_crash_buf_append_str(b, "  ECX: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_ECX]));
    svx_crash_buf_append_str(b, "  EDX: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_EDX]));
    svx_crash_buf_append_str(b, "\n");
    svx_crash_buf_append_str(b, "EDI: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_EDI]));
    svx_crash_buf_append_str(b, "  ESI: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_ESI]));
    svx_crash_buf_append_str(b, "  EBP: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_EBP]));
    svx_crash_buf_append_str(b, "  ESP: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_ESP]));
    svx_crash_buf_append_str(b, "\n");
    svx_crash_buf_append_str(b, "SS : ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_SS]));
    svx_crash_buf_append_str(b, "  EFL: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_EFL]));
    svx_crash_buf_append_str(b, "  EIP: ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_EIP]));
    svx_crash_buf_append_str(b, "  CS : ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_CS]));
    svx_crash_buf_append_str(b, "\n");
    svx_crash_buf_append_str(b, "DS : ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_DS]));
    svx_crash_buf_append_str(b, "  ES : ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_ES]));
    svx_crash_buf_append_str(b, "  FS : ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_FS]));
    svx_crash_buf_append_str(b, "  GS : ");
    svx_crash_buf_append_ptr(b, (uintptr_t)(uc->uc_mcontext.gregs[REG_GS]));
    svx_crash_buf_append_str(b, "\n");
}

#else

static uintptr_t svx_crash_get_pc(ucontext_t *uc)
{
    return 0;
}

static void svx_crash_append_registers(svx_crash_buf_t *b, ucontext_t *uc)
{
    svx_crash_buf_append_str(b, SVX_CRASH_OS_ARCH_NOT_SPT);
}

#endif

/* the end of arch dependent code */
/**********************************************************************/

static ssize_t svx_crash_write_buf(int fd, const char *buf, size_t len)
{
    size_t      nleft;
    ssize_t     nwritten;
    const char *ptr;

    ptr   = buf;
    nleft = len;

    while(nleft > 0)
    {
        if((nwritten = write(fd, ptr, nleft)) <= 0)
        {
            if(nwritten < 0 && errno == EINTR)
                nwritten = 0; /* call write() again */
            else
                return -1;    /* error */
        }

        nleft -= nwritten;
        ptr   += nwritten;
    }

    return len;
}

static ssize_t svx_crash_write_str(int fd, const char *str)
{
    const char *tmp = str;
    size_t      len = 0;

    while(*tmp) tmp++;
    len = tmp - str;
    if(0 == len) return 0;

    return svx_crash_write_buf(fd, str, len);
}

static void svx_crash_write_sysinfo(int fd, const char *path)
{
    int     fd2 = -1;
    char    buf[256] = "\0";
    ssize_t n = -1;
    
    if((fd2 = open(path, O_RDONLY)) < 0) return;
    while(1)
    {
        do n = read(fd2, buf, sizeof(buf));
        while(-1 == n && EINTR == errno);

        if(n <= 0) break;

        svx_crash_write_buf(fd, buf, n);
    }
    close(fd2);
}

static void svx_crash_write_baseinfo(int fd, ucontext_t *uc, int sig, siginfo_t *info,
                                     struct tm *tm, struct timespec *tp)
{
    char            buf[1024] = "\0";
    svx_crash_buf_t b         = SVX_CRASH_BUF_INITIALIZER;
    pid_t           pid       = getpid();
    pid_t           tid       = syscall(SYS_gettid);

    SVX_UTIL_UNUSED(uc);
    
    svx_crash_buf_init(&b, buf, sizeof(buf));

    /* write system version */
    svx_crash_write_sysinfo(fd, "/proc/version");

    /* append time */
    svx_crash_buf_append_str (&b, "Time: ");
    svx_crash_buf_append_uint(&b, tm->tm_year + 1900, 10, 4);
    svx_crash_buf_append_str (&b, "-");
    svx_crash_buf_append_uint(&b, tm->tm_mon + 1, 10, 2);
    svx_crash_buf_append_str (&b, "-");
    svx_crash_buf_append_uint(&b, tm->tm_mday, 10, 2);
    svx_crash_buf_append_str (&b, " ");
    svx_crash_buf_append_uint(&b, tm->tm_hour, 10, 2);
    svx_crash_buf_append_str (&b, ":");
    svx_crash_buf_append_uint(&b, tm->tm_min, 10, 2);
    svx_crash_buf_append_str (&b, ":");
    svx_crash_buf_append_uint(&b, tm->tm_sec, 10, 2);
    svx_crash_buf_append_str (&b, ".");
    svx_crash_buf_append_uint(&b, tp->tv_nsec / 1000, 10, 6);
    svx_crash_buf_append_str (&b, " ");
    svx_crash_buf_append_str (&b, svx_crash_timezone);

    /* append hostname */
    svx_crash_buf_append_str (&b, ", Hostname: ");
    svx_crash_buf_append_str (&b, svx_crash_hostname);

    /* append PID */
    svx_crash_buf_append_str (&b, "\nPID: ");
    svx_crash_buf_append_uint(&b, pid, 10, 0);
    svx_crash_buf_append_str (&b, ", Pname: ");
    svx_crash_write_buf(fd, svx_crash_buf_get_buf(&b), svx_crash_buf_get_buflen(&b));
    svx_crash_buf_reset(&b);
    
    /* append Pname */
    svx_crash_buf_append_str (&b, "/proc/");
    svx_crash_buf_append_uint(&b, pid, 10, 0);
    svx_crash_buf_append_str (&b, "/comm");
    svx_crash_write_sysinfo(fd, svx_crash_buf_get_buf(&b));
    svx_crash_buf_reset(&b);
    
    /* append TID */
    svx_crash_buf_append_str (&b, "TID: ");
    svx_crash_buf_append_uint(&b, tid, 10, 0);
    svx_crash_buf_append_str (&b, ", Tname: ");
    svx_crash_write_buf(fd, svx_crash_buf_get_buf(&b), svx_crash_buf_get_buflen(&b));
    svx_crash_buf_reset(&b);

    /* append Tname */
    svx_crash_buf_append_str (&b, "/proc/");
    svx_crash_buf_append_uint(&b, pid, 10, 0);
    svx_crash_buf_append_str (&b, "/task/");
    svx_crash_buf_append_uint(&b, tid, 10, 0);
    svx_crash_buf_append_str (&b, "/comm");
    svx_crash_write_sysinfo(fd, svx_crash_buf_get_buf(&b));
    svx_crash_buf_reset(&b);

    /* append signal & code */
#define SVX_CRASH_CASE_SIGNAL(signal)                                   \
    case signal:                                                        \
        svx_crash_buf_append_str(&b, " ("#signal"), Code: ");           \
        svx_crash_buf_append_int(&b, (intmax_t)(info->si_code), 10, 0)
#define SVX_CRASH_CASE_CODE(code)                                       \
    case code:                                                          \
        svx_crash_buf_append_str(&b, " ("#code")");                     \
        break
#define SVX_CRASH_CASE_CODE_DEFAULT                                     \
    SVX_CRASH_CASE_CODE(SI_USER);                                       \
    SVX_CRASH_CASE_CODE(SI_KERNEL);                                     \
    SVX_CRASH_CASE_CODE(SI_QUEUE);                                      \
    SVX_CRASH_CASE_CODE(SI_TIMER);                                      \
    SVX_CRASH_CASE_CODE(SI_MESGQ);                                      \
    SVX_CRASH_CASE_CODE(SI_ASYNCIO);                                    \
    SVX_CRASH_CASE_CODE(SI_SIGIO);                                      \
    SVX_CRASH_CASE_CODE(SI_TKILL);                                      \
    default:                                                            \
        svx_crash_buf_append_str(&b, " (UNKNOWN_CODE)");                \
        break

    svx_crash_buf_append_str(&b, "Signal: ");
    svx_crash_buf_append_int(&b, (intmax_t)sig, 10, 0);
    switch(sig)
    {
        /* SIGSEGV */
        SVX_CRASH_CASE_SIGNAL(SIGSEGV);
        switch(info->si_code)
        {
            SVX_CRASH_CASE_CODE(SEGV_MAPERR);
            SVX_CRASH_CASE_CODE(SEGV_ACCERR);
            SVX_CRASH_CASE_CODE_DEFAULT;
        }
        break;

        /* SIGFPE */
        SVX_CRASH_CASE_SIGNAL(SIGFPE);
        switch(info->si_code)
        {
            SVX_CRASH_CASE_CODE(FPE_INTDIV);
            SVX_CRASH_CASE_CODE(FPE_INTOVF);
            SVX_CRASH_CASE_CODE(FPE_FLTDIV);
            SVX_CRASH_CASE_CODE(FPE_FLTOVF);
            SVX_CRASH_CASE_CODE(FPE_FLTUND);
            SVX_CRASH_CASE_CODE(FPE_FLTRES);
            SVX_CRASH_CASE_CODE(FPE_FLTINV);
            SVX_CRASH_CASE_CODE(FPE_FLTSUB);
            SVX_CRASH_CASE_CODE_DEFAULT;
        }
        break;

        /* SIGILL */
        SVX_CRASH_CASE_SIGNAL(SIGILL);
        switch(info->si_code)
        {
            SVX_CRASH_CASE_CODE(ILL_ILLOPC);
            SVX_CRASH_CASE_CODE(ILL_ILLOPN);
            SVX_CRASH_CASE_CODE(ILL_ILLADR);
            SVX_CRASH_CASE_CODE(ILL_ILLTRP);
            SVX_CRASH_CASE_CODE(ILL_PRVOPC);
            SVX_CRASH_CASE_CODE(ILL_PRVREG);
            SVX_CRASH_CASE_CODE(ILL_COPROC);
            SVX_CRASH_CASE_CODE(ILL_BADSTK);
            SVX_CRASH_CASE_CODE_DEFAULT;
        }
        break;

        /* SIGBUS */
        SVX_CRASH_CASE_SIGNAL(SIGBUS);
        switch(info->si_code)
        {
            SVX_CRASH_CASE_CODE(BUS_ADRALN);
            SVX_CRASH_CASE_CODE(BUS_ADRERR);
            SVX_CRASH_CASE_CODE(BUS_OBJERR);
#ifdef BUS_MCEERR_AR
            SVX_CRASH_CASE_CODE(BUS_MCEERR_AR);
#endif
#ifdef BUS_MCEERR_AO
            SVX_CRASH_CASE_CODE(BUS_MCEERR_AO);
#endif
            SVX_CRASH_CASE_CODE_DEFAULT;
        }
        break;

        /* SIGABRT */
        SVX_CRASH_CASE_SIGNAL(SIGABRT);
        switch(info->si_code)
        {
            SVX_CRASH_CASE_CODE_DEFAULT;
        }
        break;

#ifdef SIGSTKFLT
        /* SIGSTKFLT */
        SVX_CRASH_CASE_SIGNAL(SIGSTKFLT);
        switch(info->si_code)
        {
            SVX_CRASH_CASE_CODE_DEFAULT;
        }
        break;
#endif

    default:
        svx_crash_buf_append_str(&b, " (UNKNOWN_SIGNAL)");
        break;
    }

#undef SVX_CRASH_CASE_SIGNAL
#undef SVX_CRASH_CASE_CODE
#undef SVX_CRASH_CASE_CODE_DEFAULT

    /* append fault addr */
    svx_crash_buf_append_str(&b, ", Fault addr: ");
    svx_crash_buf_append_ptr(&b, (uintptr_t)(info->si_addr));
    svx_crash_buf_append_str(&b, "\n");

    svx_crash_write_buf(fd, svx_crash_buf_get_buf(&b), svx_crash_buf_get_buflen(&b));
}

static void svx_crash_write_registers(int fd, ucontext_t *uc)
{
    char            buf[1024] = "\0";
    svx_crash_buf_t b         = SVX_CRASH_BUF_INITIALIZER;
    
    svx_crash_buf_init(&b, buf, sizeof(buf));

    svx_crash_append_registers(&b, uc);

    svx_crash_write_buf(fd, svx_crash_buf_get_buf(&b), svx_crash_buf_get_buflen(&b));
}

static void svx_crash_write_backtrace(int fd, ucontext_t *uc)
{
    int        i          = 0;
    uintptr_t  pc         = svx_crash_get_pc(uc);
    void      *trace[256];
    int        trace_size = 0;

    trace_size = backtrace(trace, sizeof(trace) / sizeof(void *));
    if(0 != pc)
    {
        for(i = 0; i < trace_size; i++)
        {
            /* Allow a few bytes difference to cope with as many arches as possible */
            if((uintptr_t)trace[i] >= pc - 16 && (uintptr_t)trace[i] <= pc + 16)
                break;
        }
        if(i == trace_size) i = 0; /* if we haven't found it, dump full backtrace */
    }
    backtrace_symbols_fd(trace + i, trace_size - i, fd);
}

static void svx_crash_sigaction(int sig, siginfo_t *info, void *secret)
{
    ucontext_t      *uc = (ucontext_t *)secret;
    struct timespec  tp;
    struct tm        tm;
    int              fd = -1;
    char             buf[PATH_MAX] = "\0";
    svx_crash_buf_t  b = SVX_CRASH_BUF_INITIALIZER;

    /* restore the old sigactions */
    svx_crash_uregister_signal_handler();
        
    /* crrent time */
    memset(&tm, 0, sizeof(tm));
    clock_gettime(CLOCK_REALTIME, &tp);
    svx_crash_time2tm(&(tp.tv_sec), svx_crash_timezone_off, &tm);

    /* create and open dump file */
    svx_crash_buf_init(&b, buf, sizeof(buf));
    svx_crash_buf_append_str (&b, svx_crash_dirname);
    svx_crash_buf_append_str (&b, svx_crash_prefix);
    svx_crash_buf_append_str (&b, ".");
    svx_crash_buf_append_uint(&b, tm.tm_year + 1900, 10, 4);
    svx_crash_buf_append_uint(&b, tm.tm_mon + 1, 10, 2);
    svx_crash_buf_append_uint(&b, tm.tm_mday, 10, 2);
    svx_crash_buf_append_str (&b, ".");
    svx_crash_buf_append_uint(&b, tm.tm_hour, 10, 2);
    svx_crash_buf_append_uint(&b, tm.tm_min, 10, 2);
    svx_crash_buf_append_uint(&b, tm.tm_sec, 10, 2);
    svx_crash_buf_append_str (&b, ".");
    svx_crash_buf_append_uint(&b, tp.tv_nsec / 1000, 10, 6);
    svx_crash_buf_append_str (&b, ".");
    svx_crash_buf_append_str (&b, svx_crash_timezone);
    svx_crash_buf_append_str (&b, ".");
    svx_crash_buf_append_str (&b, svx_crash_hostname);
    svx_crash_buf_append_str (&b, ".");
    svx_crash_buf_append_uint(&b, getpid(), 10, 0);
    svx_crash_buf_append_str (&b, ".");
    svx_crash_buf_append_str (&b, svx_crash_suffix);
    if((fd = open(svx_crash_buf_get_buf(&b), O_CREAT | O_TRUNC | O_WRONLY, 0644)) < 0) goto end;
    svx_crash_buf_reset(&b);

    /* write head message */
    if(svx_crash_head_msg[0]) svx_crash_write_str(fd, svx_crash_head_msg);

    /* write delimiter */
    svx_crash_write_str(fd, "\n"SVX_CRASH_DELIMITER"\n");

    /* write baseinfo */
    svx_crash_write_baseinfo(fd, uc, sig, info, &tm, &tp);

    /* write registers */
    svx_crash_write_str(fd, "\n*** Registers:\n");
    svx_crash_write_registers(fd, uc);

    /* write backtrace */
    svx_crash_write_str(fd, "\n*** Backtrace:\n");
    svx_crash_write_backtrace(fd, uc);
    
    /* write memory map */
    svx_crash_write_str(fd, "\n*** Memory map:\n");
    svx_crash_write_sysinfo(fd, "/proc/self/maps");
    
    /* write delimiter */
    svx_crash_write_str(fd, "\n"SVX_CRASH_DELIMITER"\n");
    
 end:
    if(svx_crash_cb) svx_crash_cb(fd, svx_crash_cb_arg);
    if(fd >= 0) close(fd);
    
    /* resend the signal to self, let the old sigaction to handle it again */
    raise(sig);
}

static int svx_crash_file_filter(const struct dirent *entry)
{
    switch(fnmatch(svx_crash_pattern, entry->d_name, 0))
    {
    case 0           : return 1;
    case FNM_NOMATCH : return 0;
    default          : return 0;
    }
}

static void svx_crash_remove_older_dumps()
{
    struct dirent **entry;
    struct stat     st;
    char            pathname[PATH_MAX];
    int             remove_older_files = 0;
    int             n = 0;
    size_t          i = 0;

    if(0 == svx_crash_max_dumps) return; /* keep all crach dump files */
    
    if((n = scandir(svx_crash_dirname, &entry, svx_crash_file_filter, alphasort)) >= 0)
    {
        while(n--)
        {
            if(NULL == entry[n] || NULL == entry[n]->d_name) continue;
            snprintf(pathname, sizeof(pathname), "%s%s", svx_crash_dirname, entry[n]->d_name);
            free(entry[n]);
            if(0 != lstat(pathname, &st)) continue;
            if(!S_ISREG(st.st_mode)) continue;

            if(!remove_older_files)
            {
                if(++i >= svx_crash_max_dumps)
                    remove_older_files = 1;
            }
            if(remove_older_files)
                remove(pathname);
        }
        free(entry);
    }
}

int svx_crash_set_callback(svx_crash_callback_t cb, void *arg)
{
    if(NULL == cb) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "cb:%p\n", cb);

    svx_crash_cb     = cb;
    svx_crash_cb_arg = arg;

    return 0;
}

int svx_crash_set_head_msg(const char *msg)
{
    if(NULL == msg) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "msg:%p\n", msg);
    
    strncpy(svx_crash_head_msg, msg, sizeof(svx_crash_head_msg));
    svx_crash_head_msg[sizeof(svx_crash_head_msg) - 1] = '\0';

    return 0;
}

int svx_crash_set_timezone_mode(svx_crash_timezone_mode_t mode)
{
    struct timeval tv;
    struct tm      tm;

    svx_crash_timezone_mode = mode;

    if(SVX_CRASH_TIMEZONE_MODE_GMT == mode)
    {
        svx_crash_timezone_off = 0;

        strncpy(svx_crash_timezone, "+0000", sizeof(svx_crash_timezone));
        svx_crash_timezone[sizeof(svx_crash_timezone) - 1] = '\0';
    }
    else
    {
        if(0 != gettimeofday(&tv, NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
        if(NULL == localtime_r((time_t*)(&(tv.tv_sec)), &tm)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

        svx_crash_timezone_off = tm.tm_gmtoff;
        
        snprintf(svx_crash_timezone, sizeof(svx_crash_timezone), "%c%02ld%02ld",
                 svx_crash_timezone_off < 0 ? '-' : '+', 
                 labs(svx_crash_timezone_off / 3600), labs(svx_crash_timezone_off % 3600));
    }

    return 0;
}

int svx_crash_set_dirname(const char *dirname)
{
    int r = 0;

    if(NULL == dirname || '\0' == dirname[0] || '/' != dirname[strlen(dirname) - 1])
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "dirname:%s\n", dirname);
    
    if(0 != (r = svx_util_get_absolute_path(dirname, svx_crash_dirname, sizeof(svx_crash_dirname))))
        SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    
    return 0;
}

int svx_crash_set_prefix(const char *prefix)
{
    if(NULL == prefix || '\0' == prefix[0] || strchr(prefix, '/'))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "prefix:%s\n", prefix);
    
    strncpy(svx_crash_prefix, prefix, sizeof(svx_crash_prefix));
    svx_crash_prefix[sizeof(svx_crash_prefix) - 1] = '\0';

    return 0;
}

int svx_crash_set_suffix(const char *suffix)
{
    if(NULL == suffix || '\0' == suffix[0] || strchr(suffix, '/'))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "suffix:%s\n", suffix);
    
    strncpy(svx_crash_suffix, suffix, sizeof(svx_crash_suffix));
    svx_crash_suffix[sizeof(svx_crash_suffix) - 1] = '\0';

    return 0;
}

int svx_crash_set_max_dumps(size_t max)
{
    svx_crash_max_dumps = max;
    
    return 0;
}

int svx_crash_register_signal_handler()
{
    struct sigaction act;
    int              r;
    stack_t          ss;

    /* save hostname */
    if(0 != gethostname(svx_crash_hostname, sizeof(svx_crash_hostname)))
        SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

    /* save absolute dirname*/
    if('\0' == svx_crash_dirname[0])
        if(0 != (r = svx_util_get_absolute_path(SVX_CRASH_DEFAULT_DIRNAME, svx_crash_dirname, sizeof(svx_crash_dirname))))
            SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    /* save prefix */
    if('\0' == svx_crash_prefix[0])
        if(0 != (r = svx_util_get_exe_basename(svx_crash_prefix, sizeof(svx_crash_prefix), NULL)))
            SVX_LOG_ERRNO_RETURN_ERR(r, NULL);

    /* save suffix */
    if('\0' == svx_crash_suffix[0])
    {
        strncpy(svx_crash_suffix, SVX_CRASH_DEFAULT_SUFFIX, sizeof(svx_crash_suffix));
        svx_crash_suffix[sizeof(svx_crash_suffix) - 1] = '\0';
    }

    /* save pattern */
    snprintf(svx_crash_pattern, sizeof(svx_crash_pattern), "%s.*.%s", svx_crash_prefix, svx_crash_suffix);

    /* remove older crash dump files */
    svx_crash_remove_older_dumps();

    /* setup crash stack */
    ss.ss_sp    = svx_crash_stack;
    ss.ss_size  = sizeof(svx_crash_stack);
    ss.ss_flags = 0;
    if(0 != sigaltstack(&ss, NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);

    /* register new sigaction & save old sigactions */
    if(0 != sigemptyset(&act.sa_mask)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    act.sa_flags = SA_SIGINFO | SA_ONSTACK;
    act.sa_sigaction = svx_crash_sigaction;
    if(0 != sigaction(SIGSEGV,   &act, &svx_crash_old_sigact_segv)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(0 != sigaction(SIGFPE,    &act, &svx_crash_old_sigact_fpe)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(0 != sigaction(SIGILL,    &act, &svx_crash_old_sigact_ill)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(0 != sigaction(SIGBUS,    &act, &svx_crash_old_sigact_bus)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(0 != sigaction(SIGABRT,   &act, &svx_crash_old_sigact_abrt)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
#ifdef SIGSTKFLT
    if(0 != sigaction(SIGSTKFLT, &act, &svx_crash_old_sigact_stkflt)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
#endif

    return 0;
}

int svx_crash_uregister_signal_handler()
{
    /* restore the old sigactions */
    if(0 != sigaction(SIGSEGV,   &svx_crash_old_sigact_segv,   NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(0 != sigaction(SIGFPE,    &svx_crash_old_sigact_fpe,    NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(0 != sigaction(SIGILL,    &svx_crash_old_sigact_ill,    NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(0 != sigaction(SIGBUS,    &svx_crash_old_sigact_bus,    NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
    if(0 != sigaction(SIGABRT,   &svx_crash_old_sigact_abrt,   NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
#ifdef SIGSTKFLT
    if(0 != sigaction(SIGSTKFLT, &svx_crash_old_sigact_stkflt, NULL)) SVX_LOG_ERRNO_RETURN_ERR(errno, NULL);
#endif

    return 0;
}
