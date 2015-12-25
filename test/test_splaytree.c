/*
 * This source code has been dedicated to the public domain by the author.
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
    SPLAY_ENTRY(my_node) link;
} my_node_t;
static inline int my_node_cmp(my_node_t *a, my_node_t *b)
{
    return (a->key > b->key) - (a->key < b->key);
}

/* tree */
typedef SPLAY_HEAD(my_tree, my_node) my_tree_t;

/* generate functions */
SPLAY_PROTOTYPE(my_tree, my_node, link, my_node_cmp)
SPLAY_GENERATE(my_tree, my_node, link, my_node_cmp)

int test_splaytree_runner()
{
    my_tree_t  tree;
    my_node_t *node = NULL, *node_tmp = NULL, node_key;
    int        i = 0;
    
    /* SPLAY_INIT, SPLAY_EMPTY */
    SPLAY_INIT(&tree);
    if(!SPLAY_EMPTY(&tree))
    {
        printf("SPLAY_INIT, SPLAY_EMPTY failed\n");
        return 1;
    }

    /* SPLAY_INSERT */
    for(i = 0; i < NODE_CNT; i++)
    {
        if(NULL == (node = malloc(sizeof(my_node_t))))
        {
            printf("SPLAY_INSERT failed (OOM)\n");
            return 1;
        }
        node->key  = NODE_BASE + i;
        node->data = 1;
        if(NULL != SPLAY_INSERT(my_tree, &tree, node))
        {
            printf("SPLAY_INSERT failed (colliding key)\n");
            return 1;
        }
    }
    
    /* SPLAY_FIND */
    for(i = 0; i < NODE_CNT; i++)
    {
        node_key.key = NODE_BASE + i;
        node = SPLAY_FIND(my_tree, &tree, &node_key);
        if(NULL == node)
        {
            printf("SPLAY_FIND failed\n");
            return 1;
        }
    }

    /* SPLAY_MIN, SPLAY_NEXT */
    if(NULL == (node = SPLAY_MIN(my_tree, &tree)))
    {
        printf("SPLAY_MIN failed\n");
        return 1;
    }
    i = 0;
    while(node)
    {
        if(node->key != NODE_BASE + i)
        {
            printf("SPLAY_MIN, SPLAY_NEXT failed\n");
            return 1;
        }
        i++;
        node = SPLAY_NEXT(my_tree, &tree, node);
    }
    if(i != NODE_CNT)
    {
        printf("SPLAY_MIN, SPLAY_NEXT failed\n");
        return 1;
    }

    /* SPLAY_MAX */
    if(NULL == (node = SPLAY_MAX(my_tree, &tree)))
    {
        printf("SPLAY_MAX failed\n");
        return 1;
    }
    if(node->key != NODE_BASE + NODE_CNT - 1)
    {
        printf("SPLAY_MAX failed\n");
        return 1;
    }

    /* SPLAY_FOREACH */
    SPLAY_FOREACH(node, my_tree, &tree)
    {
        node->data++;
    }
    SPLAY_FOREACH(node, my_tree, &tree)
    {
        if(2 != node->data)
        {
            printf("SPLAY_FOREACH failed\n");
            return 1;
        }
    }

    /* SPLAY_REMOVE, SPLAY_EMPTY */
    for(node = SPLAY_MIN(my_tree, &tree); NULL != node; node = node_tmp)
    {
        node_tmp = SPLAY_NEXT(my_tree, &tree, node);

        node = SPLAY_REMOVE(my_tree, &tree, node);
        if(NULL == node)
        {
            printf("SPLAY_REMOVE failed\n");
            return 1;
        }
        free(node);
        node = NULL;
    }
    if(!SPLAY_EMPTY(&tree))
    {
        printf("SPLAY_REMOVE, SPLAY_EMPTY failed\n");
        return 1;
    }
    
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    return 0;
}
