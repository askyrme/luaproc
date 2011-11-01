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

[sched.c]

****************************************************/

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "list.h"
#include "sched.h"
#include "luaproc.h"
#include "channel.h"

#define TRUE	1
#define FALSE	0

/*********
* globals
*********/

/* ready process list */
list lpready = NULL;

/* ready process queue access mutex */
pthread_mutex_t mutex_queue_access = PTHREAD_MUTEX_INITIALIZER;

/* wake worker up conditional variable */
pthread_cond_t cond_wakeup_worker = PTHREAD_COND_INITIALIZER;

/* active luaproc count access mutex */
pthread_mutex_t mutex_lp_count = PTHREAD_MUTEX_INITIALIZER;

/* no active luaproc conditional variable */
pthread_cond_t cond_no_active_lp = PTHREAD_COND_INITIALIZER;

/* number of active luaprocs */
int lpcount = 0;

/* no more lua processes flag */
int no_more_processes = FALSE;

/* worker thread main function */
void *workermain( void *args ) {

	node n;
	luaproc lp;
	int procstat;
	int destroyworker;

	/* detach thread so resources are freed as soon as thread exits (no further joining) */
	pthread_detach( pthread_self( ));

	/* main worker loop */
	while ( 1 ) {

		/* get exclusive access to the ready process queue */
		pthread_mutex_lock( &mutex_queue_access );

		/* wait until instructed to wake up (because there's work to do or because its time to finish) */
		while (( list_node_count( lpready ) == 0 ) && ( no_more_processes == FALSE )) {
			pthread_cond_wait( &cond_wakeup_worker, &mutex_queue_access );
		}

		/* pop the first node from the ready process queue */
		n = list_pop_head( lpready );

		/* ensure list pop succeeded before proceeding */
		if ( n != NULL ) {
			/* get the popped node's data content (ie, the lua process struct) */
			lp = (luaproc )list_data( n );
		}
		else {
			/* free access to the process ready queue */
			pthread_mutex_unlock( &mutex_queue_access );
			/* finished thread */
			pthread_exit( NULL );
		}

		/* free access to the process ready queue */
		pthread_mutex_unlock( &mutex_queue_access );

		/* execute the lua code specified in the lua process struct */
		procstat = lua_resume( luaproc_get_state( lp ), luaproc_get_args( lp ));

		/* reset the process argument count */
		luaproc_set_args( lp, 0 );

		/* check if process finished its whole execution */
		if ( procstat == 0 ) {

			/* destroy the corresponding list node */
			list_destroy_node( n );

			/* check if worker thread should be destroyed */
			destroyworker = luaproc_get_destroyworker( lp );

			/* set process status to finished */
			luaproc_set_status( lp, LUAPROC_STAT_FINISHED );

			/* check if lua process should be recycled and, if not, destroy it */
			if ( luaproc_recycle_push( lp ) == FALSE ) {
				lua_close( luaproc_get_state( lp ));
			}

			/* decrease active lua process count */
			sched_lpcount_dec();

			/* check if thread should be finished after lua process conclusion */
			if ( destroyworker ) {
				/* if so, finish thread */
				pthread_exit( NULL );
			}
		}

		/* check if process yielded */
		else if ( procstat == LUA_YIELD ) {

			/* if so, further check if yield originated from an unmatched send/recv operation */
			if ( luaproc_get_status( lp ) == LUAPROC_STAT_BLOCKED_SEND ) {
				/* queue blocked lua process on corresponding channel */
				luaproc_queue_sender( lp );
				/* unlock channel access */
				luaproc_unlock_channel( luaproc_get_channel( lp ));
				/* destroy node (but not the associated Lua process) */
				list_destroy_node( n );
			}

			else if ( luaproc_get_status( lp ) == LUAPROC_STAT_BLOCKED_RECV ) {
				/* queue blocked lua process on corresponding channel */
				luaproc_queue_receiver( lp );
				/* unlock channel access */
				luaproc_unlock_channel( luaproc_get_channel( lp ));
				/* destroy node (but not the associated Lua process) */
				list_destroy_node( n );
			}

			/* or if yield resulted from an explicit call to coroutine.yield in the lua code being executed */
			else {
				/* get exclusive access to the ready process queue */
				pthread_mutex_lock( &mutex_queue_access );
				/* re-insert the job at the end of the ready process queue */
				list_add( lpready, n );
				/* free access to the process ready queue */
				pthread_mutex_unlock( &mutex_queue_access );
			}
		}

		/* check if there was any execution error (LUA_ERRRUN, LUA_ERRSYNTAX, LUA_ERRMEM or LUA_ERRERR) */
		else {
			/* destroy the corresponding node */
			list_destroy_node( n );
			/* print error message */
			fprintf( stderr, "close lua_State (error: %s)\n", luaL_checkstring( luaproc_get_state( lp ), -1 ));
			/* close lua state */
			lua_close( luaproc_get_state( lp ));
			/* decrease active lua process count */
			sched_lpcount_dec();
		}
	}
}

