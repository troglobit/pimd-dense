# -D_GNU_SOURCE Use GNU extensions, where possible
# -D_BSD_SOURCE Use functions derived from 4.3 BSD Unix rather than POSIX.1
#               In GLIBC >= v2.20 this is replaced with -D_DEFAULT_SOURCE,
#               but to build on older GLIBC systems we now need both ...
AM_CPPFLAGS      = -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_GNU_SOURCE -I$(top_srcdir)/include
AM_CPPFLAGS     += -DSYSCONFDIR=\"@sysconfdir@\" -DLOCALSTATEDIR=\"@localstatedir@\"
AM_CPPFLAGS     += -Dlog=logit

sbin_PROGRAMS    = pimdd
pimdd_SOURCES    = igmp.c igmp_proto.c trace.c				\
		   inet.c kern.c main.c config.c debug.c routesock.c	\
		   vers.c callout.c					\
		   route.c vif.c timer.c mrt.c pim.c pim_proto.c	\
		   dvmrp_proto.c					\
		   debug.h defs.h dvmrp.h igmpv2.h mrt.h pathnames.h pimdd.h	\
		   trace.h vif.h
pimdd_CFLAGS     = -W -Wall -Wextra -Wno-unused
pimdd_LDADD      = $(LIBS) $(LIBOBJS)

if LINUX
AM_CPPFLAGS     += -DRAW_OUTPUT_IS_RAW -DIOCTL_OK_ON_RAW_SOCKET
endif

if RSRR
AM_CPPFLAGS     += -DPIM
pimdd_SOURCES   += rsrr.c		rsrr_var.h	rsrr.h
endif
