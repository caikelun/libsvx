/*
 * This source code has been dedicated to the public domain by the authors.
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
    SLIST_ENTRY(my_node,) link;
} my_node_t;

/* queue */
typedef SLIST_HEAD(my_queue, my_node,) my_queue_t;

static int test_slist_check(my_queue_t *queue, int start, int cnt, int data)
{
    my_node_t *node = NULL;
    int        i = 0;

    SLIST_FOREACH(node, queue, link)
    {
        if(node->key != NODE_BASE + start) return 1;
        if(node->data != data) return 1;
        start++;
        i++;
    }
    if(i != cnt) return 1;

    return 0;
}

int test_slist_runner()
{
    my_queue_t  queue, queue2;
    my_node_t  *node = NULL, *node_tmp = NULL;
    int         i = 0;

    if(NODE_CNT < 100)
    {
        printf("Too few nodes\n");
        return 1;
    }

    /* SLIST_INIT, SLIST_EMPTY */
    SLIST_INIT(&queue);
    if(!SLIST_EMPTY(&queue))
    {
        printf("SLIST_INIT, SLIST_EMPTY failed\n");
        return 1;
    }
    SLIST_INIT(&queue2);
    if(!SLIST_EMPTY(&queue2))
    {
        printf("SLIST_INIT, SLIST_EMPTY failed\n");
        return 1;
    }
    
    /* SLIST_INSERT_HEAD, SLIST_INSERT_AFTER */
    for(i = 9; i >= 0; i--)
    {
        if(NULL == (node = malloc(sizeof(my_node_t))))
        {
            printf("SLIST_INSERT_HEAD failed (OOM)\n");
            return 1;
        }
        node->key  = NODE_BASE + i;
        node->data = 1;
        SLIST_INSERT_HEAD(&queue2, node, link);

        if(9 == i) node_tmp = node;
    }
    for(i = NODE_CNT - 1; i > 9; i--)
    {
        if(NULL == (node = malloc(sizeof(my_node_t))))
        {
            printf("SLIST_INSERT_AFTER failed (OOM)\n");
            return 1;
        }
        node->key  = NODE_BASE + i;
        node->data = 1;
        SLIST_INSERT_AFTER(node_tmp, node, link);
    }
    if(0 != test_slist_check(&queue2, 0, NODE_CNT, 1))
    {
        printf("SLIST_INSERT_HEAD, SLIST_INSERT_AFTER failed\n");
        return 1;
    }

    /* SLIST_SWAP */
    SLIST_SWAP(&queue, &queue2, my_node);
    if(!SLIST_EMPTY(&queue2))
    {
        printf("SLIST_SWAP failed\n");
        return 1;
    }
    if(0 != test_slist_check(&queue, 0, NODE_CNT, 1))
    {
        printf("SLIST_SWAP failed\n");
        return 1;
    }

    /* SLIST_FIRST, SLIST_NEXT */
    for(node = SLIST_FIRST(&queue); NULL != node; node = SLIST_NEXT(node, link))
        node->data++;
    if(0 != test_slist_check(&queue, 0, NODE_CNT, 2))
    {
        printf("SLIST_FIRST, SLIST_NEXT failed\n");
        return 1;
    }
    
    /* SLIST_FOREACH, SLIST_FOREACH_FROM */
    SLIST_FOREACH(node, &queue, link)
    {
        if(node->key == NODE_BASE + 10) break;
        node->data++;
    }
    SLIST_FOREACH_FROM(node, &queue, link)
        node->data++;
    if(0 != test_slist_check(&queue, 0, NODE_CNT, 3))
    {
        printf("SLIST_FOREACH, SLIST_FOREACH_FROM failed\n");
        return 1;
    }

    /* SLIST_REMOVE_HEAD, SLIST_REMOVE_AFTER */
    node = SLIST_FIRST(&queue);
    for(i = 0; i < 9; i++)
        node = SLIST_NEXT(node, link);
    for(i = 0; i < 10; i++)
    {
        node_tmp = SLIST_NEXT(node, link);
        SLIST_REMOVE_AFTER(node, link);
        free(node_tmp);
        node_tmp = NULL;
    }
    for(i = 0; i < 10; i++)
    {
        node_tmp = SLIST_FIRST(&queue);
        SLIST_REMOVE_HEAD(&queue, link);
        free(node_tmp);
        node_tmp = NULL;
    }
    if(0 != test_slist_check(&queue, 20, NODE_CNT - 20, 3))
    {
        printf("SLIST_REMOVE_HEAD, SLIST_REMOVE_AFTER failed\n");
        return 1;
    }
    
    /* SLIST_FOREACH_SAFE, SLIST_FOREACH_FROM_SAFE, SLIST_REMOVE */
    node = SLIST_FIRST(&queue);
    for(i = 0; i < 10; i++)
        node = SLIST_NEXT(node, link);
    SLIST_FOREACH_FROM_SAFE(node, &queue, link, node_tmp)
    {
        SLIST_REMOVE(&queue, node, my_node, link); /* DO NOT USE SLIST_REMOVE */
        free(node);
        node = NULL;
    }
    SLIST_FOREACH_SAFE(node, &queue, link, node_tmp)
    {
        SLIST_REMOVE(&queue, node, my_node, link); /* DO NOT USE SLIST_REMOVE */
        free(node);
        node = NULL;
    }
    if(!SLIST_EMPTY(&queue))
    {
        printf("SLIST_FOREACH_SAFE, SLIST_FOREACH_FROM_SAFE, SLIST_REMOVE failed\n");
        return 1;
    }

    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    return 0;
}
