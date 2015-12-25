/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "svx_circlebuf.h"
#include "svx_errno.h"
#include "svx_log.h"

#define SVX_CIRCLEBUF_DEBUG_FLAG 0

#if SVX_CIRCLEBUF_DEBUG_FLAG
#define SVX_CIRCLEBUF_DEBUG(msg) do {                   \
        size_t i_ = 0;                                  \
        printf("-----------------------\n"              \
               "func:     %s\n"                         \
               "line:     %d\n"                         \
               "msg:      %s\n"                         \
               "-----------------------\n"              \
               "buf:      %p\n"                         \
               "size:     %zu\n"                        \
               "used:     %zu\n"                        \
               "max:      %zu\n"                        \
               "min:      %zu\n"                        \
               "step:     %zu\n"                        \
               "offset_r: %zu\n"                        \
               "offset_w: %zu\n"                        \
               "-----------------------\n",             \
               __FUNCTION__, __LINE__, msg, self->buf,    \
               self->size, self->used, self->max, self->min,    \
               self->step, self->offset_r, self->offset_w);   \
        for(i_ = 0; i_ < self->size; i_++)                \
        {                                               \
            printf("%02X ", self->buf[i_]);               \
            if(15 == i_ % 16) printf("\n");             \
        }                                               \
        if(0 != i_ % 16) printf("\n");                  \
        printf("-----------------------\n");            \
    } while(0)
#else
#define SVX_CIRCLEBUF_DEBUG(msg)
#endif

struct svx_circlebuf
{
    uint8_t *buf;      /* buf */
    size_t   size;     /* current buf size */
    size_t   used;     /* current used buf size */
    size_t   max;      /* max buf size (zero means infinite size) */
    size_t   min;      /* min buf size */
    size_t   step;     /* min step for expand and shrink */
    size_t   offset_r; /* read index */
    size_t   offset_w; /* write index */
};

int svx_circlebuf_create(svx_circlebuf_t **self, size_t max_len, size_t min_len, size_t min_step)
{
    if(NULL == self || 0 == min_len || 0 == min_step || (0 != max_len && (min_len > max_len || min_step > max_len)))
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, max_len:%zu, min_len:%zu, min_step:%zu\n",
                                 self, max_len, min_len, min_step);
    
    /* align to 64 bits */
    if(0 != max_len  % 8) max_len  += (8 - max_len  % 8);
    if(0 != min_len  % 8) min_len  += (8 - min_len  % 8);
    if(0 != min_step % 8) min_step += (8 - min_step % 8);

    if(NULL == (*self = malloc(sizeof(svx_circlebuf_t)))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    if(NULL == ((*self)->buf = malloc(min_len))) /* create buffer use min length */
    {
        free(*self);
        *self = NULL;
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
    }

    (*self)->size     = min_len;
    (*self)->used     = 0;
    (*self)->max      = max_len;
    (*self)->min      = min_len;
    (*self)->step     = min_step;
    (*self)->offset_r = 0;
    (*self)->offset_w = 0;

    return 0;
}

int svx_circlebuf_destroy(svx_circlebuf_t **self)
{
    if(NULL == self)  SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    if(NULL == *self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "*self:%p\n", *self);

    if((*self)->buf) free((*self)->buf);
    free(*self);
    *self = NULL;

    return 0;
}

int svx_circlebuf_get_buf_len(svx_circlebuf_t *self, size_t *len)
{
    if(NULL == self || NULL == len) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, len:%p\n", self, len);

    *len = self->size;

    return 0;
}

int svx_circlebuf_get_data_len(svx_circlebuf_t *self, size_t *len)
{
    if(NULL == self || NULL == len) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, len:%p\n", self, len);
    
    *len = self->used;

    return 0;
}

int svx_circlebuf_get_freespace_len(svx_circlebuf_t *self, size_t *len)
{
    if(NULL == self || NULL == len) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, len:%p\n", self, len);

    *len = self->size - self->used;

    return 0;
}

