/*
 * This source code has been dedicated to the public domain by the author.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include "svx_queue.h"

#define NODE_BASE 0x0badc0de
#define NODE_CNT  1024

/* node */
typedef struct my_node
{
    int key;
    int data;
    STAILQ_ENTRY(my_node,) link;
} my_node_t;

/* queue */
typedef STAILQ_HEAD(my_queue, my_node,) my_queue_t;

static int test_stailq_check(my_queue_t *queue, int start, int cnt, int data)
{
    my_node_t *node = NULL;
    int        i = 0;

    STAILQ_FOREACH(node, queue, link)
    {
        if(node->key != NODE_BASE + start) return 1;
        if(node->data != data) return 1;
        start++;
        i++;
    }
    if(i != cnt) return 1;

    return 0;
}

int test_stailq_runner()
{
    my_queue_t  queue, queue2;
    my_node_t  *node = NULL, *node_tmp = NULL;
    int         i = 0;

    if(NODE_CNT < 100)
    {
        printf("Too few nodes\n");
        return 1;
    }

    /* STAILQ_INIT, STAILQ_EMPTY */
    STAILQ_INIT(&queue);
    if(!STAILQ_EMPTY(&queue))
    {
        printf("STAILQ_INIT, STAILQ_EMPTY failed\n");
        return 1;
    }
    STAILQ_INIT(&queue2);
    if(!STAILQ_EMPTY(&queue2))
    {
        printf("STAILQ_INIT, STAILQ_EMPTY failed\n");
        return 1;
    }

    /* STAILQ_INSERT_HEAD, STAILQ_INSERT_AFTER, STAILQ_INSERT_TAIL */
    for(i = 9; i >= 0; i--)
    {
        if(NULL == (node = malloc(sizeof(my_node_t))))
        {
            printf("STAILQ_INSERT_HEAD failed (OOM)\n");
            return 1;
        }
        node->key  = NODE_BASE + i;
        node->data = 1;
        STAILQ_INSERT_HEAD(&queue, node, link);

        if(9 == i) node_tmp = node;
    }
    for(i = NODE_CNT - 11; i > 9; i--)
    {
        if(NULL == (node = malloc(sizeof(my_node_t))))
        {
            printf("STAILQ_INSERT_AFTER failed (OOM)\n");
            return 1;
        }
        node->key  = NODE_BASE + i;
        node->data = 1;
        STAILQ_INSERT_AFTER(&queue, node_tmp, node, link);
    }
    for(i = NODE_CNT - 10; i < NODE_CNT; i++)
    {
        if(NULL == (node = malloc(sizeof(my_node_t))))
        {
            printf("STAILQ_INSERT_TAIL failed (OOM)\n");
            return 1;
        }
        node->key  = NODE_BASE + i;
        node->data = 1;
        STAILQ_INSERT_TAIL(&queue, node, link);
    }
    if(0 != test_stailq_check(&queue, 0, NODE_CNT, 1))
    {
        printf("STAILQ_INSERT_HEAD, STAILQ_INSERT_AFTER, STAILQ_INSERT_TAIL failed\n");
        return 1;
    }

    /* STAILQ_CONCAT, STAILQ_SWAP */
    for(i = 0; i < 10; i++)
    {
        node = STAILQ_FIRST(&queue);
        STAILQ_REMOVE_HEAD(&queue, link);
        STAILQ_INSERT_TAIL(&queue2, node, link);
    }
    STAILQ_CONCAT(&queue2, &queue);
    if(!STAILQ_EMPTY(&queue))
    {
        printf("STAILQ_CONCAT failed\n");
        return 1;
    }
    if(0 != test_stailq_check(&queue2, 0, NODE_CNT, 1))
    {
        printf("STAILQ_CONCAT failed\n");
        return 1;
    }
    STAILQ_SWAP(&queue, &queue2, my_node);
    if(!STAILQ_EMPTY(&queue2))
    {
        printf("STAILQ_SWAP failed\n");
        return 1;
    }
    if(0 != test_stailq_check(&queue, 0, NODE_CNT, 1))
    {
        printf("STAILQ_SWAP failed\n");
        return 1;
    }

    /* STAILQ_FIRST, STAILQ_NEXT, STAILQ_LAST */
    for(node = STAILQ_FIRST(&queue); NULL != node; node = STAILQ_NEXT(node, link))
        node->data++;
    if(0 != test_stailq_check(&queue, 0, NODE_CNT, 2))
    {
        printf("STAILQ_FIRST, STAILQ_NEXT failed\n");
        return 1;
    }
    node = STAILQ_LAST(&queue, my_node, link);
    if(node->key != NODE_BASE + NODE_CNT - 1)
    {
        printf("STAILQ_LAST failed\n");
        return 1;
    }

    /* STAILQ_FOREACH, STAILQ_FOREACH_FROM */
    STAILQ_FOREACH(node, &queue, link)
    {
        if(node->key == NODE_BASE + 10) break;
        node->data++;
    }
    STAILQ_FOREACH_FROM(node, &queue, link)
        node->data++;
    if(0 != test_stailq_check(&queue, 0, NODE_CNT, 3))
    {
        printf("STAILQ_FOREACH, STAILQ_FOREACH_FROM failed\n");
        return 1;
    }

    /* STAILQ_REMOVE_HEAD, STAILQ_REMOVE_AFTER */
    node = STAILQ_FIRST(&queue);
    for(i = 0; i < 9; i++)
        node = STAILQ_NEXT(node, link);
    for(i = 0; i < 10; i++)
    {
        node_tmp = STAILQ_NEXT(node, link);
        STAILQ_REMOVE_AFTER(&queue, node, link);
        free(node_tmp);
        node_tmp = NULL;
    }
    for(i = 0; i < 10; i++)
    {
        node_tmp = STAILQ_FIRST(&queue);
        STAILQ_REMOVE_HEAD(&queue, link);
        free(node_tmp);
        node_tmp = NULL;
    }
    if(0 != test_stailq_check(&queue, 20, NODE_CNT - 20, 3))
    {
        printf("STAILQ_REMOVE_HEAD, STAILQ_REMOVE_AFTER failed\n");
        return 1;
    }

    /* STAILQ_FOREACH_SAFE, STAILQ_FOREACH_FROM_SAFE, STAILQ_REMOVE */
    node = STAILQ_FIRST(&queue);
    for(i = 0; i < 10; i++)
        node = STAILQ_NEXT(node, link);
    STAILQ_FOREACH_FROM_SAFE(node, &queue, link, node_tmp)
    {
        STAILQ_REMOVE(&queue, node, my_node, link); /* DO NOT USE STAILQ_REMOVE */
        free(node);
        node = NULL;
    }
    STAILQ_FOREACH_SAFE(node, &queue, link, node_tmp)
    {
        STAILQ_REMOVE(&queue, node, my_node, link); /* DO NOT USE STAILQ_REMOVE */
        free(node);
        node = NULL;
    }
    if(!STAILQ_EMPTY(&queue))
    {
        printf("STAILQ_FOREACH_SAFE, STAILQ_FOREACH_FROM_SAFE, STAILQ_REMOVE failed\n");
        return 1;
    }

    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    return 0;
}