/* local scheduler initialization */
int sched_init_local( int numworkers ) {

	int tid;
	int workercount = 0;
	pthread_t worker;

	/* initialize ready process list */
	lpready = list_new();

	/* initialize channels */
	channel_init();

	/* create initial worker threads */
	for ( tid = 0; tid < numworkers; tid++ ) {
		if ( pthread_create( &worker, NULL, workermain, NULL ) == 0 ) {
			workercount++;
		}
	}

	if ( workercount != numworkers ) {
		return LUAPROC_SCHED_INIT_ERROR;
	}

	return LUAPROC_SCHED_OK;
}

/* exit scheduler */
void sched_exit( void ) {

	/* get exclusive access to the ready process queue */
	pthread_mutex_lock( &mutex_queue_access );
	/* destroy the ready process list */
	list_destroy( lpready );
	/* free access to the process ready queue */
	pthread_mutex_unlock( &mutex_queue_access );
}

/* move process to ready queue (ie, schedule process) */
int sched_queue_proc( luaproc lp ) {

	/* get exclusive access to the ready process queue */
	pthread_mutex_lock( &mutex_queue_access );

	/* add process to ready queue */
	if ( list_add( lpready, list_new_node( lp )) != NULL ) {

		/* set process status to ready */
		luaproc_set_status( lp, LUAPROC_STAT_READY );

		/* wake worker up */
		pthread_cond_signal( &cond_wakeup_worker );
		/* free access to the process ready queue */
		pthread_mutex_unlock( &mutex_queue_access ); 

		return LUAPROC_SCHED_QUEUE_PROC_OK;
	}
	else {
		/* free access to the process ready queue */
		pthread_mutex_unlock( &mutex_queue_access ); 

		return LUAPROC_SCHED_QUEUE_PROC_ERR;
	}	
}

/* synchronize worker threads */
void sched_join_workerthreads( void ) {

	pthread_mutex_lock( &mutex_lp_count );

	/* wait until there is no more active lua processes */
	while( lpcount != 0 ) {
		pthread_cond_wait( &cond_no_active_lp, &mutex_lp_count );
	}
	/* get exclusive access to the ready process queue */
	pthread_mutex_lock( &mutex_queue_access );
	/* set the no more active lua processes flag to true */
	no_more_processes = TRUE;
	/* wake ALL workers up */
	pthread_cond_broadcast( &cond_wakeup_worker );
	/* free access to the process ready queue */
	pthread_mutex_unlock( &mutex_queue_access );
	/* wait for (join) worker threads */
	pthread_exit( NULL );

	pthread_mutex_unlock( &mutex_lp_count );

}

/* increase active lua process count */
void sched_lpcount_inc( void ) {
	pthread_mutex_lock( &mutex_lp_count );
	lpcount++;
	pthread_mutex_unlock( &mutex_lp_count );
}

/* decrease active lua process count */
void sched_lpcount_dec( void ) {
	pthread_mutex_lock( &mutex_lp_count );
	lpcount--;
	/* if count reaches zero, signal there are no more active processes */
	if ( lpcount == 0 ) {
		pthread_cond_signal( &cond_no_active_lp );
	}
	pthread_mutex_unlock( &mutex_lp_count );
}

/* create a new worker pthread */
int sched_create_worker( void ) {

	pthread_t worker;

	/* create a new pthread */
	if ( pthread_create( &worker, NULL, workermain, NULL ) != 0 ) {
		return LUAPROC_SCHED_PTHREAD_ERROR;
	}

	return LUAPROC_SCHED_OK;
}

