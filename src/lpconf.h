/*
 ** Kernel includes and settings
 ** Supported kernels: NetBSD, Linux
 */

#ifndef _LPCONF_H_
#define _LPCONF_H_

#if defined(__NetBSD__)
#include <sys/lua.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/kthread.h>
#include <sys/condvar.h>
#include <sys/mutex.h>
#include <sys/lwp.h>

#define luaL_newstate() lua_newstate(lua_alloc, NULL)

#elif defined(__linux__)
#include <linux/kthread.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/completion.h>
#define printf printk
#else
#error Not Supported
#endif
#endif
