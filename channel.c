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

[channel.c]

****************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "channel.h"
#include "list.h"

/* global channel lua_State mutex */
pthread_mutex_t mutex_channel_lstate = PTHREAD_MUTEX_INITIALIZER;

/* global lua_State where channel hash table will be stored */
lua_State *chanls = NULL;

/* message channel */
struct stchannel {
	list send;
	list recv;
	pthread_mutex_t *mutex;
	pthread_cond_t *in_use;
};

/* initialize channel table */
void channel_init( void ) {
	chanls = luaL_newstate();
	lua_newtable( chanls );
	lua_setglobal( chanls, "channeltb" );
}

/* create new channel */
channel channel_create( const char *cname ) {

	channel chan;

	/* get exclusive access to the channel table */
	pthread_mutex_lock( &mutex_channel_lstate );

	/* create a new channel */
	lua_getglobal( chanls, "channeltb");
	lua_pushstring( chanls, cname );
	chan = (channel )lua_newuserdata( chanls, sizeof( struct stchannel ));
	chan->send   = list_new();
	chan->recv   = list_new();
	chan->mutex  = (pthread_mutex_t *)malloc( sizeof( pthread_mutex_t ));
	pthread_mutex_init( chan->mutex, NULL );
	chan->in_use = (pthread_cond_t *)malloc( sizeof( pthread_cond_t ));
	pthread_cond_init( chan->in_use, NULL );
	lua_settable( chanls, -3 );
	lua_pop( chanls, 1 );

	/* let others access the channel table */
	pthread_mutex_unlock( &mutex_channel_lstate );

	return chan;
}

/* destroy a channel */
int channel_destroy( channel chan, const char *chname ) {

	/* get exclusive access to the channel table */
	pthread_mutex_lock( &mutex_channel_lstate );

	list_destroy( chan->send );
	list_destroy( chan->recv );

	lua_getglobal( chanls, "channeltb");
	lua_pushstring( chanls, chname );
	lua_pushnil( chanls );
	lua_settable( chanls, -3 );
	lua_pop( chanls, 1 );

	/* let others access the channel table */
	pthread_mutex_unlock( &mutex_channel_lstate );

	return CHANNEL_DESTROYED;
}

/* search for and return a channel with a given name */
channel channel_search( const char *cname ) {

	channel chan;

	/* get exclusive access to the channel table */
	pthread_mutex_lock( &mutex_channel_lstate );

	/* search for channel */
	lua_getglobal( chanls, "channeltb");
	lua_getfield( chanls, -1, cname );
	if (( lua_type( chanls, -1 )) == LUA_TUSERDATA ) {
		chan = (channel )lua_touserdata( chanls, -1 );
	} else {
		chan = NULL;
	}
	lua_pop( chanls, 2 );

	/* let others access channel table */
	pthread_mutex_unlock( &mutex_channel_lstate );

	return chan;
}

/* return a channel's send queue */
list channel_get_sendq( channel chan ) {
	return chan->send;
}

/* return a channel's receive queue */
list channel_get_recvq( channel chan ) {
	return chan->recv;
}

/* return a channel's mutex */
pthread_mutex_t *channel_get_mutex( channel chan ) {
	return chan->mutex;
}

/* return a channel's conditional variable */
pthread_cond_t *channel_get_cond( channel chan ) {
	return chan->in_use;
}

