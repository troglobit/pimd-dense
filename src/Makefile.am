# For replacement functions in lib/
AUTOMAKE_OPTIONS = subdir-objects

AM_CPPFLAGS      = -I$(top_srcdir)/include
AM_CPPFLAGS     += -DSYSCONFDIR=\"@sysconfdir@\" -DRUNSTATEDIR=\"@runstatedir@\"

sbin_PROGRAMS    = pimdd pimctl
pimdd_SOURCES    = callout.c cfparse.y config.c debug.c debug.h defs.h dvmrp.h	\
		   dvmrp_proto.c igmpv2.h igmpv3.h igmp.c igmp_proto.c inet.c	\
		   ipc.c kern.c main.c mrt.c mrt.h pathnames.h pimdd.h pim.c	\
		   pim_proto.c queue.h route.c timer.c trace.c trace.h vif.c vif.h
pimdd_CFLAGS     = -W -Wall -Wextra -Wno-unused
pimdd_LDADD      = $(LIBS) $(LIBOBJS)

if LINUX
AM_CPPFLAGS     += -DRAW_OUTPUT_IS_RAW -DIOCTL_OK_ON_RAW_SOCKET
pimdd_SOURCES   += netlink.c
endif

if BSD
pimdd_SOURCES   += routesock.c
endif

if RSRR
AM_CPPFLAGS     += -DPIM
pimdd_SOURCES   += rsrr.c	rsrr_var.h	rsrr.h
endif

pimctl_SOURCES   = pimctl.c	queue.h
pimctl_LDADD     = $(LIBS) $(LIBOBJS)
