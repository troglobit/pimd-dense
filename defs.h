/*
 *  Copyright (c) 1998 by the University of Oregon.
 *  All rights reserved.
 *
 *  Permission to use, copy, modify, and distribute this software and
 *  its documentation in source and binary forms for lawful
 *  purposes and without fee is hereby granted, provided
 *  that the above copyright notice appear in all copies and that both
 *  the copyright notice and this permission notice appear in supporting
 *  documentation, and that any documentation, advertising materials,
 *  and other materials related to such distribution and use acknowledge
 *  that the software was developed by the University of Oregon.
 *  The name of the University of Oregon may not be used to endorse or 
 *  promote products derived from this software without specific prior 
 *  written permission.
 *
 *  THE UNIVERSITY OF OREGON DOES NOT MAKE ANY REPRESENTATIONS
 *  ABOUT THE SUITABILITY OF THIS SOFTWARE FOR ANY PURPOSE.  THIS SOFTWARE IS
 *  PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES,
 *  INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, TITLE, AND 
 *  NON-INFRINGEMENT.
 *
 *  IN NO EVENT SHALL UO, OR ANY OTHER CONTRIBUTOR BE LIABLE FOR ANY
 *  SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES, WHETHER IN CONTRACT,
 *  TORT, OR OTHER FORM OF ACTION, ARISING OUT OF OR IN CONNECTION WITH,
 *  THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 *  Other copyrights might apply to parts of this software and are so
 *  noted when applicable.
 */
/*
 *  Questions concerning this software should be directed to 
 *  Kurt Windisch (kurtw@antc.uoregon.edu)
 *
 *  $Id: defs.h,v 1.18 1998/12/22 21:50:17 kurtw Exp $
 */
/*
 * Part of this program has been derived from PIM sparse-mode pimd.
 * The pimd program is covered by the license in the accompanying file
 * named "LICENSE.pimd".
 *  
 * The pimd program is COPYRIGHT 1998 by University of Southern California.
 *
 * Part of this program has been derived from mrouted.
 * The mrouted program is covered by the license in the accompanying file
 * named "LICENSE.mrouted".
 * 
 * The mrouted program is COPYRIGHT 1989 by The Board of Trustees of
 * Leland Stanford Junior University.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 
#include <ctype.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h> 
#include <string.h> 
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#if ((defined(SYSV)) || (defined(BSDI)) || ((defined SunOS) && (SunOS < 50)))
#include <sys/sockio.h>
#endif /* SYSV || BSDI || SunOS 4.x */
#include <sys/time.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/igmp.h>
#ifdef __FreeBSD__      /* sigh */
#include <osreldate.h>
#endif /* __FreeBSD__ */
#if (defined(BSDI)) || (defined(__FreeBSD__) && (__FreeBSD_version >= 220000))
#define rtentry kernel_rtentry
#include <net/route.h>
#undef rtentry
#endif /* BSDI or __FreeBSD_version >= 220000 */
#include <netinet/ip_mroute.h>
#include <strings.h>
#ifdef RSRR
#include <sys/un.h>
#endif /* RSRR */

typedef u_int   u_int32;
typedef u_short u_int16;
typedef u_char  u_int8;

#ifndef __P
#ifdef __STDC__
#define __P(x)  x
#else
#define __P(x)  ()
#endif
#endif

typedef void (*cfunc_t) __P((void *));
typedef void (*ihfunc_t) __P((int, fd_set *));

#include "dvmrp.h"     /* Added for further compatibility and convenience */
#include "pimdd.h"
#include "mrt.h"
#include "igmpv2.h"
#include "vif.h"
#include "debug.h"
#include "pathnames.h"
#ifdef RSRR
#include "rsrr.h"
#include "rsrr_var.h"
#endif /* RSRR */

/*
 * Miscellaneous constants and macros
 */
/* #if (!(defined(BSDI)) && !(defined(KERNEL))) */
#ifndef KERNEL
#define max(a, b)               ((a) < (b) ? (b) : (a))
#define min(a, b)               ((a) > (b) ? (b) : (a))
#endif

/*
 * Various definitions to make it working for different platforms
 */
/* The old style sockaddr definition doesn't have sa_len */
#if (defined(BSD) && (BSD >= 199006)) /* sa_len was added with 4.3-Reno */ 
#define HAVE_SA_LEN
#endif

