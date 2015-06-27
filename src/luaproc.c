/*
** luaproc API
** See Copyright Notice in luaproc.h
*/

#include <pthread.h>
#include <stdlib.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <string.h>

#include "luaproc.h"
#include "lpsched.h"

#define FALSE 0
#define TRUE  !FALSE
#define LUAPROC_CHANNELS_TABLE "channeltb"
#define LUAPROC_RECYCLE_MAX 0

/********************
 * global variables *
 *******************/

/* channel list mutex */
static pthread_mutex_t mutex_channel_list = PTHREAD_MUTEX_INITIALIZER;

/* recycle list mutex */
static pthread_mutex_t mutex_recycle_list = PTHREAD_MUTEX_INITIALIZER;

/* recycled lua process list */
static list recycle_list;

/* maximum lua processes to recycle */
static int recyclemax = LUAPROC_RECYCLE_MAX;

/* lua_State used to store channel hash table */
static lua_State *chanls = NULL;

/* lua process used to wrap main state. this allows it to be queued in 
   channels when sending and receiving messages */
static luaproc mainlp;

/* main state matched a send/recv operation conditional variable */
pthread_cond_t cond_mainls_sendrecv = PTHREAD_COND_INITIALIZER;

/* main state communication mutex */
static pthread_mutex_t mutex_mainls = PTHREAD_MUTEX_INITIALIZER;

/***********************
 * register prototypes *
 ***********************/

static void luaproc_openlualibs( lua_State *L );
static int luaproc_create_newproc( lua_State *L );
static int luaproc_wait( lua_State *L );
static int luaproc_send( lua_State *L );
static int luaproc_receive( lua_State *L );
static int luaproc_create_channel( lua_State *L );
static int luaproc_destroy_channel( lua_State *L );
static int luaproc_set_numworkers( lua_State *L );
static int luaproc_get_numworkers( lua_State *L );
static int luaproc_recycle_set( lua_State *L );

/***********
 * structs *
 ***********/

/* lua process */
struct stluaproc {
  lua_State *lstate;
  int status;
  int args;
  channel *chan;
  luaproc *next;
};

/* communication channel */
struct stchannel {
  list send;
  list recv;
  pthread_mutex_t mutex;
  pthread_cond_t can_be_used;
};

/* luaproc function registration array */
static const struct luaL_Reg luaproc_funcs[] = {
  { "newproc", luaproc_create_newproc },
  { "wait", luaproc_wait },
  { "send", luaproc_send },
  { "receive", luaproc_receive },
  { "newchannel", luaproc_create_channel },
  { "delchannel", luaproc_destroy_channel },
  { "setnumworkers", luaproc_set_numworkers },
  { "getnumworkers", luaproc_get_numworkers },
  { "recycle", luaproc_recycle_set },
  { NULL, NULL }
};

/******************
 * list functions *
 ******************/

/* insert a lua process in a (fifo) list */
void list_insert( list *l, luaproc *lp ) {
  if ( l->head == NULL ) {
    l->head = lp;
  } else {
    l->tail->next = lp;
  }
  l->tail = lp;
  lp->next = NULL;
  l->nodes++;
}

/* remove and return the first lua process in a (fifo) list */
luaproc *list_remove( list *l ) {
  if ( l->head != NULL ) {
    luaproc *lp = l->head;
    l->head = lp->next;
    l->nodes--;
    return lp;
  } else {
    return NULL; /* if list is empty, return NULL */
  }
}

/* return a list's node count */
int list_count( list *l ) {
  return l->nodes;
}

/* initialize an empty list */
void list_init( list *l ) {
  l->head = NULL;
  l->tail = NULL;
  l->nodes = 0;
}

/*********************
 * channel functions *
 *********************/

