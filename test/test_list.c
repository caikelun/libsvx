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
    LIST_ENTRY(my_node,) link;
} my_node_t;

/* queue */
typedef LIST_HEAD(my_queue, my_node,) my_queue_t;

static int test_list_check(my_queue_t *queue, int start, int cnt, int data)
{
    my_node_t *node = NULL;
    int        i = 0;

    LIST_FOREACH(node, queue, link)
    {
        if(node->key != NODE_BASE + start) return 1;
        if(node->data != data) return 1;
        start++;
        i++;
    }
    if(i != cnt) return 1;

    return 0;
}

int test_list_runner()
{
    my_queue_t  queue, queue2;
    my_node_t  *node = NULL, *node_tmp = NULL;
    int         i = 0;

    if(NODE_CNT < 100)
    {
        printf("Too few nodes\n");
        return 1;
    }

    /* LIST_INIT, LIST_EMPTY */
    LIST_INIT(&queue);
    if(!LIST_EMPTY(&queue))
    {
        printf("LIST_INIT, LIST_EMPTY failed\n");
        return 1;
    }
    LIST_INIT(&queue2);
    if(!LIST_EMPTY(&queue2))
    {
        printf("LIST_INIT, LIST_EMPTY failed\n");
        return 1;
    }
    
    /* LIST_INSERT_HEAD, LIST_INSERT_AFTER, LIST_INSERT_BEFORE */
    for(i = 9; i >= 0; i--)
    {
        if(NULL == (node = malloc(sizeof(my_node_t))))
        {
            printf("LIST_INSERT_HEAD failed (OOM)\n");
            return 1;
        }
        node->key  = NODE_BASE + i;
        node->data = 1;
        LIST_INSERT_HEAD(&queue2, node, link);

        if(9 == i) node_tmp = node;
    }
    for(i = NODE_CNT - 1; i > 19; i--)
    {
        if(NULL == (node = malloc(sizeof(my_node_t))))
        {
            printf("LIST_INSERT_AFTER failed (OOM)\n");
            return 1;
        }
        node->key  = NODE_BASE + i;
        node->data = 1;
        LIST_INSERT_AFTER(node_tmp, node, link);
    }
    node_tmp = LIST_NEXT(node_tmp, link);
    for(i = 10; i <= 19; i++)
    {
        if(NULL == (node = malloc(sizeof(my_node_t))))
        {
            printf("LIST_INSERT_BEFORE failed (OOM)\n");
            return 1;
        }
        node->key  = NODE_BASE + i;
        node->data = 1;
        LIST_INSERT_BEFORE(node_tmp, node, link);
    }    
    if(0 != test_list_check(&queue2, 0, NODE_CNT, 1))
    {
        printf("LIST_INSERT_HEAD, LIST_INSERT_AFTER, LIST_INSERT_BEFORE failed\n");
        return 1;
    }

    /* LIST_SWAP */
    LIST_SWAP(&queue, &queue2, my_node, link);
    if(!LIST_EMPTY(&queue2))
    {
        printf("LIST_SWAP failed\n");
        return 1;
    }
    if(0 != test_list_check(&queue, 0, NODE_CNT, 1))
    {
        printf("LIST_SWAP failed\n");
        return 1;
    }

    /* LIST_FIRST, LIST_NEXT, LIST_PREV */
    for(node = LIST_FIRST(&queue); NULL != node; node = LIST_NEXT(node, link))
    {
        node_tmp = node;
        node->data++;
    }
    if(0 != test_list_check(&queue, 0, NODE_CNT, 2))
    {
        printf("LIST_FIRST, LIST_NEXT failed\n");
        return 1;
    }
    for(node = node_tmp; NULL != node; node = LIST_PREV(node, &queue, my_node, link))
        node->data++;
    if(0 != test_list_check(&queue, 0, NODE_CNT, 3))
    {
        printf("LIST_PREV failed\n");
        return 1;
    }

    /* LIST_FOREACH, LIST_FOREACH_FROM */
    LIST_FOREACH(node, &queue, link)
    {
        if(node->key == NODE_BASE + 10) break;
        node->data++;
    }
    LIST_FOREACH_FROM(node, &queue, link)
        node->data++;
    if(0 != test_list_check(&queue, 0, NODE_CNT, 4))
    {
        printf("LIST_FOREACH, LIST_FOREACH_FROM failed\n");
        return 1;
    }

    /* LIST_FOREACH_SAFE, LIST_FOREACH_FROM_SAFE, LIST_REMOVE */
    node = LIST_FIRST(&queue);
    for(i = 0; i < 10; i++)
        node = LIST_NEXT(node, link);
    LIST_FOREACH_FROM_SAFE(node, &queue, link, node_tmp)
    {
        LIST_REMOVE(node, link);
        free(node);
        node = NULL;
    }
    LIST_FOREACH_SAFE(node, &queue, link, node_tmp)
    {
        LIST_REMOVE(node, link);
        free(node);
        node = NULL;
    }
    if(!LIST_EMPTY(&queue))
    {
        printf("LIST_FOREACH_SAFE, LIST_FOREACH_FROM_SAFE, LIST_REMOVE failed\n");
        return 1;
    }

    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    return 0;
}