/* Versions of Solaris older than 2.6 don't have routing sockets. */
/* XXX TODO: check FreeBSD version and add all other platforms */
#if ((defined(SunOS) && SunOS >=56) || (defined FreeBSD) || (defined IRIX) || (defined BSDI) || defined(NetBSD))
#define HAVE_ROUTING_SOCKETS
#endif

#define TRUE			1
#define FALSE			0

#ifndef MAX
#define MAX(a,b) (((a) >= (b))? (a) : (b))
#define MIN(a,b) (((a) <= (b))? (a) : (b))
#endif /* MAX & MIN */

#define CREATE                  TRUE
#define DONT_CREATE             FALSE

#define EQUAL(s1, s2)		(strcmp((s1), (s2)) == 0)

/* obnoxious gcc gives an extraneous warning about this constant... */
#if defined(__STDC__) || defined(__GNUC__)
#define JAN_1970        2208988800UL    /* 1970 - 1900 in seconds */
#else
#define JAN_1970        2208988800L     /* 1970 - 1900 in seconds */
#define const           /**/
#endif


#define MINTTL			1  /* min TTL in the packets send locally */

#define MAX_IP_PACKET_LEN       576
#define MIN_IP_HEADER_LEN       20
#define MAX_IP_HEADER_LEN       60


/*
 * The IGMPv2 <netinet/in.h> defines INADDR_ALLRTRS_GROUP, but earlier
 * ones don't, so we define it conditionally here.
 */
#ifndef INADDR_ALLRTRS_GROUP
					/* address for multicast mtrace msg */
#define INADDR_ALLRTRS_GROUP	(u_int32)0xe0000002	/* 224.0.0.2 */
#endif

#ifndef INADDR_MAX_LOCAL_GROUP
define INADDR_MAX_LOCAL_GROUP        (u_int32)0xe00000ff     /* 224.0.0.255 */
#endif

#define INADDR_ANY_N            (u_int32)0x00000000     /* INADDR_ANY in
							 * network order */
#define CLASSD_PREFIX           (u_int32)0xe0000000     /* 224.0.0.0 */
#define ALL_MCAST_GROUPS_ADDR   (u_int32)0xe0000000     /* 224.0.0.0 */
#define ALL_MCAST_GROUPS_LENGTH 4

/* Used by DVMRP */
#define DEFAULT_METRIC		1	/* default subnet/tunnel metric     */
#define DEFAULT_THRESHOLD	1	/* default subnet/tunnel threshold  */

#define TIMER_INTERVAL		5	/* 5 sec virtual timer granularity  */

#ifdef RSRR
#define BIT_ZERO(X)             ((X) = 0)
#define BIT_SET(X,n)            ((X) |= 1 << (n))
#define BIT_CLR(X,n)            ((X) &= ~(1 << (n)))
#define BIT_TST(X,n)            ((X) & 1 << (n))
#endif /* RSRR */

#ifdef SYSV
#define bcopy(a, b, c)		memcpy((b), (a), (c))
#define bzero(s, n)		memset((s), 0, (n))
#define setlinebuf(s)		setvbuf((s), (NULL), (_IOLBF), 0)
#define RANDOM()                lrand48()
#else
#define RANDOM()                random()
#endif /* SYSV */

/*
 * External declarations for global variables and functions.
 */
#define			RECV_BUF_SIZE 64*1024  /* Maximum buff size to send
					        * or receive packet */
#define                 SO_RECV_BUF_SIZE_MAX 256*1024
#define                 SO_RECV_BUF_SIZE_MIN 48*1024

/* TODO: describe the variables and clean up */
extern char		*igmp_recv_buf;
extern char		*igmp_send_buf;
extern char		*pim_recv_buf;
extern char		*pim_send_buf;
extern int		igmp_socket;
extern int		pim_socket;
extern u_int32		allhosts_group;
extern u_int32		allrouters_group;
extern u_int32 		allpimrouters_group;
extern vifbitmap_t      nbr_vifs;

#ifdef RSRR
extern int              rsrr_socket;
#endif /* RSRR */

extern u_long		virtual_time;
extern char		configfilename[];

extern u_int32          default_source_metric;
extern u_int32          default_source_preference;

extern srcentry_t 	*srclist;
extern grpentry_t 	*grplist;

extern struct uvif	uvifs[MAXVIFS];
extern vifi_t		numvifs;
extern int              total_interfaces;
extern int              phys_vif;
extern int		udp_socket;