/* create a new channel and insert it into channels table */
static channel *channel_create( const char *cname ) {

  channel *chan;

  /* get exclusive access to channels list */
  pthread_mutex_lock( &mutex_channel_list );

  /* create new channel and register its name */
  lua_getglobal( chanls, LUAPROC_CHANNELS_TABLE );
  chan = (channel *)lua_newuserdata( chanls, sizeof( channel ));
  lua_setfield( chanls, -2, cname );
  lua_pop( chanls, 1 );  /* remove channel table from stack */

  /* initialize channel struct */
  list_init( &chan->send );
  list_init( &chan->recv );
  pthread_mutex_init( &chan->mutex, NULL );
  pthread_cond_init( &chan->can_be_used, NULL );

  /* release exclusive access to channels list */
  pthread_mutex_unlock( &mutex_channel_list );

  return chan;
}

/*
   return a channel (if not found, return null).
   caller function MUST lock 'mutex_channel_list' before calling this function.
 */
static channel *channel_unlocked_get( const char *chname ) {

  channel *chan;

  lua_getglobal( chanls, LUAPROC_CHANNELS_TABLE );
  lua_getfield( chanls, -1, chname );
  chan = (channel *)lua_touserdata( chanls, -1 );
  lua_pop( chanls, 2 );  /* pop userdata and channel */

  return chan;
}

/*
   return a channel (if not found, return null) with its (mutex) lock set.
   caller function should unlock channel's (mutex) lock after calling this
   function.
 */
static channel *channel_locked_get( const char *chname ) {

  channel *chan;

  /* get exclusive access to channels list */
  pthread_mutex_lock( &mutex_channel_list );

  /*
     try to get channel and lock it; if lock fails, release external
     lock ('mutex_channel_list') to try again when signaled -- this avoids
     keeping the external lock busy for too long. during the release,
     the channel may be destroyed, so it must try to get it again.
  */
  while ((( chan = channel_unlocked_get( chname )) != NULL ) &&
        ( pthread_mutex_trylock( &chan->mutex ) != 0 )) {
    pthread_cond_wait( &chan->can_be_used, &mutex_channel_list );
  }

  /* release exclusive access to channels list */
  pthread_mutex_unlock( &mutex_channel_list );

  return chan;
}

/********************************
 * exported auxiliary functions *
 ********************************/

/* unlock access to a channel and signal it can be used */
void luaproc_unlock_channel( channel *chan ) {

  /* get exclusive access to channels list */
  pthread_mutex_lock( &mutex_channel_list );
  /* release exclusive access to operate on a particular channel */
  pthread_mutex_unlock( &chan->mutex );
  /* signal that a particular channel can be used */
  pthread_cond_signal( &chan->can_be_used );
  /* release exclusive access to channels list */
  pthread_mutex_unlock( &mutex_channel_list );

}

/* insert lua process in recycle list */
void luaproc_recycle_insert( luaproc *lp ) {

  /* get exclusive access to recycled lua processes list */
  pthread_mutex_lock( &mutex_recycle_list );

  /* is recycle list full? */
  if ( list_count( &recycle_list ) >= recyclemax ) {
    /* destroy state */
    lua_close( luaproc_get_state( lp ));
  } else {
    /* insert lua process in recycle list */
    list_insert( &recycle_list, lp );
  }

  /* release exclusive access to recycled lua processes list */
  pthread_mutex_unlock( &mutex_recycle_list );
}

/* queue a lua process that tried to send a message */
void luaproc_queue_sender( luaproc *lp ) {
  list_insert( &lp->chan->send, lp );
}

/* queue a lua process that tried to receive a message */
void luaproc_queue_receiver( luaproc *lp ) {
  list_insert( &lp->chan->recv, lp );
}

/********************************
 * internal auxiliary functions *
 ********************************/
static void luaproc_loadbuffer( lua_State *parent, luaproc *lp,
                                const char *code, size_t sz ) {

  /* load lua process' lua code */
  int ret = luaL_loadbuffer( lp->lstate, code, sz, "luaproc_fn" );

  /* in case of errors, close lua_State and push error to parent */
  if ( ret != 0 ) {
    lua_pushstring( parent, lua_tostring( lp->lstate, -1 ));
    lua_close( lp->lstate );
    luaL_error( parent, lua_tostring( parent, -1 ));
  }
}

