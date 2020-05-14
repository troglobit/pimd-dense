#
# Makefile for the Daemon part of PIM-SMv2.0,
# Protocol Independent Multicast, Sparse-Mode version 2.0
#
#
#  Questions concerning this software should be directed to 
#  Kurt Windisch (kurtw@antc.uoregon.edu)
#
#  $Id: Makefile,v 1.10 1998/12/22 21:50:16 kurtw Exp $
#
# XXX: SEARCH FOR "CONFIGCONFIGCONFIG" (without the quotas) for the lines
# that might need configuration

PROG_NAME=pimdd

#CONFIGCONFIGCONFIG
#
# flags:
#	-DSAVE_MEMORY: saves 4 bytes per unconfigured interface
#		per routing entry. If set, configuring such interface
#		will restart the daemon and will flush the routing
#		table.
#
MISCDEFS=	# -DSAVE_MEMORY

#
# Version control stuff. Nothing should be changed
#
VERSION = `cat VERSION`
CVS_VERSION = `cat VERSION | sed 'y/./_/' | sed 'y/-/_/'`
CVS_LAST_VERSION=`cat CVS_LAST_VERSION`
PROG_VERSION =  ${PROG_NAME}-${VERSION}
PROG_CVS_VERSION = ${PROG_NAME}_${CVS_VERSION}
PROG_CVS_LAST_VERSION = ${PROG_NAME}_${CVS_LAST_VERSION}

# TODO: XXX: CURRENTLY SNMP NOT SUPPORTED!!!!
#
# Uncomment the following eight lines if you want to use David Thaler's
# CMU SNMP daemon support.
#
#SNMPDEF=	-DSNMP
#SNMPLIBDIR=	-Lsnmpd -Lsnmplib
#SNMPLIBS=	-lsnmpd -lsnmp
#CMULIBS=	snmpd/libsnmpd.a snmplib/libsnmp.a
#MSTAT=		mstat
#SNMP_SRCS=	snmp.c
#SNMP_OBJS=	snmp.o
#SNMPCLEAN=	snmpclean
# End SNMP support

#CONFIGCONFIGCONFIG
# Uncomment the following line if you want to use RSRR (Routing
# Support for Resource Reservations), currently used by RSVP.
RSRRDEF=	-DRSRR

CC =		gcc
MCAST_INCLUDE=	-Iinclude
LDFLAGS=

#CONFIGCONFIGCONFIG
PURIFY=		purify -cache-dir=/tmp -collector=/import/pkgs/gcc/lib/gcc-lib/sparc-sun-sunos4.1.3_U1/2.7.2.2/ld

#CONFIGCONFIGCONFIG
### Compilation flags for different platforms. Uncomment only one of them
## FreeBSD
CFLAGS= -Wall -g	-Iinclude/freebsd ${MCAST_INCLUDE} ${SNMPDEF} ${RSRRDEF} ${MISCDEFS} -DFreeBSD -DPIM

## NetBSD   -DNetBSD is done by OS
#CFLAGS= -Wall -g	-Iinclude/netbsd ${MCAST_INCLUDE} ${SNMPDEF} ${RSRRDEF} ${MISCDEFS} -DPIM

## BSDI
#CFLAGS=  -g	 ${MCAST_INCLUDE} ${SNMPDEF} ${RSRRDEF} ${MISCDEFS} -DBSDI -DPIM

## SunOS, OSF1, gcc
#CFLAGS= -Wall -g	-Iinclude/sunos-gcc ${MCAST_INCLUDE} ${SNMPDEF} ${RSRRDEF} ${MISCDEFS} -DSunOS=43 -DPIM
## SunOS, OSF1, cc
#CFLAGS= -g		-Iinclude/sunos-cc ${MCAST_INCLUDE} ${SNMPDEF} ${RSRRDEF} ${MISCDEFS} -DSunOS=43 -DPIM

## IRIX
#CFLAGS= -Wall -g	 ${MCAST_INCLUDE} ${SNMPDEF} ${RSRRDEF} ${MISCDEFS} -D_BSD_SIGNALS -DIRIX -DPIM