int svx_circlebuf_expand(svx_circlebuf_t *self, size_t freespace_need)
{
    uint8_t *new_buf  = NULL;
    size_t   new_size = 0;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);
    
    if(self->size - self->used >= freespace_need) return 0;

    SVX_CIRCLEBUF_DEBUG("before");

    /* check max limit */
    if(self->max > 0 && self->used + freespace_need > self->max)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_REACH, "self->max:%zu, self->used:%zu, freespace_need:%zu\n", 
                                 self->max, self->used, freespace_need);

    new_size = self->used + freespace_need;
    if(new_size - self->size < self->step) new_size = self->size + self->step;
    if(0 != new_size % 8) new_size += (8 - new_size % 8);
    if(self->max > 0 && new_size > self->max) new_size = self->max;

    if(NULL == (new_buf = realloc(self->buf, new_size))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);

    /* move data if necessary */
    if(self->offset_w < self->offset_r || (self->offset_w == self->offset_r && self->used > 0))
    {
        /*                    r
              w  r            w
           xxx---xxx  OR  xxxxxxxxx
           012345678      012345678 */
        memmove(new_buf + (new_size - (self->size - self->offset_r)), new_buf + self->offset_r, self->size - self->offset_r);
        self->offset_r += (new_size - self->size);
    }
    
    self->buf  = new_buf;
    self->size = new_size;

    SVX_CIRCLEBUF_DEBUG("after");
    return 0;
}

int svx_circlebuf_shrink(svx_circlebuf_t *self, size_t freespace_keep)
{
    uint8_t *new_buf  = NULL;
    size_t   new_size = 0;

    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    if(self->size - self->used <= freespace_keep) return 0;

    /* check min limit */
    if(self->used + freespace_keep < self->min)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_REACH, "self->min:%zu, self->used:%zu, freespace_keep:%zu\n", 
                                 self->min, self->used, freespace_keep);

    new_size = self->used + freespace_keep;
    if(self->size - new_size < self->step) return 0; /* to avoid too small shrinking step */
    if(0 != new_size % 8) new_size += (8 - new_size % 8);
    if(self->max > 0 && new_size > self->max) new_size = self->max;
    if(new_size >= self->size) return 0; /* new_size must smaller than the current size */

    SVX_CIRCLEBUF_DEBUG("before");

    /* move data if necessary */
    if(self->offset_r < self->offset_w)
    {
        /*    r  w
           ---xxx---
           012345678 */
        if(new_size <= self->offset_r)
        {
            memcpy(self->buf, self->buf + self->offset_r, self->used);
            self->offset_r = 0;
            self->offset_w = self->used;
        }
        else if(new_size > self->offset_r && new_size < self->offset_w)
        {
            memcpy(self->buf, self->buf + new_size, self->offset_w - new_size);
            self->offset_w = self->offset_w - new_size;
        }
        else if(new_size == self->offset_w)
        {
            self->offset_w = 0;
        }
    }
    else if(self->offset_w < self->offset_r)
    {
        /*    w  r
           xxx---xxx
           012345678 */
        memmove(self->buf + self->offset_r - (self->size - new_size), self->buf + self->offset_r, self->size - self->offset_r);
        self->offset_r -= self->size - new_size;
    }
    else /* self->offset_w == self->offset_r */
    {
        if(0 == self->used)
        {
            /*     r
                   w
               ---------
               012345678 */
            self->offset_r = 0;
            self->offset_w = 0;
        }
        else
        {
            /*     r
                   w
               xxxxxxxxx
               012345678 */

            /* impossible */
            SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_UNKNOWN, NULL);
        }
    }

    if(NULL == (new_buf = realloc(self->buf, new_size))) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_UNKNOWN, NULL);
    self->buf  = new_buf;
    self->size = new_size;

    SVX_CIRCLEBUF_DEBUG("after");
    return 0;
}

int svx_circlebuf_erase_all_data(svx_circlebuf_t *self)
{
    if(NULL == self) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p\n", self);

    self->used     = 0;
    self->offset_r = 0;
    self->offset_w = 0;

    return 0;
}

int svx_circlebuf_erase_data(svx_circlebuf_t *self, size_t len)
{
    if(NULL == self || 0 == len) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, len:%zu\n", self, len);
    if(len > self->used) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_RANGE, "len:%zu, self->used:%zu\n", len, self->used);

    SVX_CIRCLEBUF_DEBUG("before");
    
    self->used     -= len;
    self->offset_r += len;
    self->offset_r %= self->size;
    
    SVX_CIRCLEBUF_DEBUG("after");
    return 0;
}