/* moves values between lua states' stacks */
static int luaproc_movevalues( lua_State *Lfrom, lua_State *Lto ) {

  int i;
  int n = lua_gettop( Lfrom );
  const char *str;
  size_t len;

  /* ensure there is space in the receiver's stack */
  if ( lua_checkstack( Lto, n ) == 0 ) {
    lua_pushnil( Lto );
    lua_pushstring( Lto, "not enough space in the stack" );
    lua_pushnil( Lfrom );
    lua_pushstring( Lfrom, "not enough space in the receiver's stack" );
    return FALSE;
  }

  /* test each value's type and, if it's supported, move value */
  for ( i = 2; i <= n; i++ ) {
    switch ( lua_type( Lfrom, i )) {
      case LUA_TBOOLEAN:
        lua_pushboolean( Lto, lua_toboolean( Lfrom, i ));
        break;
      case LUA_TNUMBER:
        lua_pushnumber( Lto, lua_tonumber( Lfrom, i ));
        break;
      case LUA_TSTRING: {
        str = lua_tolstring( Lfrom, i, &len );
        lua_pushlstring( Lto, str, len );
        break;
      }
      case LUA_TNIL:
        lua_pushnil( Lto );
        break;
      default: /* value type not supported: table, function, userdata, etc. */
        lua_settop( Lto, 1 );
        lua_pushnil( Lto );
        lua_pushstring( Lto, "failed to receive unsupported value type" );
        lua_pushnil( Lfrom );
        lua_pushstring( Lfrom, "failed to send unsupported value type" );
        return FALSE;
    }
  }
  return TRUE;
}

/* return the lua process associated with a given lua state */
static luaproc *luaproc_getself( lua_State *L ) {

  luaproc *lp;

  lua_getfield( L, LUA_REGISTRYINDEX, "LUAPROC_LP_UDATA" );
  lp = (luaproc *)lua_touserdata( L, -1 );
  lua_pop( L, 1 );

  return lp;
}

#if LUA_VERSION_NUM >= 502
#define luaproc_newlib( L, funcs ) luaL_newlib( L, funcs )
#else
#define luaproc_newlib( L, funcs ) \
  lua_newtable( L ); luaL_register( L, NULL, funcs )
#endif

/* create new lua process */
static luaproc *luaproc_new( lua_State *L ) {

  luaproc *lp;
  lua_State *lpst = luaL_newstate();  /* create new lua state */

  /* store the lua process in its own lua state */
  lp = (luaproc *)lua_newuserdata( lpst, sizeof( struct stluaproc ));
  lua_setfield( lpst, LUA_REGISTRYINDEX, "LUAPROC_LP_UDATA" );
  luaproc_openlualibs( lpst );  /* load standard libraries */
  /* register luaproc's own functions */
  luaproc_newlib( lpst, luaproc_funcs );
  lua_setglobal( lpst, "luaproc" );
  lp->lstate = lpst;  /* insert created lua state into lua process struct */

  return lp;
}

/* join schedule workers (called before exiting Lua) */
static int luaproc_join_workers( lua_State *L ) {
  sched_join_workers();
  lua_close( chanls );
  return 0;
}

/* container for bytecode produced by lua_dump() */
struct dump_collector {
  int len;
  int alloc_len;
  char *code;
};

/* handle and aggregate lua_dump() bytecode output */
static int luaproc_collect_dump( lua_State *L, const void *data,
                                 size_t sz, void *o ) {

  struct dump_collector *out = (struct dump_collector *) o;
  /* ensure we have space for the next bytecode chunk */
  while ( out->len + sz > out->alloc_len ) {
    out->alloc_len <<= 1;
    if ( out->len + sz > out->alloc_len )
      continue;
    o = realloc(out->code, out->alloc_len);
    if ( o != NULL ) {
      out->code = (char *) o;
    } else {
      free(out->code);
      return LUA_ERRMEM;
    }
  }

  /* append data to bytecode already collected */
  memcpy(out->code + out->len, data, sz);
  out->len += sz;
  return 0;
}

/*********************
 * library functions *
 *********************/