extern int		vifs_down;

extern char		s1[];
extern char		s2[];
extern char		s3[];
extern char		s4[];

#if !((defined(BSD) && (BSD >= 199103)) || defined(linux))
extern int		errno;
extern int		sys_nerr;
extern char *		sys_errlist[];
#endif


#ifndef IGMP_MEMBERSHIP_QUERY
#define IGMP_MEMBERSHIP_QUERY		IGMP_HOST_MEMBERSHIP_QUERY
#if !(defined(NetBSD))
#define IGMP_V1_MEMBERSHIP_REPORT	IGMP_HOST_MEMBERSHIP_REPORT
#define IGMP_V2_MEMBERSHIP_REPORT	IGMP_HOST_NEW_MEMBERSHIP_REPORT
#else
#define IGMP_V1_MEMBERSHIP_REPORT       IGMP_v1_HOST_MEMBERSHIP_REPORT
#define IGMP_V2_MEMBERSHIP_REPORT       IGMP_v2_HOST_MEMBERSHIP_REPORT
#endif
#define IGMP_V2_LEAVE_GROUP		IGMP_HOST_LEAVE_MESSAGE
#endif

#if defined(NetBSD)
#define IGMP_MTRACE_RESP                IGMP_MTRACE_REPLY
#define IGMP_MTRACE                     IGMP_MTRACE_QUERY
#endif

/* For timeout. The timers count down */
#define SET_TIMER(timer, value) (timer) = (value)
#define RESET_TIMER(timer) (timer) = 0
#define COPY_TIMER(timer_1, timer_2) (timer_2) = (timer_1)
#define IF_TIMER_SET(timer) if ((timer) > 0)
#define IF_TIMER_NOT_SET(timer) if ((timer) <= 0)
#define FIRE_TIMER(timer)       (timer) = 0

/* XXX Timer will never decrement below 0 - does this work with age_routes? 
 */
#define IF_TIMEOUT(timer)		\
	if (!((timer) -= (MIN(timer, TIMER_INTERVAL))))

#define IF_NOT_TIMEOUT(timer)		\
	if ((timer) -= (MIN(timer, TIMER_INTERVAL)))

#define TIMEOUT(timer)			\
	(!((timer) -= (MIN(timer, TIMER_INTERVAL))))

#define NOT_TIMEOUT(timer)		\
	((timer) -= (MIN(timer, TIMER_INTERVAL)))

#if 0
#define IF_TIMEOUT(value)     \
  if (!(((value) >= TIMER_INTERVAL) && ((value) -= TIMER_INTERVAL)))

#define IF_NOT_TIMEOUT(value) \
  if (((value) >= TIMER_INTERVAL) && ((value) -= TIMER_INTERVAL))

#define TIMEOUT(value)        \
     (!(((value) >= TIMER_INTERVAL) && ((value) -= TIMER_INTERVAL)))

#define NOT_TIMEOUT(value)    \
     (((value) >= TIMER_INTERVAL) && ((value) -= TIMER_INTERVAL))
#endif /* 0 */

#define ELSE else           /* To make emacs cc-mode happy */      


/*
 * External function definitions
 */

/* callout.c */
extern void     callout_init      __P((void));
extern void     free_all_callouts __P((void));
extern void     age_callout_queue __P((int));
extern int      timer_nextTimer   __P((void));
extern int      timer_setTimer    __P((int, cfunc_t, void *));
extern void     timer_clearTimer  __P((int));
extern int      timer_leftTimer   __P((int));

/* config.c */
extern void config_vifs_from_kernel __P(());
extern void config_vifs_from_file   __P(());

/* debug.c */
extern char     *packet_kind __P((u_int proto, u_int type, u_int code));
extern int      debug_kind   __P((u_int proto, u_int type, u_int code));
extern void     log          __P((int, int, char *, ...));
extern int      log_level    __P((u_int proto, u_int type, u_int code));
extern void     dump         __P((int i));
extern void     fdump        __P((int i));
extern void     cdump        __P((int i));
extern void     dump_vifs    __P((FILE *fp));
extern void     dump_pim_mrt __P((FILE *fp));

/* dvmrp_proto.c */
extern void	dvmrp_accept_probe             __P((u_int32 src, u_int32 dst,
						    char *p, int datalen,
						    u_int32 level));
extern void	dvmrp_accept_report            __P((u_int32 src, u_int32 dst,
						    char *p, int datalen,
						    u_int32 level));
