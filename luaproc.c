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

[luaproc.c]

****************************************************/

#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "luaproc.h"
#include "list.h"
#include "sched.h"
#include "channel.h"

#define FALSE 0
#define TRUE  1

/*********
* globals
*********/

/* channel operations mutex */
pthread_mutex_t mutex_channel = PTHREAD_MUTEX_INITIALIZER;

/* recycle list mutex */
pthread_mutex_t mutex_recycle_list = PTHREAD_MUTEX_INITIALIZER;

/* recycled lua process list */
list recyclelp = NULL;

/* maximum lua processes to recycle */
int recyclemax = 0;

/* lua process */
struct stluaproc {
	lua_State *lstate;
	int stat;
	int args;
	channel chan;
	int destroyworker;
};

/******************************
* library functions prototypes 
******************************/
/* create a new lua process */
static int luaproc_create_newproc( lua_State *L );
/* send a message to a lua process */
static int luaproc_send( lua_State *L );
/* receive a message from a lua process */
static int luaproc_receive( lua_State *L );
/* create a new channel */
static int luaproc_create_channel( lua_State *L );
/* destroy a channel */
static int luaproc_destroy_channel( lua_State *L );
/* wait until all luaprocs have finished and exit */
static int luaproc_exit( lua_State *L );
/* create a new worker */
static int luaproc_create_worker( lua_State *L );
/* destroy a worker */
static int luaproc_destroy_worker( lua_State *L );
/* set amount of lua processes that should be recycled (ie, reused) */
static int luaproc_recycle_set( lua_State *L );

/* luaproc function registration array - main (parent) functions */
static const struct luaL_reg luaproc_funcs_parent[] = {
	{ "newproc", luaproc_create_newproc },
	{ "exit", luaproc_exit },
	{ "createworker", luaproc_create_worker },
	{ "destroyworker", luaproc_destroy_worker },
	{ "recycle", luaproc_recycle_set },
	{ NULL, NULL }
};

/* luaproc function registration array - newproc (child) functions */
static const struct luaL_reg luaproc_funcs_child[] = {
	{ "newproc", luaproc_create_newproc },
	{ "send", luaproc_send },
	{ "receive", luaproc_receive },
	{ "newchannel", luaproc_create_channel },
	{ "delchannel", luaproc_destroy_channel },
	{ "createworker", luaproc_create_worker },
	{ "destroyworker", luaproc_destroy_worker },
	{ "recycle", luaproc_recycle_set },
	{ NULL, NULL }
};

static void registerlib( lua_State *L, const char *name, lua_CFunction f ) {
	lua_getglobal( L, "package" );
	lua_getfield( L, -1, "preload" );
	lua_pushcfunction( L, f );
	lua_setfield( L, -2, name );
	lua_pop( L, 2 );
}

static void openlibs( lua_State *L ) {
	lua_cpcall( L, luaopen_base, NULL );
	lua_cpcall( L, luaopen_package, NULL );
	registerlib( L, "io", luaopen_io );
	registerlib( L, "os", luaopen_os );
	registerlib( L, "table", luaopen_table );
	registerlib( L, "string", luaopen_string );
	registerlib( L, "math", luaopen_math );
	registerlib( L, "debug", luaopen_debug );
}

/* return status (boolean) indicating if lua process should be recycled */
luaproc luaproc_recycle_pop( void ) {

	luaproc lp;
	node n;

	/* get exclusive access to operate on recycle list */
	pthread_mutex_lock( &mutex_recycle_list );

	/* check if there are any lua processes on recycle list */
	if ( list_node_count( recyclelp ) > 0 ) {
		/* pop list head */
		n = list_pop_head( recyclelp );
		/* free access to operate on recycle list */
		pthread_mutex_unlock( &mutex_recycle_list );
		/* find associated luaproc */
		lp = (luaproc )list_data( n );
		/* destroy node (but not associated luaproc) */
		list_destroy_node( n );
		/* return associated luaproc */
		return lp; 
	}

	/* free access to operate on recycle list */
	pthread_mutex_unlock( &mutex_recycle_list );

	/* if no lua processes are available simply return null */
	return NULL;
}