/* set maximum number of lua processes in the recycle list */
static int luaproc_recycle_set( lua_State *L ) {

  luaproc *lp;

  /* validate parameter is a non negative number */
  int max = luaL_checkint( L, 1 );
  luaL_argcheck( L, max >= 0, 1, "recycle limit can't be negative" );

  /* get exclusive access to recycled lua processes list */
  pthread_mutex_lock( &mutex_recycle_list );

  recyclemax = max;  /* set maximum number */

  /* remove extra nodes and destroy each lua processes */
  while ( list_count( &recycle_list ) > recyclemax ) {
    lp = list_remove( &recycle_list );
    lua_close( lp->lstate );
  }
  /* release exclusive access to recycled lua processes list */
  pthread_mutex_unlock( &mutex_recycle_list );

  return 0;
}

/* wait until there are no more active lua processes */
static int luaproc_wait( lua_State *L ) {
  sched_wait();
  return 0;
}

/* set number of workers (creates or destroys accordingly) */
static int luaproc_set_numworkers( lua_State *L ) {

  int ret;

  /* validate parameter is a positive number */
  int numworkers = luaL_checkint( L, -1 );
  luaL_argcheck( L, numworkers > 0, 1, "number of workers must be positive" );

  /* set number of threads; signal error on failure */
  ret = sched_set_numworkers( numworkers );
  if ( ret == LUAPROC_SCHED_PTHREAD_ERROR ) {
      luaL_error( L, "failed to create worker" );
  } 

  return 0;
}

/* return the number of active workers */
static int luaproc_get_numworkers( lua_State *L ) {
  lua_pushnumber( L, sched_get_numworkers( ));
  return 1;
}

/* wrapper for lua_dump() */
static int luaproc_lua_dump( lua_State *L, lua_Writer w,
                             void *data, int strip ) {
#if LUA_VERSION_NUM >= 503
  return lua_dump(L, w, data, strip);
#else
  return lua_dump(L, w, data);
#endif
}

/* create and schedule a new lua process */
static int luaproc_create_newproc( lua_State *L ) {

  /* check if first argument is a function or a string */
  luaproc *lp;
  struct dump_collector fdata = { 0, 0, NULL };
  if ( lua_isfunction( L, 1 ) ) {
    /* if we have a function, get its bytecode */
    fdata.code = malloc(8);
    fdata.alloc_len = 8;
    if ( luaproc_lua_dump( L, &luaproc_collect_dump, &fdata, 0 ) ) {
      lua_pushstring( L, "luaproc: out of memory or invalid function" );
      lua_error( L );
    }
  } else {
    fdata.code = (char *) luaL_checkstring( L, 1 );
    fdata.len = strlen( fdata.code );
  }

  /* get exclusive access to recycled lua processes list */
  pthread_mutex_lock( &mutex_recycle_list );

  /* check if a lua process can be recycled */
  if ( recyclemax > 0 ) {
    lp = list_remove( &recycle_list );
    /* otherwise create a new lua process */
    if ( lp == NULL ) {
      lp = luaproc_new( L );
    }
  } else {
    lp = luaproc_new( L );
  }

  /* release exclusive access to recycled lua processes list */
  pthread_mutex_unlock( &mutex_recycle_list );

  /* init luaprocess */
  lp->status = LUAPROC_STATUS_IDLE;
  lp->args   = 0;
  lp->chan   = NULL;

  /* check code syntax and set lua process ready to execute, 
     or raise an error in corresponding lua state */
  luaproc_loadbuffer( L, lp, fdata.code, fdata.len );
  sched_inc_lpcount();   /* increase active lua process count */
  sched_queue_proc( lp );  /* schedule lua process for execution */
  lua_pushboolean( L, TRUE );

  if ( fdata.alloc_len != 0 )
    free(fdata.code);

  return 1;
}