## Solaris 2.5, gcc
#CFLAGS= -Wall	-g ${MCAST_INCLUDE} ${SNMPDEF} ${RSRRDEF} ${MISCDEFS} -DSYSV -DSunOS=55 -DPIM
## Solaris 2.5, cc
#CFLAGS= 	-g ${MCAST_INCLUDE} ${SNMPDEF} ${RSRRDEF} ${MISCDEFS} -DSYSV -DSunOS=55 -DPIM
## Solaris 2.6
#CFLAGS=	-g ${MCAST_INCLUDE} ${SNMPDEF} ${RSRRDEF} ${MISCDEFS} -DSYSV -DSunOS=56 -DPIM
## Solaris 2.x
#LIB2=		-L/usr/ucblib -lucb -L/usr/lib -lsocket -lnsl

## Linux
#CFLAGS= -Wall -g ${MCAST_INCLUDE} ${SNMPDEF} ${RSRRDEF} ${MISCDEFS} -D__BSD_SOURCE -DRAW_INPUT_IS_RAW -DRAW_OUTPUT_IS_RAW -DIOCTL_OK_ON_RAW_SOCKET -DLinux -DPIM


LIBS=		${SNMPLIBDIR} ${SNMPLIBS} ${LIB2}
LINTFLAGS=	${MCAST_INCLUDE} ${CFLAGS}

IGMP_SRCS=	igmp.c igmp_proto.c trace.c
IGMP_OBJS=	igmp.o igmp_proto.o trace.o
ROUTER_SRCS=	inet.c kern.c main.c config.c debug.c routesock.c \
		vers.c callout.c	
ROUTER_OBJS=	inet.o kern.o main.o config.o debug.o routesock.o \
		vers.o callout.o
PIM_SRCS=	route.c vif.c timer.c mrt.c pim.c pim_proto.c
PIM_OBJS=	route.o vif.o timer.o mrt.o pim.o pim_proto.o 
DVMRP_SRCS=	dvmrp_proto.c
DVMRP_OBJS=	dvmrp_proto.o
RSRR_SRCS=	rsrr.c
RSRR_OBJS=	rsrr.o
RSRR_HDRS=      rsrr.h rsrr_var.h
HDRS=		debug.h defs.h dvmrp.h igmpv2.h mrt.h pathnames.h pimdd.h \
		trace.h vif.h  ${RSRR_HDRS}
SRCS=		${IGMP_SRCS} ${ROUTER_SRCS} ${PIM_SRCS} ${DVMRP_SRCS} \
		${SNMP_SRCS} ${RSRR_SRCS}
OBJS=		${IGMP_OBJS} ${ROUTER_OBJS} ${PIM_OBJS} ${DVMRP_OBJS} \
		${SNMP_OBJS} ${RSRR_OBJS}
DISTFILES=	README CHANGES ${SRCS} ${HDRS} VERSION LICENSE \
		LICENSE.mrouted Makefile pimdd.conf BUGS.TODO include

all: ${PROG_NAME}

${PROG_NAME}: ${IGMP_OBJS} ${ROUTER_OBJS} ${PIM_OBJS} ${DVMRP_OBJS} \
		${RSRR_OBJS} ${CMULIBS}
	rm -f $@
	${CC} ${LDFLAGS} -o $@ ${CFLAGS} ${IGMP_OBJS} ${ROUTER_OBJS} \
	${PIM_OBJS} ${DVMRP_OBJS} ${RSRR_OBJS} ${LIBS}

purify: ${OBJS}
	${PURIFY} ${CC} ${LDFLAGS} -o ${PROG_NAME} ${CFLAGS} \
	${IGMP_OBJS} ${ROUTER_OBJS} ${PIM_OBJS} ${DVMRP_OBJS} ${RSRR_OBJS}  \
	${LIBS}


vers.c:	VERSION
	rm -f $@
	sed -e 's/.*/char todaysversion[]="&";/' < VERSION > vers.c

snmpd/libsnmpd.a:
	cd snmpd; make)

snmplib/libsnmp.a:
	(cd snmplib; make)

release: cvs-commit cvs-tag last-dist