int svx_circlebuf_commit_data(svx_circlebuf_t *self, size_t len)
{
    if(NULL == self || 0 == len) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, len:%zu\n", self, len);
    if(len > self->size - self->used) SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_RANGE, "len:%zu, self->size:%zu, self->used:%zu\n", len, self->size, self->used);

    SVX_CIRCLEBUF_DEBUG("before");

    self->used     += len;
    self->offset_w += len;
    self->offset_w %= self->size;
    
    SVX_CIRCLEBUF_DEBUG("after");
    return 0;
}

int svx_circlebuf_append_data(svx_circlebuf_t *self, const uint8_t *buf, size_t buf_len)
{
    int r = 0;

    if(NULL == self || NULL == buf || 0 == buf_len)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, buf:%p, buf_len:%zu\n", self, buf, buf_len);

    SVX_CIRCLEBUF_DEBUG("before");
 
    if(self->size - self->used < buf_len)
    {
        if(0 != (r = svx_circlebuf_expand(self, buf_len))) SVX_LOG_ERRNO_RETURN_ERR(r, NULL);
    }

    if(self->offset_r < self->offset_w)
    {
        /*    r  w
           ---xxx---
           012345678 */
        if(self->size - self->offset_w >= buf_len)
        {
            memcpy(self->buf + self->offset_w, buf, buf_len);
        }
        else
        {
            memcpy(self->buf + self->offset_w, buf, self->size - self->offset_w);
            memcpy(self->buf, buf + (self->size - self->offset_w), buf_len - (self->size - self->offset_w));
        }
    }
    else if(self->offset_w < self->offset_r)
    {
        /*    w  r
           xxx---xxx
           012345678 */
        memcpy(self->buf + self->offset_w, buf, buf_len);
    }
    else /* self->offset_w == self->offset_r */
    {
        if(0 == self->used)
        {
            /*     r
                   w
               ---------
               012345678 */
            self->offset_r = 0;
            self->offset_w = 0;
            memcpy(self->buf, buf, buf_len);
        }
        else
        {
            /*     r
                   w
               xxxxxxxxx
               012345678 */

            /* impossible */
            SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NOMEM, NULL);
        }
    }

    self->used     += buf_len;
    self->offset_w += buf_len;
    self->offset_w %= self->size;

    SVX_CIRCLEBUF_DEBUG("after");
    return 0;
}

int svx_circlebuf_get_data_ptr(svx_circlebuf_t *self, uint8_t **buf1, size_t *buf1_len, 
                               uint8_t **buf2, size_t *buf2_len)
{
    if(NULL == self || NULL == buf1 || NULL == buf1_len || NULL == buf2 || NULL == buf2_len)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, buf1:%p, buf1_len:%p, buf2:%p, buf2_len:%p\n",
                                 self, buf1, buf1_len, buf2, buf2_len);

    if(self->offset_r < self->offset_w)
    {
        /*    r  w
           ---xxx---
           012345678 */
        *buf1     = self->buf + self->offset_r;
        *buf1_len = self->used;
        *buf2     = NULL;
        *buf2_len = 0;
    }
    else if(self->offset_w < self->offset_r)
    {
        /*    w  r
           xxx---xxx
           012345678 */
        *buf1     = self->buf + self->offset_r;
        *buf1_len = self->size - self->offset_r;
        if(self->offset_w > 0)
        {
            *buf2     = self->buf;
            *buf2_len = self->offset_w;
        }
        else
        {
            *buf2     = NULL;
            *buf2_len = 0;
        }
    }
    else /* self->offset_w == self->offset_r */
    {
        if(0 == self->used)
        {
            /*     r
                   w
               ---------
               012345678 */
            self->offset_r = 0;
            self->offset_w = 0;
            *buf1        = NULL;
            *buf1_len    = 0;
            *buf2        = NULL;
            *buf2_len    = 0;
        }
        else
        {
            /*     r
                   w
               xxxxxxxxx
               012345678 */
            *buf1     = self->buf + self->offset_r;
            *buf1_len = self->size - self->offset_r;
            if(self->offset_w > 0)
            {
                *buf2     = self->buf;
                *buf2_len = self->offset_w;
            }
            else
            {
                *buf2     = NULL;
                *buf2_len = 0;
            }
        }
    }

    return 0;
}

