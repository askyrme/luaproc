# Makefile for building luaproc

# lua version
LUA_VERSION=5.3
# path to lua header files
LUA_INCDIR=/usr/include/lua${LUA_VERSION}
# path to lua library
LUA_LIBDIR=/usr/lib/x86_64-linux-gnu/
# path to install library
LUA_CPATH=/usr/lib/lua/${LUA_VERSION}

# standard makefile variables
CC=gcc
SRCDIR=src
BINDIR=bin
THREADS=-DLUAPROC_USE_PTHREADS
CFLAGS=-c -O2 -Wall -fPIC -I${LUA_INCDIR} ${THREADS}
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
all: ${BINDIR}/${LIB}

${BINDIR}/${LIB}: ${OBJECTS}
	${CC} $^ -o $@ ${LDFLAGS} 

lpsched.o: lpsched.c lpsched.h luaproc.h lpthread.h
	${CC} ${CFLAGS} $^

luaproc.o: luaproc.c luaproc.h lpsched.h lpthread.h
	${CC} ${CFLAGS} $^

install: 
	cp -v ${BINDIR}/${LIB} ${LUA_CPATH}

clean:
	rm -f ${OBJECTS} ${BINDIR}/${LIB}

# list targets that do not create files (but not all makes understand .PHONY)
.PHONY: clean install

# (end of Makefile)

