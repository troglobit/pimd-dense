# For replacement functions in lib/
AUTOMAKE_OPTIONS = subdir-objects

# -D_GNU_SOURCE Use GNU extensions, where possible
# -D_BSD_SOURCE Use functions derived from 4.3 BSD Unix rather than POSIX.1
#               In GLIBC >= v2.20 this is replaced with -D_DEFAULT_SOURCE,
#               but to build on older GLIBC systems we now need both ...
AM_CPPFLAGS      = -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_GNU_SOURCE -I$(top_srcdir)/include
AM_CPPFLAGS     += -DSYSCONFDIR=\"@sysconfdir@\" -DLOCALSTATEDIR=\"@localstatedir@\"

sbin_PROGRAMS    = pimdd pimctl
pimdd_SOURCES    = callout.c config.c debug.c debug.h defs.h dvmrp.h	\
		   dvmrp_proto.c igmpv2.h igmp.c igmp_proto.c inet.c	\
		   ipc.c ipc.h kern.c main.c mrt.c mrt.h		\
		   pathnames.h pimdd.h pim.c pim_proto.c queue.h	\
		   route.c vif.c timer.c trace.c trace.h vif.h
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
pimdd_SOURCES   += rsrr.c		rsrr_var.h	rsrr.h
endif

pimctl_SOURCES   = pimctl.c		ipc.h
pimctl_LDADD     = $(LIBS) $(LIBOBJS)
