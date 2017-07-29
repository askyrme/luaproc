/*
 ** Threads API layer
 ** Supported Kernels: NetBSD, Linux
 */

#ifndef _LUA_LUAPROC_CONF_H_
#define _LUA_LUAPROC_CONF_H_

#ifdef LUAPROC_USE_PTHREADS
#ifdef __unix__
#include <unistd.h>
#if defined (_POSIX_THREADS)
#include <pthread.h>
#define TRYLOCK_SUCCESS 0
#define lpthread_cond_t pthread_cond_t
#define lpthread_mutex_t pthread_mutex_t
#define lpthread_t pthread_t

#define lpthread_create pthread_create
#define lpthread_exit pthread_exit
#define lpthread_cond_init pthread_cond_init
#define lpthread_cond_wait pthread_cond_wait
#define lpthread_cond_signal pthread_cond_signal
#define lpthread_cond_broadcast pthread_cond_broadcast
#define lpthread_cond_destroy pthread_cond_destroy
#define lpthread_mutex_init pthread_mutex_init
#define lpthread_mutex_lock pthread_mutex_lock
#define lpthread_mutex_trylock pthread_mutex_trylock
#define lpthread_mutex_unlock pthread_mutex_unlock
#define lpthread_mutex_destroy pthread_mutex_destroy
#define lpthread_join pthread_join
#define lpthread_self pthread_self

#else
#error Not Supported
#endif /* _POSIX_THREADS */
#endif /* __unix__ */
#endif /* LUAPROC_USE_PTHREADS */

#ifdef LUAPROC_USE_KTHREADS
#if defined(__NetBSD__)
#define TRYLOCK_SUCCESS 1
#include "lpconf.h"
#define lpthread_t lwp_t
#define lpthread_mutex_t kmutex_t
#define lpthread_cond_t kcondvar_t

#define lpthread_create( thread, attr, routine, arg ) kthread_create( PRI_KTHREAD, KTHREAD_MPSAFE|KTHREAD_MUSTJOIN, NULL, routine, arg, thread, "luaproc" )
#define lpthread_join( thread, unused ) kthread_join( thread )
#define lpthread_exit kthread_exit
#define lpthread_mutex_init( mutex, unused ) mutex_init( mutex, MUTEX_DEFAULT, IPL_NONE )
#define lpthread_mutex_destroy mutex_destroy
#define lpthread_mutex_lock mutex_enter
#define lpthread_mutex_trylock mutex_tryenter
#define lpthread_mutex_unlock mutex_exit
#define lpthread_cond_init( condvar, unused ) cv_init( condvar, "luaproc" )
#define lpthread_cond_destroy cv_destroy
#define lpthread_cond_wait cv_wait
#define lpthread_cond_signal cv_signal
#define lpthread_cond_broadcast cv_broadcast
#define lpthread_self curlwp

#elif defined(__linux__)
#include "lpconf.h"
#define TRYLOCK_SUCCESS 1
#define lpthread_t struct task_struct*
#define lpthread_mutex_t struct mutex
#define lpthread_cond_t struct completion

#define lpthread_create( routine, arg, wc ) kthread_run( routine, arg, "luaproc%d", wc )
#define lpthread_join( thread, unused ) kthread_stop( thread )
#define lpthread_exit( value ) return (value)
#define lpthread_mutex_init( mutex, unused ) mutex_init( mutex )
#define lpthread_mutex_destroy mutex_destroy
#define lpthread_mutex_lock mutex_lock
#define lpthread_mutex_trylock mutex_trylock
#define lpthread_mutex_unlock mutex_unlock

#define cond_wait( x, m ) do { \
   lpthread_mutex_unlock( m ); \
   wait_for_completion_interruptible( x );   \
   lpthread_mutex_lock( m );   \
} while (0)

#define lpthread_cond_init( condvar, unused ) init_completion( condvar )
#define lpthread_cond_destroy( unused ) ( ( void ) unused )
#define lpthread_cond_wait cond_wait
#define lpthread_cond_signal complete
#define lpthread_cond_broadcast complete_all
#define lpthread_self current

#else
#error Not Supported
#endif /* __NetBSD__ */
#endif /* LUAPROC_USE_KTHREADS */

#endif /* _LUA_LUAPROC_CONF_H_ */