int svx_circlebuf_get_freespace_ptr(svx_circlebuf_t *self, uint8_t **buf1, size_t *buf1_len, 
                                    uint8_t **buf2, size_t *buf2_len)
{
    if(NULL == self || NULL == buf1 || NULL == buf1_len || NULL == buf2 || NULL == buf2_len)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, buf1:%p, buf1_len:%p, buf2:%p, buf2_len:%p\n",
                                 self, buf1, buf1_len, buf2, buf2_len);

    if(self->offset_r < self->offset_w)
    {
        /*    r  w
           ---xxx---
           012345678 */
        *buf1     = self->buf + self->offset_w;
        *buf1_len = self->size - self->offset_w;
        if(self->offset_r > 0)
        {
            *buf2     = self->buf;
            *buf2_len = self->offset_r;
        }
        else
        {
            *buf2     = NULL;
            *buf2_len = 0;
        }
    }
    else if(self->offset_w < self->offset_r)
    {
        /*    w  r
           xxx---xxx
           012345678 */
        *buf1     = self->buf + self->offset_w;
        *buf1_len = self->size - self->used;
        *buf2     = NULL;
        *buf2_len = 0;
    }
    else /* self->offset_w == self->offset_r */
    {
        if(0 == self->used)
        {
            /*     r
                   w
               ---------
               012345678 */
            self->offset_r = 0;
            self->offset_w = 0;
            *buf1        = self->buf;
            *buf1_len    = self->size;
            *buf2        = NULL;
            *buf2_len    = 0;
        }
        else
        {
            /*     r
                   w
               xxxxxxxxx
               012345678 */
            *buf1     = NULL;
            *buf1_len = 0;
            *buf2     = NULL;
            *buf2_len = 0;
        }
    }

    return 0;
}

int svx_circlebuf_get_data(svx_circlebuf_t *self, uint8_t *buf, size_t buf_len)
{
    if(NULL == self || NULL == buf || 0 == buf_len) 
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, buf:%p, buf_len:%zu\n", self, buf, buf_len);
    
    if(buf_len > self->used) return SVX_ERRNO_NODATA;

    SVX_CIRCLEBUF_DEBUG("before");

    if(self->offset_r < self->offset_w)
    {
        /*    r  w
           ---xxx---
           012345678 */
        memcpy(buf, self->buf + self->offset_r, buf_len);
        self->used     -= buf_len;
        self->offset_r += buf_len;
    }
    else if(self->offset_w < self->offset_r || (self->offset_w == self->offset_r && self->used > 0))
    {
        /*                    r
              w  r            w
           xxx---xxx  OR  xxxxxxxxx
           012345678      012345678 */
        if(buf_len <= self->size - self->offset_r)
        {
            memcpy(buf, self->buf + self->offset_r, buf_len);
        }
        else
        {
            memcpy(buf, self->buf + self->offset_r, self->size - self->offset_r);
            memcpy(buf + (self->size - self->offset_r), self->buf, buf_len - (self->size - self->offset_r));
        }
        self->used     -= buf_len;
        self->offset_r += buf_len;
        self->offset_r %= self->size;
    }
    else /* self->offset_w == self->offset_r && 0 == self->used */
    {
        /*     r
               w
           ---------
           012345678 */

        /* impossible */
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NODATA, NULL);
    }

    SVX_CIRCLEBUF_DEBUG("after");
    return 0;
}

