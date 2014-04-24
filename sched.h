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

[sched.h]

****************************************************/
#ifndef _LUAPROC_SCHED_H_
#define _LUAPROC_SCHED_H_

#include "luaproc.h"

/* scheduler function return constants */
#define	LUAPROC_SCHED_OK						0
#define LUAPROC_SCHED_SOCKET_ERROR				-1
#define LUAPROC_SCHED_SETSOCKOPT_ERROR			-2
#define LUAPROC_SCHED_BIND_ERROR				-3
#define LUAPROC_SCHED_LISTEN_ERROR				-4
#define LUAPROC_SCHED_FORK_ERROR				-5
#define LUAPROC_SCHED_PTHREAD_ERROR				-6
#define LUAPROC_SCHED_INIT_ERROR				-7

/* ready process queue insertion status */
#define LUAPROC_SCHED_QUEUE_PROC_OK		0
#define LUAPROC_SCHED_QUEUE_PROC_ERR	-1

/* scheduler listener service default hostname and port */
#define LUAPROC_SCHED_DEFAULT_HOST "127.0.0.1"
#define LUAPROC_SCHED_DEFAULT_PORT 3133

/* scheduler default number of worker threads */
#define LUAPROC_SCHED_DEFAULT_WORKER_THREADS	1

/* initialize local scheduler */
int sched_init_local( int numworkers ); 

/* initialize socket enabled scheduler */
int sched_init_socket( int numworkers, const char *host, int port ); 

/* exit scheduler */
void sched_exit( void );

/* move process to ready queue (ie, schedule process) */
int sched_queue_proc( luaproc lp );

/* join all worker threads and exit */
void sched_join_workerthreads( void ); 

/* increase active luaproc count */
void sched_lpcount_inc( void );

/* decrease active luaproc count */
void sched_lpcount_dec( void );

/* create a new worker pthread */
int sched_create_worker( void );

#endif
