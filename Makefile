#
# OCFLAGS:
# 	COUNT_IOS	- Counts struct io's left at end
# 	DEBUG		- Various and sundy debug asserts
# 	NDEBUG		- Defined: no asserts, Undefined: asserts
#
UTILDIR = util

CC	= gcc
MAKE = make
CFLAGS	= -Wall -W -g
INCS	= -I. -I$(UTILDIR)
OCFLAGS	= -UCOUNT_IOS -UDEBUG -DNDEBUG
XCFLAGS	= -D_GNU_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64
override CFLAGS += $(INCS) $(XCFLAGS) $(OCFLAGS)

PROGS	= recovery
LIBS	= -laio -lrt -lpthread
UTILO   = $(UTILDIR)/hashtable.o $(UTILDIR)/str.o

all: depend $(PROGS)

$(PROGS): | depend util


clean:
	rm -f *.o $(PROGS) .depend
	rm -f $(UTILO)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $*.o $<

recovery: recovery.o
	$(CC) $(CFLAGS) -o $@ $(filter %.o,$^) $(UTILO) $(LIBS)

depend:
	@$(CC) -MM $(CFLAGS) *.c 1> .depend

util: $(UTILO)
	echo util
	cd $(UTILDIR);  $(MAKE)

ifneq ($(wildcard .depend),)
include .depend
endif
