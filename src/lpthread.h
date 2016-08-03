#ifndef _LUA_LUAPROC_CONF_H_
#define _LUA_LUAPROC_CONF_H_

#ifdef LUAPROC_USE_PTHREADS
#ifdef __unix__
#include <unistd.h>
#if defined _POSIX_THREADS
#include <pthread.h>
#define lpthread_cond_t     pthread_cond_t
#define lpthread_mutex_t    pthread_mutex_t
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

#else
#error Not Supported
#endif
#endif
#endif

#ifdef LUAPROC_USE_KTHREADS

#endif
#endif
