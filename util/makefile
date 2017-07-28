SHELL    = /bin/bash

CC       = gcc -std=c99
CFBASE   = -Wall -pedantic 
CFLAGS   = $(CFBASE) -DNDEBUG -O3 -fpic

LD       = gcc
SOFLAGS  = -shared 

OBJS     = hashtable.o str.o
SOS      = libht.so libstr.so
STATICS  = libhts.a libstrs.a

all:	$(OBJS) 
so:     $(SOS)
statics:$(STATICS)

hashtable.o:   hashtable.h hashtable.c makefile
	$(CC) $(CFLAGS) -c hashtable.c -o $@

str.o: str.h str.c makefile
	$(CC) $(CFLAGS) -c str.c -o $@

libht.so: hashtable.o str.o
	$(LD) $(SOFLAGS) hashtable.o str.o -o $@

libstr.so : str.o
	$(LD) $(SOFLAGS) str.o -o $@

libhts.a: hashtable.o str.o
	ar cr libhts.a hashtable.o str.o

libstrs.a:str.o
	ar cr libstrs.a str.o

#-----------------------------------------------------------------------
# Clean up
#-----------------------------------------------------------------------
clean:
	rm -f $(PRGS) *.o *.so *.a