/* check if lua process should be recycled and, in case so, add it to the recycle list */
int luaproc_recycle_push( luaproc lp ) {

	node n;

	/* get exclusive access to operate on recycle list */
	pthread_mutex_lock( &mutex_recycle_list );

	/* check if amount of lua processes currently on recycle list is greater than
	   or equal to the maximum amount of lua processes that should be recycled */
	if ( list_node_count( recyclelp ) >= recyclemax ) {
		/* free access to operate on recycle list */
		pthread_mutex_unlock( &mutex_recycle_list );
		/* if so, lua process should NOT be recycled and should be destroyed */
		return FALSE;
	}
	/* otherwise, lua process should be added to recycle list */
	n = list_new_node( lp );
	if ( n == NULL ) {
		/* free access to operate on recycle list */
		pthread_mutex_unlock( &mutex_recycle_list );
		/* in case of errors, lua process should be destroyed */
		return FALSE;
	}
	list_add( recyclelp, n );
	/* free access to operate on recycle list */
	pthread_mutex_unlock( &mutex_recycle_list );
	/* since lua process will be recycled, it should not be destroyed */
	return TRUE;
}

/* create new luaproc */
luaproc luaproc_new( const char *code, int destroyflag ) {

	luaproc lp;
	int ret;
	/* create new lua state */
	lua_State *lpst = luaL_newstate( );
	/* store the luaproc struct in its own lua state */
	lp = (luaproc )lua_newuserdata( lpst, sizeof( struct stluaproc ));
	lua_setfield( lpst, LUA_REGISTRYINDEX, "_SELF" );

	lp->lstate = lpst;
	lp->stat = LUAPROC_STAT_IDLE;
	lp->args = 0;
	lp->chan = NULL;
	lp->destroyworker = destroyflag;

	/* load standard libraries */
	openlibs( lpst );

	/* register luaproc's own functions */
	luaL_register( lpst, "luaproc", luaproc_funcs_child );

	/* load process' code */
	ret = luaL_loadstring( lpst, code );
	/* in case of errors, destroy recently created lua process */
	if ( ret != 0 ) {
		lua_close( lpst );
		return NULL;
	}

	/* return recently created lua process */
	return lp;
}

/* synchronize worker threads and exit */
static int luaproc_exit( lua_State *L ) {
	sched_join_workerthreads( );
	return 0;
}

/* create a new worker pthread */
static int luaproc_create_worker( lua_State *L ) {

	if ( sched_create_worker( ) != LUAPROC_SCHED_OK ) {
		lua_pushnil( L );
		lua_pushstring( L, "error creating worker" );
		return 2;
	}

	lua_pushboolean( L, TRUE );
	return 1;
}

/* set amount of lua processes that should be recycled (ie, reused) */
static int luaproc_recycle_set( lua_State *L ) {

	node n;
	luaproc lp;
	int max = luaL_checkint( L, 1 );

	/* check if function argument represents a reasonable value */
	if ( max < 0 ) {
		/* in case of errors return nil + error msg */
		lua_pushnil( L );
		lua_pushstring( L, "error setting recycle limit to negative value" );
		return 2;
	}

	/* get exclusive access to operate on recycle list */
	pthread_mutex_lock( &mutex_recycle_list );

	/* set maximum lua processes that should be recycled */
	recyclemax = max;

	/* destroy recycle list excessive nodes (and corresponding lua processes) */
	while ( list_node_count( recyclelp ) > max ) {
		/* get first node from recycle list */
		n = list_pop_head( recyclelp );
		/* find associated luaproc */
		lp = (luaproc )list_data( n );
		/* destroy node */
		list_destroy_node( n );
		/* close associated lua_State */
		lua_close( lp->lstate );
	}

	/* free access to operate on recycle list */
	pthread_mutex_unlock( &mutex_recycle_list );

	lua_pushboolean( L, TRUE );
	return 1;
}