extern void	dvmrp_accept_info_request      __P((u_int32 src, u_int32 dst,
						    u_char *p, int datalen));
extern void	dvmrp_accept_info_reply        __P((u_int32 src, u_int32 dst,
						    u_char *p, int datalen));
extern void	dvmrp_accept_neighbors         __P((u_int32 src, u_int32 dst,
						    u_char *p, int datalen,
						    u_int32 level));
extern void	dvmrp_accept_neighbors2        __P((u_int32 src, u_int32 dst,
						    u_char *p, int datalen,
						    u_int32 level));
extern void	dvmrp_accept_prune             __P((u_int32 src, u_int32 dst,
						    char *p, int datalen));
extern void	dvmrp_accept_graft             __P((u_int32 src, u_int32 dst,
						    char *p, int datalen));
extern void	dvmrp_accept_g_ack             __P((u_int32 src, u_int32 dst,
						    char *p, int datalen));

/* igmp.c */
extern void     init_igmp     __P(());
extern void     send_igmp     __P((char *buf, u_int32 src, u_int32 dst,
				   int type, int code, u_int32 group,
				   int datalen));

/* igmp_proto.c */
extern void     query_groups            __P((struct uvif *v));
extern void     accept_membership_query __P((u_int32 src, u_int32 dst,
					     u_int32 group, int tmo));
extern void     accept_group_report     __P((u_int32 src, u_int32 dst,
					     u_int32 group, int r_type));
extern void     accept_leave_message    __P((u_int32 src, u_int32 dst,
					     u_int32 group));
extern int      check_grp_membership    __P((struct uvif *v, u_int32 group));

/* inet.c */
extern int      inet_cksum        __P((u_int16 *addr, u_int len));
extern int      inet_valid_host   __P((u_int32 naddr));
extern int      inet_valid_mask   __P((u_int32 mask));
extern int      inet_valid_subnet __P((u_int32 nsubnet, u_int32 nmask));
extern char    *inet_fmt          __P((u_int32, char *s));
#ifdef NOSUCHDEF
extern char    *inet_fmts         __P((u_int32 addr, u_int32 mask, char *s));
#endif /* NOSUCHDEF */
extern char    *netname           __P((u_int32 addr, u_int32 mask));
extern u_int32  inet_parse        __P((char *s, int n));

/* kern.c */
extern void     k_set_rcvbuf    __P((int socket, int bufsize, int minsize));
extern void     k_hdr_include   __P((int socket, int bool));
extern void     k_set_ttl       __P((int socket, int t));
extern void     k_set_loop      __P((int socket, int l));
extern void     k_set_if        __P((int socket, u_int32 ifa));
extern void     k_join          __P((int socket, u_int32 grp, u_int32 ifa));
extern void     k_leave         __P((int socket, u_int32 grp, u_int32 ifa));
extern void     k_init_pim      __P(());
extern void     k_stop_pim      __P(());
extern int      k_del_mfc       __P((int socket, u_int32 source,
				     u_int32 group));
extern int      k_chg_mfc       __P((int socket, u_int32 source,
				     u_int32 group, vifi_t iif,
				     vifbitmap_t oifs));
extern void     k_add_vif       __P((int socket, vifi_t vifi, struct uvif *v));
extern void     k_del_vif       __P((int socket, vifi_t vifi));
extern int      k_get_vif_count __P((vifi_t vifi, struct vif_count *retval));
extern int      k_get_sg_cnt    __P((int socket, u_int32 source,
				     u_int32 group, struct sg_count *retval));

/* main.c */
extern int      register_input_handler __P((int fd, ihfunc_t func));

/* mrt.c */
extern void         init_pim_mrt             __P(());
extern mrtentry_t   *find_route              __P((u_int32 source,
						  u_int32 group,
						  u_int16 flags, char create));
extern grpentry_t   *find_group              __P((u_int32 group));
extern srcentry_t   *find_source             __P((u_int32 source));
extern void         delete_mrtentry          __P((mrtentry_t *mrtentry_ptr));
extern void         delete_srcentry          __P((srcentry_t *srcentry_ptr));
extern void         delete_grpentry          __P((grpentry_t *grpentry_ptr));

/* pim.c */
extern void         init_pim         __P(());
extern void         send_pim         __P((char *buf, u_int32 src, u_int32 dst,
					  int type, int datalen));