re-release: cvs-commit cvs-retag last-dist

cvs-commit:
	cvs commit -m "make cvs-commit   VERSION=${PROG_VERSION}"

cvs-tag:
	cvs tag ${PROG_CVS_VERSION}
	`cat VERSION | sed 'y/./_/' | sed 'y/-/_/' > CVS_LAST_VERSION`

cvs-retag:
	cvs tag -F ${PROG_CVS_VERSION}
	`cat VERSION | sed 'y/./_/' | sed 'y/-/_/' > CVS_LAST_VERSION`

last-dist:
	- mv ${PROG_NAME} _${PROG_NAME}.SAVE_
	- rm -rf ${PROG_VERSION}
	- rm -rf ${PROG_VERSION}.tar.gz
	- rm -rf ${PROG_VERSION}.tar.gz.formail
	cvs export -r ${PROG_CVS_LAST_VERSION} ${PROG_NAME}
	mv ${PROG_NAME} ${PROG_VERSION}
	tar cvf - ${PROG_VERSION} | gzip > ${PROG_VERSION}.tar.gz
	uuencode ${PROG_VERSION}.tar.gz ${PROG_VERSION}.tar.gz > ${PROG_VERSION}.tar.gz.formail
	rm -rf ${PROG_VERSION}
	- mv _${PROG_NAME}.SAVE_ ${PROG_NAME}

curr-dist:
	- mv ${PROG_NAME} _${PROG_NAME}.SAVE_
	cvs commit -m "make curr-dist   VERSION=${PROG_VERSION}"
	- rm -rf ${PROG_NAME}-current
	- rm -rf ${PROG_NAME}-current.tar.gz
	- rm -rf ${PROG_NAME}-current.tar.gz.formail
	cvs checkout ${PROG_NAME}
	mv ${PROG_NAME} ${PROG_NAME}-current
	tar cvf - ${PROG_NAME}-current | gzip > ${PROG_NAME}-current.tar.gz
	uuencode ${PROG_NAME}-current.tar.gz ${PROG_NAME}-current.tar.gz > ${PROG_NAME}-current.tar.gz.formail
	rm -rf ${PROG_NAME}-current
	- mv _${PROG_NAME}.SAVE_ ${PROG_NAME}

curr-diff:
	cvs commit -m "make curr-diff   VERSION=${PROG_VERSION}"
	cvs rdiff -kk -u -r ${PROG_CVS_LAST_VERSION} ${PROG_NAME} > ${PROG_NAME}-current.diff


install:	${PROG_NAME}
	install -d /usr/local/bin
	install -m 0755 -f /usr/local/bin ${PROG_NAME}
	- mv /etc/pimdd.conf /etc/pimdd.conf.old
	cp pimdd.conf /etc
	echo "Don't forget to check/edit /etc/pimdd.conf!!!"

clean:	FRC ${SNMPCLEAN}
	rm -f ${OBJS} core ${PROG_NAME} tags TAGS

snmpclean:	FRC
	-(cd snmpd; make clean)
	-(cd snmplib; make clean)

depend:	FRC
	mkdep ${CFLAGS} ${SRCS}

lint:	FRC
	lint ${LINTFLAGS} ${SRCS}

tags:	${IGMP_SRCS} ${ROUTER_SRCS}
	ctags ${IGMP_SRCS} ${ROUTER_SRCS}

cflow:	FRC
	cflow ${MCAST_INCLUDE} ${IGMP_SRCS} ${ROUTER_SRCS} > cflow.out

cflow2:	FRC
	cflow -ix ${MCAST_INCLUDE} ${IGMP_SRCS} ${ROUTER_SRCS} > cflow2.out

rcflow:	FRC
	cflow -r ${MCAST_INCLUDE} ${IGMP_SRCS} ${ROUTER_SRCS} > rcflow.out

rcflow2: FRC
	cflow -r -ix ${MCAST_INCLUDE} ${IGMP_SRCS} ${ROUTER_SRCS} > rcflow2.out

TAGS:	FRC
	etags ${SRCS}

FRC:



# DO NOT DELETE THIS LINE -- mkdep uses it.
