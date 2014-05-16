####################################################
#
# Copyright 2008-2014 Alexandre Skyrme, Noemi Rodriguez, Roberto Ierusalimschy
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
LUA_INCDIR=/usr/include/lua5.1
# path to lua library
LUA_LIBDIR=/usr/lib/x86_64-linux-gnu/
# path to install library
LUA_CPATH=/usr/share/lua/5.1

# standard makefile variables
CC=gcc
SRCDIR=src
BINDIR=bin
CFLAGS=-c -O2 -Wall -fPIC -I${LUA_INCDIR}
# MacOS X users should replace LIBFLAG with the following definition
# LIBFLAG=-bundle -undefined dynamic_lookup
LIBFLAG=-shared
#
LDFLAGS=${LIBFLAG} -L${LUA_LIBDIR} -lpthread 
SOURCES=${SRCDIR}/lpsched.c ${SRCDIR}/luaproc.c
OBJECTS=${SOURCES:.c=.o}

# luaproc specific variables
LIBNAME=luaproc
LIB=${LIBNAME}.so

# build targets
all: ${SOURCES} ${LIB}

${LIB}: ${OBJECTS}
	${CC} ${OBJECTS} -o ${BINDIR}/$@ ${LDFLAGS} 

install: 
	mkdir -p ${LUA_CPATH}/${LIBNAME} 
	cp -v ${LIB} ${LUA_CPATH}/${LIBNAME}

lpsched.o: lpsched.c lpsched.h luaproc.h
	@cd src && ${CC} ${CFLAGS} lpsched.c

luaproc.o: luaproc.c luaproc.h lpsched.h
	@cd src && ${CC} ${CFLAGS} luaproc.c

clean:
	rm -f ${OBJECTS} ${BINDIR}/${LIB}

# list targets that do not create files (but not all makes understand .PHONY)
.PHONY: clean install

# (end of Makefile)

