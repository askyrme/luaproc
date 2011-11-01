/***************************************************

Copyright 2008 Alexandre Skyrme, Noemi Rodriguez, Roberto Ierusalimschy

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*****************************************************

[list.h]

****************************************************/

#ifndef _LIST_H_
#define _LIST_H_

#define LIST_COUNT_ERROR	-1

/* node pointer type */
typedef struct stnode *node;

/* list pointer type */
typedef struct stlist *list;

/* create new (empty) list */
list list_new( void );

/* create new node */
node list_new_node( void *data );

/* add node to list */
node list_add( list lst, node n ); 

/* search for a node */
node list_search( list lst, void *data );

/* remove node from list */
void list_remove( list lst, node n ); 

/* destroy list */
void list_destroy( list lst );

/* return list's first node */
node list_head( list lst );

/* return node's next node */
node list_next( node n ); 

/* return a node's data */
void *list_data( node n );

/* pop the head node from the list */
node list_pop_head( list lst ); 

/* destroy a node */
void list_destroy_node( node n );

/* return a list's node count */
int list_node_count( list lst );

#endif
