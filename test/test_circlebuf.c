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
#include <inttypes.h>
#include "svx_circlebuf.h"

#define TEST_CIRCLEBUF_BLOCK_TEST      10240
#define TEST_CIRCLEBUF_MAX_LEN         1231
#define TEST_CIRCLEBUF_MIN_LEN         37
#define TEST_CIRCLEBUF_MIN_STEP        62
#define TEST_CIRCLEBUF_ENDING          "abcdefg"
#define TEST_CIRCLEBUF_ENDING_LEN      7
#define TEST_CIRCLEBUF_PAYLOAD_CHAR    '#'
#define TEST_CIRCLEBUF_PAYLOAD_LEN_MAX 10

typedef struct
{
    uint64_t payload_len;
    uint8_t  ending[TEST_CIRCLEBUF_ENDING_LEN];
}__attribute__((packed)) test_circlebuf_header_t;

int test_circlebuf_runner()
{
    int                      r  = 0;
    svx_circlebuf_t         *cb = NULL;
    size_t                   cb_block_capacity = (TEST_CIRCLEBUF_MAX_LEN / (sizeof(test_circlebuf_header_t) + TEST_CIRCLEBUF_PAYLOAD_LEN_MAX));
    size_t                   cb_block_used = 0;
    uint8_t                  block_send[sizeof(test_circlebuf_header_t) + TEST_CIRCLEBUF_PAYLOAD_LEN_MAX];
    test_circlebuf_header_t *block_send_header = (test_circlebuf_header_t *)block_send;
    uint8_t                 *block_send_payload = block_send + sizeof(test_circlebuf_header_t);
    size_t                   block_send_cnt = 0;
    uint8_t                  block_recv[sizeof(test_circlebuf_header_t) + TEST_CIRCLEBUF_PAYLOAD_LEN_MAX];
    test_circlebuf_header_t *block_recv_header = (test_circlebuf_header_t *)block_recv;
    uint8_t                 *block_recv_payload = block_recv + sizeof(test_circlebuf_header_t);
    size_t                   block_recv_cnt = 0;
    uint64_t                 block_payload_len_saved[TEST_CIRCLEBUF_BLOCK_TEST];
    size_t                   block_len = 0;
    size_t                   i = 0, j = 0;
    uint8_t                 *buf1 = NULL, *buf2 = NULL;
    size_t                   buf1_len = 0, buf2_len = 0;
    size_t                   ret_len = 0;
    size_t                   buf_len = 0, data_len = 0, freespace_len = 0;

    /* init data */
    memcpy(block_send_header->ending, TEST_CIRCLEBUF_ENDING, TEST_CIRCLEBUF_ENDING_LEN);
    memset(block_send_payload, TEST_CIRCLEBUF_PAYLOAD_CHAR, TEST_CIRCLEBUF_PAYLOAD_LEN_MAX);
    for(i = 0; i < TEST_CIRCLEBUF_BLOCK_TEST; i++)
        block_payload_len_saved[i] = (uint64_t)(random() % (TEST_CIRCLEBUF_PAYLOAD_LEN_MAX + 1));

    if(0 != (r = svx_circlebuf_create(&cb, TEST_CIRCLEBUF_MAX_LEN, TEST_CIRCLEBUF_MIN_LEN, TEST_CIRCLEBUF_MIN_STEP)))
    {
        printf("svx_circlebuf_create() failed\n");
        goto end;
    }

    while(block_send_cnt < TEST_CIRCLEBUF_BLOCK_TEST || block_recv_cnt < TEST_CIRCLEBUF_BLOCK_TEST)
    {
        /* (1) send blocks to buffer */
        i = (uint64_t)(random() % (cb_block_capacity - cb_block_used + 1));
        //printf("*** send loop ------------> %"PRIu64"\n", i);
        while(block_send_cnt < TEST_CIRCLEBUF_BLOCK_TEST && i--)
        {
            //printf("*** send: [%zu] = %zu\n", block_send_cnt, block_payload_len_saved[block_send_cnt]);
            block_send_header->payload_len  = block_payload_len_saved[block_send_cnt];
            block_len = sizeof(test_circlebuf_header_t) + block_send_header->payload_len;

            if(0 == block_send_cnt % 2) /* use append_data() */
            {
                if(0 != (r = svx_circlebuf_append_data(cb, block_send, block_len)))
                {
                    printf("svx_circlebuf_append_data() failed\n");
                    goto end;
                }
            }
            else /* use get_freespace_ptr(), memcpy() and commit_data() */
            {
                if(0 != (r = svx_circlebuf_expand(cb, block_len)))
                {
                    printf("svx_circlebuf_expand() failed\n");
                    goto end;
                }
                if(0 != (r = svx_circlebuf_get_freespace_ptr(cb, &buf1, &buf1_len, &buf2, &buf2_len)))
                {
                    printf("svx_circlebuf_get_freespace_ptr() failed\n");
                    goto end;
                }
                if(buf1_len + buf2_len < block_len)
                {
                    r = 1;
                    printf("no freespace\n");
                    goto end;
                }

                if(buf1_len >= block_len)
                {
                    memcpy(buf1, block_send, block_len);
                }
                else
                {
                    memcpy(buf1, block_send, buf1_len);
                    memcpy(buf2, block_send + buf1_len, block_len - buf1_len);
                }

                if(0 != (r = svx_circlebuf_commit_data(cb, block_len)))
                {
                    printf("svx_circlebuf_commit_data() failed\n");
                    goto end;
                }
            }

            block_send_cnt++;
            cb_block_used++;
        }

        /* receive blocks from buffer, check it */
        i = (uint64_t)(random() % (cb_block_used + 1));
        //printf("*** recv loop ------------> %"PRIu64"\n", i);
        while(block_recv_cnt < TEST_CIRCLEBUF_BLOCK_TEST && i--)
        {
            //printf("*** recv: [%zu] = %zu\n", block_recv_cnt, block_payload_len_saved[block_recv_cnt]);            
            memset(block_recv, 0, sizeof(block_recv));

            /* get block */
            if(0 == block_recv_cnt % 3) /* use get_data_by_ending(), get_data() */
            {
                /* get header */
                if(0 != (r = svx_circlebuf_get_data_by_ending(cb, (uint8_t *)TEST_CIRCLEBUF_ENDING, 
                                                              TEST_CIRCLEBUF_ENDING_LEN,
                                                              block_recv, sizeof(block_recv), &ret_len)))
                {
                    printf("svx_circlebuf_get_data_by_ending() failed\n");
                    goto end;
                }
                if(sizeof(test_circlebuf_header_t) != ret_len)
                {
                    r = 1;
                    printf("check header length failed %zu\n", block_recv_cnt);
                    goto end;
                }

                /* get payload */
                if(block_recv_header->payload_len > 0)
                {
                    if(0 != (r = svx_circlebuf_get_data(cb, block_recv_payload, block_recv_header->payload_len)))
                    {
                        printf("svx_circlebuf_get_data() failed\n");
                        goto end;
                    }
                }
            }
            else /* use get_data_ptr(), erase_data() */
            {
                block_len = sizeof(test_circlebuf_header_t) + block_payload_len_saved[block_recv_cnt];

                /* get header & payload */
                if(0 != (r = svx_circlebuf_get_data_ptr(cb, &buf1, &buf1_len, &buf2, &buf2_len)))
                {
                    printf("svx_circlebuf_get_data_ptr() failed\n");
                    goto end;
                }
                if(buf1_len + buf2_len < block_len)
                {
                    r = 1;
                    printf("check data length failed\n");
                    goto end;
                }
                if(buf1_len >= block_len)
                {
                    memcpy(block_recv, buf1, block_len);
                }
                else
                {
                    memcpy(block_recv, buf1, buf1_len);
                    memcpy(block_recv + buf1_len, buf2, block_len - buf1_len);
                }
                if(0 != (r = svx_circlebuf_erase_data(cb, block_len)))
                {
                    printf("svx_circlebuf_erase_data() failed\n");
                    goto end;
                }
            }
            
            /* check block */
            if(0 != memcmp(block_recv_header->ending, TEST_CIRCLEBUF_ENDING, TEST_CIRCLEBUF_ENDING_LEN))
            {
                r = 1;
                printf("check header ending failed\n");
                goto end;
            }
            if(block_payload_len_saved[block_recv_cnt] != block_recv_header->payload_len)
            {
                r = 1;
                printf("check payload length failed\n");
                goto end;
            }
            for(j = 0; j < block_recv_header->payload_len; j++)
            {
                if(block_recv_payload[j] != TEST_CIRCLEBUF_PAYLOAD_CHAR)
                {
                    r = 1;
                    printf("check payload failed\n");
                    goto end;
                }
            }

            /* shrink buffer */
            if(0 == block_recv_cnt % 5)
            {
                if(0 != (r = svx_circlebuf_shrink(cb, TEST_CIRCLEBUF_MIN_STEP)))
                {
                    printf("svx_circlebuf_shrink() failed\n");
                    goto end;
                }
            }

            block_recv_cnt++;
            cb_block_used--;
        }
    }

    /* check buffer's data length */
    if(0 != (r = svx_circlebuf_get_buf_len(cb, &buf_len)))
    {
        printf("svx_circlebuf_get_buf_len() failed\n");
        goto end;
    }
    if(0 != (r = svx_circlebuf_get_data_len(cb, &data_len)))
    {
        printf("svx_circlebuf_get_data_len() failed\n");
        goto end;
    }
    if(0 != (r = svx_circlebuf_get_freespace_len(cb, &freespace_len)))
    {
        printf("svx_circlebuf_get_freespace_len() failed\n");
        goto end;
    }
    if(0 != data_len)
    {
        r = 1;
        printf("check data length failed (!= 0)\n");
        goto end;
    }
    if(freespace_len != buf_len)
    {
        r = 1;
        printf("check freespace length & buffer length failed (!=)\n");
        goto end;
    }

 end:
    if(cb)
    {
        if(0 != (r = svx_circlebuf_destroy(&cb)) || NULL != cb)
        {
            printf("svx_callqueue_destroy() failed\n");
        }
    }

    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    return r;
}