/* send a message to a lua process */
static int luaproc_send( lua_State *L ) {

  int ret;
  channel *chan;
  luaproc *dstlp, *self;
  const char *chname = luaL_checkstring( L, 1 );

  chan = channel_locked_get( chname );
  /* if channel is not found, return an error to lua */
  if ( chan == NULL ) {
    lua_pushnil( L );
    lua_pushfstring( L, "channel '%s' does not exist", chname );
    return 2;
  }

  /* remove first lua process, if any, from channel's receive list */
  dstlp = list_remove( &chan->recv );
  
  if ( dstlp != NULL ) { /* found a receiver? */
    /* try to move values between lua states' stacks */
    ret = luaproc_movevalues( L, dstlp->lstate );
    /* -1 because channel name is on the stack */
    dstlp->args = lua_gettop( dstlp->lstate ) - 1; 
    if ( dstlp->lstate == mainlp.lstate ) {
      /* if sending process is the parent (main) Lua state, unblock it */
      pthread_mutex_lock( &mutex_mainls );
      pthread_cond_signal( &cond_mainls_sendrecv );
      pthread_mutex_unlock( &mutex_mainls );
    } else {
      /* schedule receiving lua process for execution */
      sched_queue_proc( dstlp );
    }
    /* unlock channel access */
    luaproc_unlock_channel( chan );
    if ( ret == TRUE ) { /* was send successful? */
      lua_pushboolean( L, TRUE );
      return 1;
    } else { /* nil and error msg already in stack */
      return 2;
    }

  } else { 
    if ( L == mainlp.lstate ) {
      /* sending process is the parent (main) Lua state - block it */
      mainlp.chan = chan;
      luaproc_queue_sender( &mainlp );
      luaproc_unlock_channel( chan );
      pthread_mutex_lock( &mutex_mainls );
      pthread_cond_wait( &cond_mainls_sendrecv, &mutex_mainls );
      pthread_mutex_unlock( &mutex_mainls );
      return mainlp.args;
    } else {
      /* sending process is a standard luaproc - set status, block and yield */
      self = luaproc_getself( L );
      if ( self != NULL ) {
        self->status = LUAPROC_STATUS_BLOCKED_SEND;
        self->chan   = chan;
      }
      /* yield. channel will be unlocked by the scheduler */
      return lua_yield( L, lua_gettop( L ));
    }
  }
}

/* receive a message from a lua process */
static int luaproc_receive( lua_State *L ) {

  int ret, nargs;
  channel *chan;
  luaproc *srclp, *self;
  const char *chname = luaL_checkstring( L, 1 );

  /* get number of arguments passed to function */
  nargs = lua_gettop( L );

  chan = channel_locked_get( chname );
  /* if channel is not found, return an error to Lua */
  if ( chan == NULL ) {
    lua_pushnil( L );
    lua_pushfstring( L, "channel '%s' does not exist", chname );
    return 2;
  }

  /* remove first lua process, if any, from channels' send list */
  srclp = list_remove( &chan->send );

  if ( srclp != NULL ) {  /* found a sender? */
    /* try to move values between lua states' stacks */
    ret = luaproc_movevalues( srclp->lstate, L );
    if ( ret == TRUE ) { /* was receive successful? */
      lua_pushboolean( srclp->lstate, TRUE );
      srclp->args = 1;
    } else {  /* nil and error_msg already in stack */
      srclp->args = 2;
    }
    if ( srclp->lstate == mainlp.lstate ) {
      /* if sending process is the parent (main) Lua state, unblock it */
      pthread_mutex_lock( &mutex_mainls );
      pthread_cond_signal( &cond_mainls_sendrecv );
      pthread_mutex_unlock( &mutex_mainls );
    } else {
      /* otherwise, schedule process for execution */
      sched_queue_proc( srclp );
    }
    /* unlock channel access */
    luaproc_unlock_channel( chan );
    /* disconsider channel name, async flag and any other args passed 
       to the receive function when returning its results */
    return lua_gettop( L ) - nargs; 

  } else {  /* otherwise test if receive was synchronous or asynchronous */
    if ( lua_toboolean( L, 2 )) { /* asynchronous receive */
      /* unlock channel access */
      luaproc_unlock_channel( chan );
      /* return an error */
      lua_pushnil( L );
      lua_pushfstring( L, "no senders waiting on channel '%s'", chname );
      return 2;
    } else { /* synchronous receive */
      if ( L == mainlp.lstate ) {
        /*  receiving process is the parent (main) Lua state - block it */
        mainlp.chan = chan;
        luaproc_queue_receiver( &mainlp );
        luaproc_unlock_channel( chan );
        pthread_mutex_lock( &mutex_mainls );
        pthread_cond_wait( &cond_mainls_sendrecv, &mutex_mainls );
        pthread_mutex_unlock( &mutex_mainls );
        return mainlp.args;
      } else {
        /* receiving process is a standard luaproc - set status, block and 
           yield */
        self = luaproc_getself( L );
        if ( self != NULL ) {
          self->status = LUAPROC_STATUS_BLOCKED_RECV;
          self->chan   = chan;
        }
        /* yield. channel will be unlocked by the scheduler */
        return lua_yield( L, lua_gettop( L ));
      }
    }
  }
}

