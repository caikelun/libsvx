/*
 * This source code has been dedicated to the public domain by the authors.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this source code, either in source code form or as a compiled binary, 
 * for any purpose, commercial or non-commercial, and by any means.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include "svx_tree.h"

#define NODE_BASE 0x0badc0de
#define NODE_CNT  1024

/* node */
typedef struct my_node
{
    int key;
    int data;
    RB_ENTRY(my_node) link;
} my_node_t;
static __inline__ int my_node_cmp(my_node_t *a, my_node_t *b)
{
    return (a->key > b->key) - (a->key < b->key);
}

/* tree */
typedef RB_HEAD(my_tree, my_node) my_tree_t;

/* generate functions */
RB_GENERATE_STATIC(my_tree, my_node, link, my_node_cmp)

int test_rbtree_runner()
{
    my_tree_t  tree;
    my_node_t *node = NULL, *node_tmp = NULL, *node_from = NULL, node_key;
    int        i = 0;
    
    /* RB_INIT, RB_EMPTY */
    RB_INIT(&tree);
    if(!RB_EMPTY(&tree))
    {
        printf("RB_INIT, RB_EMPTY failed\n");
        return 1;
    }

    /* RB_INSERT */
    for(i = 0; i < NODE_CNT; i++)
    {
        if(NULL == (node = malloc(sizeof(my_node_t))))
        {
            printf("RB_INSERT failed (OOM)\n");
            return 1;
        }
        node->key  = NODE_BASE + i;
        node->data = 1;
        if(NULL != RB_INSERT(my_tree, &tree, node))
        {
            printf("RB_INSERT failed (colliding key)\n");
            return 1;
        }
    }
    
    /* RB_FIND */
    for(i = 0; i < NODE_CNT; i++)
    {
        node_key.key = NODE_BASE + i;
        node = RB_FIND(my_tree, &tree, &node_key);
        if(NULL == node || node->key != NODE_BASE + i)
        {
            printf("RB_FIND failed\n");
            return 1;
        }
    }

    /* RB_NFIND */
    for(i = 0; i < NODE_CNT; i++)
    {
        node_key.key = NODE_BASE + i;
        node = RB_NFIND(my_tree, &tree, &node_key);
        if(NULL == node || node->key < NODE_BASE + i)
        {
            printf("RB_NFIND failed\n");
            return 1;
        }
    }

    /* RB_MIN, RB_NEXT */
    if(NULL == (node = RB_MIN(my_tree, &tree)))
    {
        printf("RB_MIN failed\n");
        return 1;
    }
    i = 0;
    while(node)
    {
        if(node->key != NODE_BASE + i)
        {
            printf("RB_MIN, RB_NEXT failed\n");
            return 1;
        }
        i++;
        node = RB_NEXT(my_tree, &tree, node);
    }
    if(i != NODE_CNT)
    {
        printf("RB_MIN, RB_NEXT failed\n");
        return 1;
    }

    /* RB_MAX, RB_PREV */
    if(NULL == (node = RB_MAX(my_tree, &tree)))
    {
        printf("RB_MAX failed\n");
        return 1;
    }        
    i = 0;
    while(node)
    {
        if(node->key != NODE_BASE + NODE_CNT - i - 1)
        {
            printf("RB_MAX, RB_PREV failed\n");
            return 1;
        }
        i++;
        node = RB_PREV(my_tree, &tree, node);
    }
    if(i != NODE_CNT)
    {
        printf("RB_MAX, RB_PREV failed\n");
        return 1;
    }
    
    /* RB_FOREACH, RB_FOREACH_REVERSE */
    RB_FOREACH(node, my_tree, &tree)
    {
        node->data++;
    }
    RB_FOREACH_REVERSE(node, my_tree, &tree)
    {
        if(2 != node->data)
        {
            printf("RB_FOREACH, RB_FOREACH_REVERSE failed\n");
            return 1;
        }
        node->data++;
    }
    RB_FOREACH(node, my_tree, &tree)
    {
        if(3 != node->data)
        {
            printf("RB_FOREACH, RB_FOREACH_REVERSE failed\n");
            return 1;
        }
    }

    /* RB_FOREACH_FROM, RB_FOREACH_REVERSE_FROM */
    node_key.key = NODE_BASE + 10;
    node_from = RB_FIND(my_tree, &tree, &node_key);
    if(NULL == node_from)
    {
        printf("RB_FOREACH_FROM, RB_FOREACH_REVERSE_FROM (RB_FIND) failed\n");
        return 1;
    }
    RB_FOREACH_FROM(node, my_tree, node_from)
    {
        node->data = 1;
    }
    node_key.key = NODE_BASE + 9;
    node_from = RB_FIND(my_tree, &tree, &node_key);
    if(NULL == node_from)
    {
        printf("RB_FOREACH_FROM, RB_FOREACH_REVERSE_FROM (RB_FIND) failed\n");
        return 1;
    }
    RB_FOREACH_REVERSE_FROM(node, my_tree, node_from)
    {
        node->data = 1;
    }
    RB_FOREACH(node, my_tree, &tree)
    {
        if(1 != node->data)
        {
            printf("RB_FOREACH_FROM, RB_FOREACH_REVERSE_FROM failed\n");
            return 1;
        }
    }

    /* RB_FOREACH_SAFE, RB_FOREACH_REVERSE_SAFE, RB_REMOVE, RB_EMPTY */
    RB_FOREACH_SAFE(node, my_tree, &tree, node_tmp)
    {
        if(node->key == NODE_BASE + 10) break;

        node = RB_REMOVE(my_tree, &tree, node);
        if(NULL == node)
        {
            printf("RB_FOREACH_SAFE, RB_REMOVE failed\n");
            return 1;
        }
        free(node);
        node = NULL;
    }
    RB_FOREACH_REVERSE_SAFE(node, my_tree, &tree, node_tmp)
    {
        node = RB_REMOVE(my_tree, &tree, node);
        if(NULL == node)
        {
            printf("RB_FOREACH_REVERSE_SAFE, RB_REMOVE failed\n");
            return 1;
        }
        free(node);
        node = NULL;
    }
    if(!RB_EMPTY(&tree))
    {
        printf("RB_FOREACH_SAFE, RB_FOREACH_REVERSE_SAFE, RB_REMOVE, RB_EMPTY failed\n");
        return 1;
    }
    
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    return 0;
}