/* destroy a worker pthread */
static int luaproc_destroy_worker( lua_State *L ) {

	/* new lua process pointer */
	luaproc lp;

	/* create new lua process with empty code and destroy worker flag set to true
	   (ie, conclusion of lua process WILL result in worker thread destruction */
	lp = luaproc_new( "", TRUE );

	/* ensure process creation was successfull */
	if ( lp == NULL ) {
		/* in case of errors return nil + error msg */
		lua_pushnil( L );
		lua_pushstring( L, "error destroying worker" );
		return 2;
	}

	/* increase active luaproc count */
	sched_lpcount_inc();

	/* schedule luaproc */
	if ( sched_queue_proc( lp ) != LUAPROC_SCHED_QUEUE_PROC_OK ) {
		printf( "[luaproc] error queueing Lua process\n" );
		/* decrease active luaproc count */
		sched_lpcount_dec();
		/* close lua_State */
		lua_close( lp->lstate );
		/* return nil + error msg */
		lua_pushnil( L );
		lua_pushstring( L, "error destroying worker" );
		return 2;
	}

	lua_pushboolean( L, TRUE );
	return 1;
}

/* recycle a lua process */
luaproc luaproc_recycle( luaproc lp, const char *code ) {

	int ret;

	/* reset struct members */
	lp->stat = LUAPROC_STAT_IDLE;
	lp->args = 0;
	lp->chan = NULL;
	lp->destroyworker = FALSE;

	/* load process' code */
	ret = luaL_loadstring( lp->lstate, code );

	/* in case of errors, destroy lua process */
	if ( ret != 0 ) {
		lua_close( lp->lstate );
		return NULL;
	}

	/* return recycled lua process */
	return lp;
}

/* create and schedule a new lua process (luaproc.newproc) */
static int luaproc_create_newproc( lua_State *L ) {

	/* check if first argument is a string (lua code) */
	const char *code = luaL_checkstring( L, 1 );

	/* new lua process pointer */
	luaproc lp;

	/* check if existing lua process should be recycled to avoid new creation */
	lp = luaproc_recycle_pop( );

	/* if there is a lua process available on the recycle queue, recycle it */
	if ( lp != NULL ) {
		lp = luaproc_recycle( lp, code );
	}
	/* otherwise create a new one from scratch */
	else {
		/* create new lua process with destroy worker flag set to false
		   (ie, conclusion of lua process will NOT result in worker thread destruction */
		lp = luaproc_new( code, FALSE );
	}

	/* ensure process creation was successfull */
	if ( lp == NULL ) {
		/* in case of errors return nil + error msg */
		lua_pushnil( L );
		lua_pushstring( L, "error loading code string" );
		return 2;
	}

	/* increase active luaproc count */
	sched_lpcount_inc();

	/* schedule luaproc */
	if ( sched_queue_proc( lp ) != LUAPROC_SCHED_QUEUE_PROC_OK ) {
		printf( "[luaproc] error queueing Lua process\n" );
		/* decrease active luaproc count */
		sched_lpcount_dec();
		/* close lua_State */
		lua_close( lp->lstate );
		/* return nil + error msg */
		lua_pushnil( L );
		lua_pushstring( L, "error queuing process" );
		return 2;
	}

	lua_pushboolean( L, TRUE );
	return 1;
}

/* queue a lua process sending a message without a matching receiver */
void luaproc_queue_sender( luaproc lp ) {
	/* add the sending process to this process' send queue */
	list_add( channel_get_sendq( lp->chan ), list_new_node( lp ));
}

/* dequeue a lua process sending a message with a receiver match */
luaproc luaproc_dequeue_sender( channel chan ) {

	node n;
	luaproc lp;

	if ( list_node_count( channel_get_sendq( chan )) > 0 ) {
		/* get first node from channel's send queue */
		n = list_pop_head( channel_get_sendq( chan ));
		/* find associated luaproc */
		lp = (luaproc )list_data( n );
		/* destroy node (but not associated luaproc) */
		list_destroy_node( n );
		/* return associated luaproc */
		return lp;
	}

	return NULL;
}