int svx_circlebuf_get_data_by_ending(svx_circlebuf_t *self, const uint8_t *ending, size_t ending_len,
                                     uint8_t *buf, size_t buf_len, size_t *ret_len)
{
    uint8_t *p = NULL;
    size_t   offset_start = 0, offset_end = 0;
    size_t   search_len = 0, search_count = 0;
    size_t   i = 0, j = 0;

    if(NULL == self || NULL == ending || 0 == ending_len || NULL == buf || 0 == buf_len ||
       NULL == ret_len || ending_len > buf_len)
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_INVAL, "self:%p, ending:%p, ending_len:%zu, buf:%p, buf_len:%zu, "
                                 "ret_len:%p\n", self, ending, ending_len, buf, buf_len, ret_len);

    if(ending_len > self->used) return SVX_ERRNO_NODATA;

    SVX_CIRCLEBUF_DEBUG("before");
    
    if(self->offset_r < self->offset_w)
    {
        /*    r  w
           ---xxx---
           012345678 */
        if(NULL == (p = memmem(self->buf + self->offset_r, self->used, ending, ending_len))) return SVX_ERRNO_NOTFND;
        *ret_len = p + ending_len - (self->buf + self->offset_r);
        if(*ret_len > buf_len) return SVX_ERRNO_NOBUF;
        memcpy(buf, self->buf + self->offset_r, *ret_len);
        self->used     -= *ret_len;
        self->offset_r += *ret_len;

        SVX_CIRCLEBUF_DEBUG("after 1");
        return 0;
    }
    else if(self->offset_w < self->offset_r || (self->offset_w == self->offset_r && self->used > 0))
    {
        /*                    r
              w  r            w
           xxx---xxx  OR  xxxxxxxxx
           012345678      012345678 */

        /* try to find the ending from self->offset_r to self->size */
        if(self->size - self->offset_r >= ending_len)
        {
            if(NULL != (p = memmem(self->buf + self->offset_r, self->size - self->offset_r, ending, ending_len)))
            {
                *ret_len = p + ending_len - (self->buf + self->offset_r);
                if(*ret_len > buf_len) return SVX_ERRNO_NOBUF;
                memcpy(buf, self->buf + self->offset_r, *ret_len);
                self->used     -= *ret_len;
                self->offset_r += *ret_len;
                self->offset_r %= self->size;

                SVX_CIRCLEBUF_DEBUG("after 2");
                return 0;
            }
        }

        /* try to find the ending around the "turning point" */
        if(self->offset_w > 0 && ending_len > 1)
        {
            offset_start = self->size - ending_len + 1;
            if(offset_start < self->offset_r) offset_start = self->offset_r;
            offset_end = ending_len - 1;
            if(offset_end > self->offset_w) offset_end = self->offset_w;
            search_len = self->size - offset_start + offset_end;
            search_count = search_len - ending_len + 1;

            for(i = 0; i < search_count; i++)
            {
                for(j = offset_start + i; j < self->size; j++)
                    if(*(self->buf + j) != *(ending + (j - (offset_start + i))))
                        goto next_round;
                for(j = 0; j < ending_len - (self->size - (offset_start + i)); j++)
                    if(*(self->buf + j) != *(ending + (self->size - (offset_start + i)) + j))
                        goto next_round;

                *ret_len = (self->size - self->offset_r) + j;
                if(*ret_len > buf_len) return SVX_ERRNO_NOBUF;
                memcpy(buf, self->buf + self->offset_r, self->size - self->offset_r);
                memcpy(buf + (self->size - self->offset_r), self->buf, j);
                self->used     -= *ret_len;
                self->offset_r += *ret_len;
                self->offset_r %= self->size;

                SVX_CIRCLEBUF_DEBUG("after 3");
                return 0;
            next_round:
                continue;
            }
        }

        /* try to find the ending from self->buf to self->offset_w */
        if(self->offset_w >= ending_len)
        {
            if(NULL != (p = memmem(self->buf, self->offset_w, ending, ending_len)))
            {
                *ret_len = (self->size - self->offset_r) + (p + ending_len - self->buf);
                if(*ret_len > buf_len) return SVX_ERRNO_NOBUF;
                memcpy(buf, self->buf + self->offset_r, self->size - self->offset_r);
                memcpy(buf + (self->size - self->offset_r), self->buf, p + ending_len - self->buf);
                self->used     -= *ret_len;
                self->offset_r += *ret_len;
                self->offset_r %= self->size;

                SVX_CIRCLEBUF_DEBUG("after 4");
                return 0;
            }
        }
        
        /* not found */
        return SVX_ERRNO_NOTFND;
    }
    else /* self->offset_w == self->offset_r && 0 == self->used */
    {
        /*     r
               w
           ---------
           012345678 */

        /* impossible */
        SVX_LOG_ERRNO_RETURN_ERR(SVX_ERRNO_NODATA, NULL);
    }
}
