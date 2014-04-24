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

[channel.h]

****************************************************/

#ifndef _LUAPROC_CHANNEL_H_
#define _LUAPROC_CHANNEL_H_

#include <pthread.h>

#include "list.h"

#define CHANNEL_MAX_NAME_LENGTH 255

#define CHANNEL_DESTROYED 0

/* message channel pointer type */
typedef struct stchannel *channel;

/* initialize channels */
void channel_init( void );

/* create new channel */
channel channel_create( const char *cname );

/* destroy a channel */
int channel_destroy( channel chan, const char *chname ); 

/* search for and return a channel with a given name */
channel channel_search( const char *cname );

/* return a channel's send queue */
list channel_get_sendq( channel chan );

/* return a channel's receive queue */
list channel_get_recvq( channel chan );

/* return a channel's mutex */
pthread_mutex_t *channel_get_mutex( channel chan ); 

/* return a channel's conditional variable */
pthread_cond_t *channel_get_cond( channel chan ); 

#endif