/* queue a luc process receiving a message without a matching sender */
void luaproc_queue_receiver( luaproc lp ) {
	/* add the receiving process to this process' receive queue */
	list_add( channel_get_recvq( lp->chan ), list_new_node( lp )); 
}

/* dequeue a lua process receiving a message with a sender match */
luaproc luaproc_dequeue_receiver( channel chan ) {

	node n;
	luaproc lp;

	if ( list_node_count( channel_get_recvq( chan )) > 0 ) {
		/* get first node from channel's recv queue */
		n = list_pop_head( channel_get_recvq( chan ));
		/* find associated luaproc */
		lp = (luaproc )list_data( n );
		/* destroy node (but not associated luaproc) */
		list_destroy_node( n );
		/* return associated luaproc */
		return lp;
	}

	return NULL;
}

/* moves values between lua states' stacks */
void luaproc_movevalues( lua_State *Lfrom, lua_State *Lto ) {

	int i;
	int n = lua_gettop( Lfrom );

	/* move values between lua states' stacks */
	for ( i = 2; i <= n; i++ ) {
		lua_pushstring( Lto, lua_tostring( Lfrom, i ));
	}
}

/* return the lua process associated with a given lua state */
luaproc luaproc_getself( lua_State *L ) {
	luaproc lp;
	lua_getfield( L, LUA_REGISTRYINDEX, "_SELF" );
	lp = (luaproc )lua_touserdata( L, -1 );
	lua_pop( L, 1 );
	return lp;
}

/* send a message to a lua process */
static int luaproc_send( lua_State *L ) {

	channel chan;
	luaproc dstlp, self;
	const char *chname = luaL_checkstring( L, 1 );

	/* get exclusive access to operate on channels */
	pthread_mutex_lock( &mutex_channel );

	/* wait until channel is not in use */
	while((( chan = channel_search( chname )) != NULL ) && ( pthread_mutex_trylock( channel_get_mutex( chan )) != 0 )) {
		pthread_cond_wait( channel_get_cond( chan ), &mutex_channel );
	}

	/* free access to operate on channels */
	pthread_mutex_unlock( &mutex_channel );

	/* if channel is not found, return an error to Lua */
	if ( chan == NULL ) {
		lua_pushnil( L );
		lua_pushstring( L, "non-existent channel" );
		return 2;
	}

	/* try to find a matching receiver */
	dstlp = luaproc_dequeue_receiver( chan );

	/* if a match is found, move values to it and (queue) wake it */
	if ( dstlp != NULL ) {

		/* move values between Lua states' stacks */
		luaproc_movevalues( L, dstlp->lstate );

		dstlp->args = lua_gettop( dstlp->lstate ) - 1;
	
		if ( sched_queue_proc( dstlp ) != LUAPROC_SCHED_QUEUE_PROC_OK ) {

			/* unlock channel access */
			luaproc_unlock_channel( chan );

			/* decrease active luaproc count */
			sched_lpcount_dec();

			/* close lua_State */
			lua_close( dstlp->lstate );
			lua_pushnil( L );
			lua_pushstring( L, "error scheduling process" );
			return 2;
		}

		/* unlock channel access */
		luaproc_unlock_channel( chan );
	}

	/* otherwise queue (block) the sending process */
	else {

		self = luaproc_getself( L );

		if ( self != NULL ) {
			self->stat = LUAPROC_STAT_BLOCKED_SEND;
			self->chan = chan;
		}

		/* just yield the lua process, channel unlocking will be done by the scheduler */
		return lua_yield( L, lua_gettop( L ));
	}

	lua_pushboolean( L, TRUE );
	return 1;
}

