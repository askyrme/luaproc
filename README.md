# luaproc - A concurrent programming library for Lua

## Introduction

*luaproc* is a [Lua](http://www.lua.org) extension library for concurrent
programming. This text provides some background information and also serves as s
reference manual for the library. The library is available under the same [terms
and conditions](http://www.lua.org/copyright.html) as the Lua language, the MIT
license. The idea is that if you can use Lua in a project, you should also be
able to use *luaproc*.

Lua natively supports cooperative multithreading by means of coroutines.
However, coroutines in Lua cannot be executed in parallel. *luaproc* overcomes
that restriction by building on the proposal and sample implementation presented
in [Programming in Lua](http://www.inf.puc-rio.br/~roberto/pil2) (chapter 30).
It uses coroutines and multiple independent states in Lua to implement *Lua
processes*, which are user threads comprised of Lua code that have no shared
data. Lua processes are executed by *workers*, which are system threads
implemented with POSIX threads (pthreads), and thus can run in parallel.

Communication between Lua processes relies exclusively on message passing. Each
message can carry a tuple of atomic Lua values (strings, numbers, booleans and
nil). More complex types must be encoded somehow -- for instance by using
strings of Lua code that when executed return such a type. Message addressing is
based on communication channels, which are decoupled from Lua processes and must
be explicitly created.

Sending a message is always a synchronous operation, i.e., the send operation
only returns after a message has been received by another Lua process or if an
error occurs (such as trying to send a message to a non-existent channel).
Receiving a message, on the other hand, can be a synchronous or asynchronous
operation. In synchronous mode, a call to the receive operation only returns
after a message has been received or if an error occurs. In asynchronous mode, a
call to the receive operation returns immediately and indicates if a message was
received or not.

If a Lua process tries to send a message to a channel where there are no Lua
processes waiting to receive a message, its execution is suspended until a
matching receive occurs or the channel is destroyed. The same happens if a Lua
process tries to synchronously receive a message from a channel where there are
no Lua processes waiting to send a message.

*luaproc* also offers an optional facility to recycle Lua processes. Recycling
consists of reusing states from finished Lua processes, instead of destroying
them. When recycling is enabled, a new Lua process can be created by loading its
code in a previously used state from a finished Lua process, instead of creating
a new state. 

## API

**`luaproc.newproc( string lua_code )`**
**`luaproc.newproc( function f )`**

Creates a new Lua process to run the specified string of Lua code or the
specified Lua function. Returns true if successful or nil and an error message
if failed. The only libraries loaded in new Lua processes are luaproc itself and
the standard Lua base and package libraries. The remaining standard Lua
libraries (io, os, table, string, math, debug, coroutine and utf8) are
pre-registered and can be loaded with a call to the standard Lua function
`require`. 

**`luaproc.setnumworkers( int number_of_workers )`**

Sets the number of active workers (pthreads) to n (default = 1, minimum = 1).
Creates and destroys workers as needed, depending on the current number of
active workers. No return, raises error if worker could not be created. 

**`luaproc.getnumworkers( )`**

Returns the number of active workers (pthreads). 

**`luaproc.wait( )`**

Waits until all Lua processes have finished, then continues program execution.
It only makes sense to call this function from the main Lua script. Moreover,
this function is implicitly called when the main Lua script finishes executing.
No return. 

**`luaproc.recycle( int maxrecycle )`**

Sets the maximum number of Lua processes to recycle. Returns true if successful
or nil and an error message if failed. The default number is zero, i.e., no Lua
processes are recycled. 

**`luaproc.send( string channel_name, msg1, [msg2], [msg3], [...] )`**

Sends a message (tuple of boolean, nil, number or string values) to a channel.
Returns true if successful or nil and an error message if failed. Suspends
execution of the calling Lua process if there is no matching receive. 

**`luaproc.receive( string channel_name, [boolean asynchronous] )`**

Receives a message (tuple of boolean, nil, number or string values) from a
channel. Returns received values if successful or nil and an error message if
failed. Suspends execution of the calling Lua process if there is no matching
receive and the async (boolean) flag is not set. The async flag, by default, is
not set. 

**`luaproc.newchannel( string channel_name )`**

Creates a new channel identified by string name. Returns true if successful or
nil and an error message if failed.

**`luaproc.delchannel( string channel_name )`**

Destroys a channel identified by string name. Returns true if successful or nil
and an error message if failed. Lua processes waiting to send or receive
messages on destroyed channels have their execution resumed and receive an error
message indicating the channel was destroyed. 

## References

A paper about luaproc -- *Exploring Lua for Concurrent Programming* -- was
published in the Journal of Universal Computer Science and is available
[here](http://www.jucs.org/jucs_14_21/exploring_lua_for_concurrent) and
[here](http://www.inf.puc-rio.br/~roberto/docs/ry08-05.pdf). Some information in
the paper is already outdated, but it still provides a good overview of the
library and some of its design choices.

A tech report about concurrency in Lua, which uses luaproc as part of a case
study, is also available
[here](ftp://ftp.inf.puc-rio.br/pub/docs/techreports/11_13_skyrme.pdf).

Finally, a paper about an experiment to port luaproc to use Transactional Memory
instead of the standard POSIX Threads synchronization constructs, published as a
part of the 8th ACM SIGPLAN Workshop on Transactional Computing, can be found
[here](http://transact2013.cse.lehigh.edu/skyrme.pdf).

## Download

GitHub source repository:
[https://github.com/askyrme/luaproc](https://github.com/askyrme/luaproc)

## License

Copyright © 2008-2015 Alexandre Skyrme, Noemi Rodriguez, Roberto Ierusalimschy.
All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. 