extern void         send_pim_unicast __P((char *buf, u_int32 src, u_int32 dst,
					  int type, int datalen));

/* pim_proto.c */
extern int receive_pim_hello         __P((u_int32 src, u_int32 dst,
					  char *pim_message, int datalen));
extern int send_pim_hello            __P((struct uvif *v, u_int16 holdtime));
extern void delete_pim_nbr           __P((pim_nbr_entry_t *nbr_delete));
extern int receive_pim_join_prune    __P((u_int32 src, u_int32 dst,
					  char *pim_message, int datalen));
extern int send_pim_jp               __P((mrtentry_t *mrtentry_ptr, int action,
					  vifi_t vifi, u_int32 target_addr, 
					  u_int16 holdtime));
extern int receive_pim_assert        __P((u_int32 src, u_int32 dst,
					  char *pim_message, int datalen));
extern int send_pim_assert           __P((u_int32 source, u_int32 group,
					  vifi_t vifi,
					  u_int32 local_preference,
					  u_int32 local_metric));
extern void delete_pim_graft_entry   __P((mrtentry_t *mrtentry_ptr));
extern int receive_pim_graft         __P((u_int32 src, u_int32 dst, 
					  char *pim_message, int datalen,
					  int pimtype));
extern int send_pim_graft            __P((mrtentry_t *mrtentry_ptr));


/* route.c */
extern int    set_incoming           __P((srcentry_t *srcentry_ptr,
					  int srctype));
extern vifi_t get_iif                __P((u_int32 source));
extern pim_nbr_entry_t *find_pim_nbr __P((u_int32 source));
extern int    add_sg_oif             __P((mrtentry_t *mrtentry_ptr,
					  vifi_t vifi,
					  u_int16 holdtime,
					  int update_holdtime));
extern void   add_leaf               __P((vifi_t vifi, u_int32 source,
					  u_int32 group));
extern void   delete_leaf            __P((vifi_t vifi, u_int32 source,
					  u_int32 group));
extern void   set_leaves             __P((mrtentry_t *mrtentry_ptr));
extern int    change_interfaces      __P((mrtentry_t *mrtentry_ptr,
					  vifi_t new_iif,
					  vifbitmap_t new_pruned_oifs,
					  vifbitmap_t new_leaves_));
extern void   calc_oifs              __P((mrtentry_t *mrtentry_ptr,
                                       vifbitmap_t *oifs_ptr));
extern void   process_kernel_call    __P(());
extern int    delete_vif_from_mrt    __P((vifi_t vifi));
extern void   trigger_join_alert     __P((mrtentry_t *mrtentry_ptr));
extern void   trigger_prune_alert    __P((mrtentry_t *mrtentry_ptr));


/* routesock.c */
extern int    k_req_incoming         __P((u_int32 source,
					  struct rpfctl *rpfp));
#ifdef HAVE_ROUTING_SOCKETS
extern int    init_routesock         __P(());
#endif /* HAVE_ROUTING_SOCKETS */

#ifdef RSRR
#define gtable mrtentry
#define RSRR_NOTIFICATION_OK        TRUE
#define RSRR_NOTIFICATION_FALSE    FALSE

/* rsrr.c */
extern void             rsrr_init        __P((void));
extern void             rsrr_clean       __P((void));
extern void             rsrr_cache_send  __P((struct gtable *, int));
extern void             rsrr_cache_clean __P((struct gtable *));
extern void             rsrr_cache_bring_up __P((struct gtable *));
#endif /* RSRR */

/* timer.c */
extern void init_timers                 __P(());
extern void age_vifs                    __P(());
extern void age_routes                  __P(());
extern int  clean_srclist               __P(());

/* trace.c */
/* u_int is promoted u_char */
extern void	accept_mtrace            __P((u_int32 src, u_int32 dst,
					      u_int32 group, char *data,
					      u_int no, int datalen));
extern void	accept_neighbor_request  __P((u_int32 src, u_int32 dst));
extern void	accept_neighbor_request2 __P((u_int32 src, u_int32 dst));

/* vif.c */
extern void    init_vifs               __P(());
extern void    stop_all_vifs           __P(());
extern void    check_vif_state         __P(());
extern vifi_t  local_address           __P((u_int32 src));
extern vifi_t  find_vif_direct         __P((u_int32 src));
extern vifi_t  find_vif_direct_local   __P((u_int32 src));
extern u_int32 max_local_address       __P((void));

