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

[luaproc.h]

****************************************************/
#ifndef _LUAPROC_H_
#define _LUAPROC_H_

#include "channel.h"

/* process is idle */
#define LUAPROC_STAT_IDLE			0
/* process is ready to run */
#define LUAPROC_STAT_READY			1
/* process is blocked on send */
#define LUAPROC_STAT_BLOCKED_SEND	2
/* process is blocked on receive */
#define LUAPROC_STAT_BLOCKED_RECV	3
/* process is finished */
#define LUAPROC_STAT_FINISHED		4

/* lua process pointer type */
typedef struct stluaproc *luaproc;

/* return a process' status */
int luaproc_get_status( luaproc lp );

/* set a process' status */
void luaproc_set_status( luaproc lp, int status );

/* return a process' state */
lua_State *luaproc_get_state( luaproc lp );

/* return the number of arguments expected by a given a process */
int luaproc_get_args( luaproc lp );

/* set the number of arguments expected by a given process */
void luaproc_set_args( luaproc lp, int n );

/* create luaproc (from scheduler) */
luaproc luaproc_create_sched( char *code );

/* register luaproc's functions in a lua_State */
void luaproc_register_funcs( lua_State *L );

/* allow registering of luaproc's functions in c main prog */
void luaproc_register_lib( lua_State *L ); 

/* queue a luaproc that tried to send a message */
void luaproc_queue_sender( luaproc lp );
 
/* queue a luaproc that tried to receive a message */
void luaproc_queue_receiver( luaproc lp ); 

/* unlock a channel's access */
void luaproc_unlock_channel( channel chan );

/* return a luaproc's channel */
channel luaproc_get_channel( luaproc lp );

/* return status (boolean) indicating if worker thread should be destroyed after luaproc execution */
int luaproc_get_destroyworker( luaproc lp );

/* return status (boolean) indicating if lua process should be recycled */
luaproc luaproc_recycle_pop( void );

/* add a lua process to the recycle list */
int luaproc_recycle_push( luaproc lp );

#endif
