####################################################
#
# Copyright 2008 Alexandre Skyrme, Noemi Rodriguez, Roberto Ierusalimschy
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
# 
######################################################
#
# [Makefile]
#
######################################################

# path to lua header files
LUA_INC_PATH=/usr/include/lua5.1
# path to lua library
LUA_LIB_PATH=/usr/lib/lua5.1

# standard makefile variables
CC=gcc
CFLAGS=-c -Wall -fPIC -I${LUA_INC_PATH}
LDFLAGS=-shared -L${LUA_LIB_PATH} -lpthread
SOURCES=sched.c list.c luaproc.c channel.c
OBJECTS=${SOURCES:.c=.o}
LIB=luaproc.so

all: ${SOURCES} ${LIB}

${LIB}: ${OBJECTS}
	${CC} ${OBJECTS} -o $@ ${LDFLAGS} 

sched.o: sched.c sched.h list.h luaproc.h channel.h
	${CC} ${CFLAGS} sched.c

list.o: list.c list.h
	${CC} ${CFLAGS} list.c

luaproc.o: luaproc.c luaproc.h list.h sched.h channel.h
	${CC} ${CFLAGS} luaproc.c

channel.o: channel.c channel.h list.h
	${CC} ${CFLAGS} channel.c

clean:
	rm -f ${OBJECTS} ${LIB}

test:
	lua test.lua

