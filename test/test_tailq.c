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
    TAILQ_ENTRY(my_node,) link;
} my_node_t;

/* queue */
typedef TAILQ_HEAD(my_queue, my_node,) my_queue_t;

static int test_tailq_check(my_queue_t *queue, int start, int cnt, int data)
{
    my_node_t *node = NULL;
    int        i = 0;

    TAILQ_FOREACH(node, queue, link)
    {
        if(node->key != NODE_BASE + start) return 1;
        if(node->data != data) return 1;
        start++;
        i++;
    }
    if(i != cnt) return 1;

    return 0;
}

int test_tailq_runner()
{
    my_queue_t  queue, queue2;
    my_node_t  *node = NULL, *node_tmp = NULL;
    int         i = 0;

    if(NODE_CNT < 100)
    {
        printf("Too few nodes\n");
        return 1;
    }

    /* TAILQ_INIT, TAILQ_EMPTY */
    TAILQ_INIT(&queue);
    if(!TAILQ_EMPTY(&queue))
    {
        printf("TAILQ_INIT, TAILQ_EMPTY failed\n");
        return 1;
    }
    TAILQ_INIT(&queue2);
    if(!TAILQ_EMPTY(&queue2))
    {
        printf("TAILQ_INIT, TAILQ_EMPTY failed\n");
        return 1;
    }

    /* TAILQ_INSERT_HEAD, TAILQ_INSERT_AFTER, TAILQ_INSERT_BEFORE, TAILQ_INSERT_TAIL */
    for(i = 9; i >= 0; i--)
    {
        if(NULL == (node = malloc(sizeof(my_node_t))))
        {
            printf("TAILQ_INSERT_HEAD failed (OOM)\n");
            return 1;
        }
        node->key  = NODE_BASE + i;
        node->data = 1;
        TAILQ_INSERT_HEAD(&queue, node, link);

        if(9 == i) node_tmp = node;
    }
    for(i = NODE_CNT - 11; i > 19; i--)
    {
        if(NULL == (node = malloc(sizeof(my_node_t))))
        {
            printf("TAILQ_INSERT_AFTER failed (OOM)\n");
            return 1;
        }
        node->key  = NODE_BASE + i;
        node->data = 1;
        TAILQ_INSERT_AFTER(&queue, node_tmp, node, link);
    }
    node_tmp = TAILQ_NEXT(node_tmp, link);
    for(i = 10; i < 20; i++)
    {
        if(NULL == (node = malloc(sizeof(my_node_t))))
        {
            printf("TAILQ_INSERT_BEFORE failed (OOM)\n");
            return 1;
        }
        node->key  = NODE_BASE + i;
        node->data = 1;
        TAILQ_INSERT_BEFORE(node_tmp, node, link);
    }
    for(i = NODE_CNT - 10; i < NODE_CNT; i++)
    {
        if(NULL == (node = malloc(sizeof(my_node_t))))
        {
            printf("TAILQ_INSERT_TAIL failed (OOM)\n");
            return 1;
        }
        node->key  = NODE_BASE + i;
        node->data = 1;
        TAILQ_INSERT_TAIL(&queue, node, link);
    }
    if(0 != test_tailq_check(&queue, 0, NODE_CNT, 1))
    {
        printf("TAILQ_INSERT_HEAD, TAILQ_INSERT_AFTER, TAILQ_INSERT_BEFORE, TAILQ_INSERT_TAIL failed\n");
        return 1;
    }

    /* TAILQ_CONCAT, TAILQ_SWAP */
    for(i = 0; i < 10; i++)
    {
        node = TAILQ_FIRST(&queue);
        TAILQ_REMOVE(&queue, node, link);
        TAILQ_INSERT_TAIL(&queue2, node, link);
    }
    TAILQ_CONCAT(&queue2, &queue, link);
    if(!TAILQ_EMPTY(&queue))
    {
        printf("TAILQ_CONCAT failed\n");
        return 1;
    }
    if(0 != test_tailq_check(&queue2, 0, NODE_CNT, 1))
    {
        printf("TAILQ_CONCAT failed\n");
        return 1;
    }
    TAILQ_SWAP(&queue, &queue2, my_node, link);
    if(!TAILQ_EMPTY(&queue2))
    {
        printf("TAILQ_SWAP failed\n");
        return 1;
    }
    if(0 != test_tailq_check(&queue, 0, NODE_CNT, 1))
    {
        printf("TAILQ_SWAP failed\n");
        return 1;
    }

    /* TAILQ_FIRST, TAILQ_NEXT, TAILQ_PREV, TAILQ_LAST */
    for(node = TAILQ_FIRST(&queue); NULL != node; node = TAILQ_NEXT(node, link))
        node->data++;
    if(0 != test_tailq_check(&queue, 0, NODE_CNT, 2))
    {
        printf("TAILQ_FIRST, TAILQ_NEXT failed\n");
        return 1;
    }
    for(node = TAILQ_LAST(&queue, my_queue); NULL != node; node = TAILQ_PREV(node, my_queue, link))
        node->data++;
    if(0 != test_tailq_check(&queue, 0, NODE_CNT, 3))
    {
        printf("TAILQ_PREV, TAILQ_LAST failed\n");
        return 1;
    }

    /* STAILQ_FOREACH, STAILQ_FOREACH_FROM */
    TAILQ_FOREACH(node, &queue, link)
    {
        if(node->key == NODE_BASE + 10) break;
        node->data++;
    }
    TAILQ_FOREACH_FROM(node, &queue, link)
        node->data++;
    if(0 != test_tailq_check(&queue, 0, NODE_CNT, 4))
    {
        printf("TAILQ_FOREACH, TAILQ_FOREACH_FROM failed\n");
        return 1;
    }

    /* STAILQ_FOREACH_REVERSE, STAILQ_FOREACH_REVERSE_FROM */
    TAILQ_FOREACH_REVERSE(node, &queue, my_queue, link)
    {
        if(node->key == NODE_BASE + 10) break;
        node->data++;
    }
    TAILQ_FOREACH_REVERSE_FROM(node, &queue, my_queue, link)
        node->data++;
    if(0 != test_tailq_check(&queue, 0, NODE_CNT, 5))
    {
        printf("TAILQ_FOREACH_REVERSE, TAILQ_FOREACH_REVERSE_FROM failed\n");
        return 1;
    }

    /* STAILQ_FOREACH_SAFE, STAILQ_FOREACH_FROM_SAFE, STAILQ_REMOVE */
    node = TAILQ_FIRST(&queue);
    for(i = 0; i < 20; i++)
        node = TAILQ_NEXT(node, link);
    i = 0;
    TAILQ_FOREACH_FROM_SAFE(node, &queue, link, node_tmp)
    {
        if(node->key == NODE_BASE + 30) break;
        TAILQ_REMOVE(&queue, node, link);
        free(node);
        node = NULL;
        i++;
    }
    if(10 != i)
    {
        printf("TAILQ_FOREACH_FROM_SAFE, TAILQ_REMOVE failed\n");
        return 1;
    }
    node = TAILQ_PREV(node, my_queue, link);
    i = 0;
    TAILQ_FOREACH_REVERSE_FROM_SAFE(node, &queue, my_queue, link, node_tmp)
    {
        if(node->key == NODE_BASE + 9) break;
        TAILQ_REMOVE(&queue, node, link);
        free(node);
        node = NULL;
        i++;
    }
    if(10 != i)
    {
        printf("TAILQ_FOREACH_REVERSE_FROM_SAFE, TAILQ_REMOVE failed\n");
        return 1;
    }
    i = 0;
    TAILQ_FOREACH_SAFE(node, &queue, link, node_tmp)
    {
        if(node->key == NODE_BASE + 30) break;
        TAILQ_REMOVE(&queue, node, link);
        free(node);
        node = NULL;
        i++;
    }
    if(10 != i)
    {
        printf("TAILQ_FOREACH_SAFE, TAILQ_REMOVE failed\n");
        return 1;
    }
    i = 0;
    TAILQ_FOREACH_REVERSE_SAFE(node, &queue, my_queue, link, node_tmp)
    {
        TAILQ_REMOVE(&queue, node, link);
        free(node);
        node = NULL;
        i++;
    }
    if(NODE_CNT - 30 != i)
    {
        printf("TAILQ_FOREACH_REVERSE_SAFE, TAILQ_REMOVE failed\n");
        return 1;
    }
    if(!TAILQ_EMPTY(&queue))
    {
        printf("TAILQ_FOREACH_SAFE, TAILQ_FOREACH_FROM_SAFE, TAILQ_FOREACH_REVERSE_SAFE, TAILQ_FOREACH_REVERSE_FROM_SAFE, STAILQ_REMOVE failed\n");
        return 1;
    }

    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    return 0;
}