/* create a new channel */
static int luaproc_create_channel( lua_State *L ) {

  const char *chname = luaL_checkstring( L, 1 );

  channel *chan = channel_locked_get( chname );
  if (chan != NULL) {  /* does channel exist? */
    /* unlock the channel mutex locked by channel_locked_get */
    luaproc_unlock_channel( chan );
    /* return an error to lua */
    lua_pushnil( L );
    lua_pushfstring( L, "channel '%s' already exists", chname );
    return 2;
  } else {  /* create channel */
    channel_create( chname );
    lua_pushboolean( L, TRUE );
    return 1;
  }
}

/* destroy a channel */
static int luaproc_destroy_channel( lua_State *L ) {

  channel *chan;
  list *blockedlp;
  luaproc *lp;
  const char *chname = luaL_checkstring( L,  1 );

  /* get exclusive access to channels list */
  pthread_mutex_lock( &mutex_channel_list );

  /*
     try to get channel and lock it; if lock fails, release external
     lock ('mutex_channel_list') to try again when signaled -- this avoids
     keeping the external lock busy for too long. during this release,
     the channel may have been destroyed, so it must try to get it again.
  */
  while ((( chan = channel_unlocked_get( chname )) != NULL ) &&
          ( pthread_mutex_trylock( &chan->mutex ) != 0 )) {
    pthread_cond_wait( &chan->can_be_used, &mutex_channel_list );
  }

  if ( chan == NULL ) {  /* found channel? */
    /* release exclusive access to channels list */
    pthread_mutex_unlock( &mutex_channel_list );
    /* return an error to lua */
    lua_pushnil( L );
    lua_pushfstring( L, "channel '%s' does not exist", chname );
    return 2;
  }

  /* remove channel from table */
  lua_getglobal( chanls, LUAPROC_CHANNELS_TABLE );
  lua_pushnil( chanls );
  lua_setfield( chanls, -2, chname );
  lua_pop( chanls, 1 );

  pthread_mutex_unlock( &mutex_channel_list );

  /*
     wake up workers there are waiting to use the channel.
     they will not find the channel, since it was removed,
     and will not get this condition anymore.
   */
  pthread_cond_broadcast( &chan->can_be_used );

  /*
     dequeue lua processes waiting on the channel, return an error message
     to each of them indicating channel was destroyed and schedule them
     for execution (unblock them).
   */
  if ( chan->send.head != NULL ) {
    lua_pushfstring( L, "channel '%s' destroyed while waiting for receiver", 
                     chname );
    blockedlp = &chan->send;
  } else {
    lua_pushfstring( L, "channel '%s' destroyed while waiting for sender", 
                     chname );
    blockedlp = &chan->recv;
  }
  while (( lp = list_remove( blockedlp )) != NULL ) {
    /* return an error to each process */
    lua_pushnil( lp->lstate );
    lua_pushstring( lp->lstate, lua_tostring( L, -1 ));
    lp->args = 2;
    sched_queue_proc( lp ); /* schedule process for execution */
  }

  /* unlock channel mutex and destroy both mutex and condition */
  pthread_mutex_unlock( &chan->mutex );
  pthread_mutex_destroy( &chan->mutex );
  pthread_cond_destroy( &chan->can_be_used );

  lua_pushboolean( L, TRUE );
  return 1;
}