/* receive a message from a lua process */
static int luaproc_receive( lua_State *L ) {

	channel chan;
	luaproc srclp, self;
	const char *chname = luaL_checkstring( L, 1 );

	/* get exclusive access to operate on channels */
	pthread_mutex_lock( &mutex_channel );

	/* wait until channel is not in use */
	while((( chan = channel_search( chname )) != NULL ) && ( pthread_mutex_trylock( channel_get_mutex( chan )) != 0 )) {
		pthread_cond_wait( channel_get_cond( chan ), &mutex_channel );
	}

	/* free access to operate on channels */
	pthread_mutex_unlock( &mutex_channel );

	/* if channel is not found, free access to operate on channels and return an error to Lua */
	if ( chan == NULL ) {
		lua_pushnil( L );
		lua_pushstring( L, "non-existent channel" );
		return 2;
	}

	/* try to find a matching sender */
	srclp = luaproc_dequeue_sender( chan );

	/* if a match is found, get values from it and (queue) wake it */
	if ( srclp != NULL ) {

		/* move values between Lua states' stacks */
		luaproc_movevalues( srclp->lstate, L );

		/* return to sender indicanting message was sent */
		lua_pushboolean( srclp->lstate, TRUE );
		srclp->args = 1;

		if ( sched_queue_proc( srclp ) != LUAPROC_SCHED_QUEUE_PROC_OK ) {

			/* unlock channel access */
			luaproc_unlock_channel( chan );

			/* decrease active luaproc count */
			sched_lpcount_dec();

			/* close lua_State */
			lua_close( srclp->lstate );
			lua_pushnil( L );
			lua_pushstring( L, "error scheduling process" );
			return 2;
		}

		/* unlock channel access */
		luaproc_unlock_channel( chan );

		return lua_gettop( L ) - 1;
	}

	/* otherwise queue (block) the receiving process (sync) or return immediatly (async) */
	else {

		/* if trying an asynchronous receive, unlock channel access and return an error */
		if ( lua_toboolean( L, 2 )) {
			/* unlock channel access */
			luaproc_unlock_channel( chan );
			/* return an error */
			lua_pushnil( L );
			lua_pushfstring( L, "no senders waiting on channel %s", chname );
			return 2;
		}

		/* otherwise (synchronous receive) simply block process */
		else {
			self = luaproc_getself( L );

			if ( self != NULL ) {
				self->stat = LUAPROC_STAT_BLOCKED_RECV;
				self->chan = chan;
			}

			/* just yield the lua process, channel unlocking will be done by the scheduler */
			return lua_yield( L, lua_gettop( L ));
		}
	}
}

LUALIB_API int luaopen_luaproc( lua_State *L ) {

	/* register luaproc functions */
	luaL_register( L, "luaproc", luaproc_funcs_parent );

	/* initialize recycle list */
	recyclelp = list_new();

	/* initialize local scheduler */
	sched_init_local( LUAPROC_SCHED_DEFAULT_WORKER_THREADS );

	return 0;
}

/* return a process' status */
int luaproc_get_status( luaproc lp ) {
	return lp->stat;
}

/* set a process' status */
void luaproc_set_status( luaproc lp, int status ) {
	lp->stat = status;
}

/* return a process' state */
lua_State *luaproc_get_state( luaproc lp ) {
	return lp->lstate;
}

/* return the number of arguments expected by a given process */
int luaproc_get_args( luaproc lp ) {
	return lp->args;
}

/* set the number of arguments expected by a given process */
void luaproc_set_args( luaproc lp, int n ) {
	lp->args = n;
}

/* create a new channel */
static int luaproc_create_channel( lua_State *L ) {

	const char *chname = luaL_checkstring( L, 1 );

	/* get exclusive access to operate on channels */
	pthread_mutex_lock( &mutex_channel );

	/* check if channel exists */
	if ( channel_search( chname ) != NULL ) {
		/* free access to operate on channels */
		pthread_mutex_unlock( &mutex_channel );
		/* return an error to lua */
		lua_pushnil( L );
		lua_pushstring( L, "channel already exists" );
		return 2;
	}

	channel_create( chname );

	/* free access to operate on channels */
	pthread_mutex_unlock( &mutex_channel );

	lua_pushboolean( L, TRUE );

	return 1;

}

