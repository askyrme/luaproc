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

[list.c]

****************************************************/
#include <stdio.h>
#include <stdlib.h>

#include "list.h"

/* linked list node */
struct stnode {
	void *data;
	struct stnode *next;
	struct stnode *prev;
};

/* linked list */
struct stlist {
	node head;
	node tail;
	int nodes;
};

/* create new (empty) list */
list list_new( void ) {
	list lst;
	lst = (list )malloc( sizeof( struct stlist ));
	if ( lst == NULL ) {
		return lst;
	}
	lst->head = NULL;
	lst->tail = NULL;
	lst->nodes = 0;
	return lst;
}

/* create new node */
node list_new_node( void *data ) {
	node n;
	n = (node )malloc( sizeof( struct stnode ));
	if ( n == NULL ) {
		return n;
	}
	n->data = data;
	n->next = NULL;
	n->prev = NULL;
	return n;
}

/* add node to list */
node list_add( list lst, node n ) {

	/* void list or node */
	if (( lst == NULL ) || ( n == NULL )) {
		return NULL;
	}

	/* list is empty */
	if ( lst->head == NULL ) {
		lst->head = n;
		lst->tail = n;
	}

	/* list is _not_ empty */
	else {
		lst->tail->next = n;
		n->prev = lst->tail;
		lst->tail = n;
	}

	lst->nodes++;
	return n;
}

/* search for a node */
node list_search( list lst, void *data ) {

	node nitr;

	/* check if list is null or empty */
	if (( lst == NULL ) || ( lst->head == NULL )) {
		return NULL;
	}

	/* look for node between first and last nodes (first and last are included) */
	for ( nitr = lst->head; nitr != NULL; nitr = nitr->next ) {
		if ( nitr->data == data ) {
			/* node found, return it */
			return nitr;
		}
	}

	/* node not found */
	return NULL;
}

/* remove node from list */
void list_remove( list lst, node n ) {

	node nitr;

	/* check if list or node are null and if list is empty */
	if (( lst == NULL ) || ( n == NULL ) || ( lst->head == NULL )) {
		return;
	}

	/* check if node is list's head */
	if ( lst->head == n ) {
		lst->head = n->next;
		/* if so, also check if it's the only node in the list */
		if ( lst->tail == n ) {
			lst->tail = n->next;
		}
		else {
			lst->head->prev = NULL;
		}
		free( n );
		lst->nodes--;
		return;
	}

	/* look for node between first and last nodes (first and last are excluded) */
	for ( nitr = lst->head->next; nitr != lst->tail; nitr = nitr->next ) {
		if ( nitr == n ) {
			n->prev->next = n->next;
			n->next->prev = n->prev;
			free( n );
			lst->nodes--;
			return;
		}
	}

	/* check if node is list's tail */
	if ( lst->tail == n ) {
		lst->tail = n->prev;
		n->prev->next = n->next;
		free( n );
		lst->nodes--;
		return;
	}

	return;
}

/* list_destroy */
void list_destroy( list lst ) {

	/* empty list */
	if ( lst == NULL ) {
		return;
	}

	/* non-empty list */
	while ( lst->head != NULL ) {
		list_remove( lst, lst->head );
	}

	free( lst );
}

/* return list's first node */
node list_head( list lst ) {
	if ( lst != NULL ) {
		return lst->head;
	}
	return NULL;
}

/* return node's next node */
node list_next( node n ) {
	if ( n != NULL ) {
		return n->next;
	}
	return NULL;
}

/* return a node's data */
void *list_data( node n ) {
	if ( n != NULL ) {
		return n->data;
	}
	return NULL;
}

/* pop the head node from the list */
node list_pop_head( list lst ) {
	node ntmp;
	if (( lst == NULL ) || ( lst->head == NULL )) {
		return NULL;
	}

	ntmp = lst->head;
	if ( lst->head == lst->tail ) {
		lst->head = NULL;
		lst->tail = NULL;
	} else {
		lst->head = ntmp->next;
		ntmp->next->prev = NULL;
	}
	ntmp->next = NULL;
	ntmp->prev = NULL;
	lst->nodes--;
	return ntmp;
}

/* destroy a node */
void list_destroy_node( node n ) {
	free( n );
}

/* return a list's node count */
int list_node_count( list lst ) {
	if ( lst != NULL ) {
		return lst->nodes;
	}
	return LIST_COUNT_ERROR;
}