/***********************
 * get'ers and set'ers *
 ***********************/

/* return the channel where a lua process is blocked at */
channel *luaproc_get_channel( luaproc *lp ) {
  return lp->chan;
}

/* return a lua process' status */
int luaproc_get_status( luaproc *lp ) {
  return lp->status;
}

/* set lua a process' status */
void luaproc_set_status( luaproc *lp, int status ) {
  lp->status = status;
}

/* return a lua process' state */
lua_State *luaproc_get_state( luaproc *lp ) {
  return lp->lstate;
}

/* return the number of arguments expected by a lua process */
int luaproc_get_numargs( luaproc *lp ) {
  return lp->args;
}

/* set the number of arguments expected by a lua process */
void luaproc_set_numargs( luaproc *lp, int n ) {
  lp->args = n;
}

/**********************************
 * register structs and functions *
 **********************************/

static void luaproc_reglualib( lua_State *L, const char *name, 
                               lua_CFunction f ) {
  lua_getglobal( L, "package" );
  lua_getfield( L, -1, "preload" );
  lua_pushcfunction( L, f );
  lua_setfield( L, -2, name );
  lua_pop( L, 2 );
}

#if LUA_VERSION_NUM >= 502
#define luaproc_reqf( L, modname, f, glob ) \
  luaL_requiref( L, modname, f, glob ); lua_pop( L, 1 )
#else
#define luaproc_reqf( L, modname, f, glob ) lua_cpcall( L, f, NULL )
#endif

static void luaproc_openlualibs( lua_State *L ) {
  luaproc_reqf( L, "_G", luaopen_base, FALSE );
  luaproc_reqf( L, "package", luaopen_package, TRUE );
  luaproc_reglualib( L, "io", luaopen_io );
  luaproc_reglualib( L, "os", luaopen_os );
  luaproc_reglualib( L, "table", luaopen_table );
  luaproc_reglualib( L, "string", luaopen_string );
  luaproc_reglualib( L, "math", luaopen_math );
  luaproc_reglualib( L, "debug", luaopen_debug );
#if LUA_VERSION_NUM == 502
  luaproc_reglualib( L, "bit32", luaopen_bit32 );
#endif
#if LUA_VERSION_NUM >= 502
  luaproc_reglualib( L, "coroutine", luaopen_coroutine );
#endif
}

LUALIB_API int luaopen_luaproc( lua_State *L ) {

  int init;

  /* wrap main state inside a lua process */
  mainlp.lstate = L;
  mainlp.status = LUAPROC_STATUS_IDLE;
  mainlp.args   = 0;
  mainlp.chan   = NULL;
  mainlp.next   = NULL;
  /* register luaproc functions */
  luaproc_newlib( L, luaproc_funcs );
  /* initialize recycle list */
  list_init( &recycle_list );
  /* initialize channels table and lua_State used to store it */
  chanls = luaL_newstate();
  lua_newtable( chanls );
  lua_setglobal( chanls, LUAPROC_CHANNELS_TABLE );
  /* create finalizer to join workers when Lua exits */
  lua_newuserdata( L, 0 );
  lua_setfield( L, LUA_REGISTRYINDEX, "LUAPROC_FINALIZER_UDATA" );
  luaL_newmetatable( L, "LUAPROC_FINALIZER_MT" );
  lua_pushliteral( L, "__gc" );
  lua_pushcfunction( L, luaproc_join_workers );
  lua_rawset( L, -3 );
  lua_pop( L, 1 );
  lua_getfield( L, LUA_REGISTRYINDEX, "LUAPROC_FINALIZER_UDATA" );
  lua_getfield( L, LUA_REGISTRYINDEX, "LUAPROC_FINALIZER_MT" );
  lua_setmetatable( L, -2 );
  lua_pop( L, 1 );
  /* initialize scheduler */
  init = sched_init();
  if ( init == LUAPROC_SCHED_PTHREAD_ERROR ) {
    luaL_error( L, "failed to create worker" );
  }

  return 1;
}