/* destroy a channel */
static int luaproc_destroy_channel( lua_State *L ) {

	channel chan;
	luaproc lp;
	node nitr;
	pthread_mutex_t *chmutex;
	pthread_cond_t *chcond;
	const char *chname = luaL_checkstring( L, 1 );
	

	/* get exclusive access to operate on channels */
	pthread_mutex_lock( &mutex_channel );

	/* wait until channel is not in use */
	while((( chan = channel_search( chname )) != NULL ) && ( pthread_mutex_trylock( channel_get_mutex( chan )) != 0 )) {
		pthread_cond_wait( channel_get_cond( chan ), &mutex_channel );
	}

	/* free access to operate on channels */
	pthread_mutex_unlock( &mutex_channel );

	/* if channel is not found, return an error to Lua */
	if ( chan == NULL ) {
		lua_pushnil( L );
		lua_pushstring( L, "non-existent channel" );
		return 2;
	}

	/* get channel's mutex and conditional pointers */
	chmutex = channel_get_mutex( chan );
	chcond  = channel_get_cond( chan );

	/* search for processes waiting to send a message on this channel */
	while (( nitr = list_pop_head( channel_get_sendq( chan ))) != NULL ) {

		lp = (luaproc )list_data( nitr );

		/* destroy node (but not associated luaproc) */
		list_destroy_node( nitr );

		/* return an error so the processe knows the channel was destroyed before the message was sent */
		lua_settop( lp->lstate, 0 );
		lua_pushnil( lp->lstate );
		lua_pushstring( lp->lstate, "channel destroyed while waiting for receiver" );
		lp->args = 2;

		/* schedule the process for execution */
		if ( sched_queue_proc( lp ) != LUAPROC_SCHED_QUEUE_PROC_OK ) {

			/* decrease active luaproc count */
			sched_lpcount_dec();

			/* close lua_State */
			lua_close( lp->lstate );
		}
	}

	/* search for processes waiting to receive a message on this channel */
	while (( nitr = list_pop_head( channel_get_recvq( chan ))) != NULL ) {

		lp = (luaproc )list_data( nitr );

		/* destroy node (but not associated luaproc) */
		list_destroy_node( nitr );

		/* return an error so the processe knows the channel was destroyed before the message was received */
		lua_settop( lp->lstate, 0 );
		lua_pushnil( lp->lstate );
		lua_pushstring( lp->lstate, "channel destroyed while waiting for sender" );
		lp->args = 2;

		/* schedule the process for execution */
		if ( sched_queue_proc( lp ) != LUAPROC_SCHED_QUEUE_PROC_OK ) {

			/* decrease active luaproc count */
			sched_lpcount_dec();

			/* close lua_State */
			lua_close( lp->lstate );
		}
	}

	/* get exclusive access to operate on channels */
	pthread_mutex_lock( &mutex_channel );
	/* destroy channel */
	channel_destroy( chan, chname );
	/* broadcast channel not in use */
	pthread_cond_broadcast( chcond );
	/* unlock channel access */
	pthread_mutex_unlock( chmutex );
	/* destroy channel mutex and conditional */
	pthread_mutex_destroy( chmutex );
	pthread_cond_destroy( chcond );
	/* free memory used by channel mutex and conditional */
	free( chmutex );
	free( chcond );
	/* free access to operate on channels */
	pthread_mutex_unlock( &mutex_channel );

	lua_pushboolean( L, TRUE );

	return 1;
}

/* register luaproc's functions in a lua_State */
void luaproc_register_funcs( lua_State *L ) {
	luaL_register( L, "luaproc", luaproc_funcs_child );
}

/* return the channel where the corresponding luaproc is blocked at */
channel luaproc_get_channel( luaproc lp ) {
	return lp->chan;
}

/* unlock access to a channel */
void luaproc_unlock_channel( channel chan ) {
	/* get exclusive access to operate on channels */
	pthread_mutex_lock( &mutex_channel );
	/* unlock channel access */
	pthread_mutex_unlock( channel_get_mutex( chan ));
	/* signal channel not in use */
	pthread_cond_signal( channel_get_cond( chan ));
	/* free access to operate on channels */
	pthread_mutex_unlock( &mutex_channel );
}

/* return status (boolean) indicating if worker thread should be destroyed after luaproc execution */
int luaproc_get_destroyworker( luaproc lp ) {
	return lp->destroyworker;
}

