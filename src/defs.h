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
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h> 
#include <ctype.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h> 
#include <string.h> 
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#if ((defined(SYSV)) || (defined(BSDI)) || ((defined SunOS) && (SunOS < 50)))
#include <sys/sockio.h>
#endif /* SYSV || BSDI || SunOS 4.x */
#include <sys/time.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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

#ifdef __linux__
#define _LINUX_IN_H             /* For Linux <= 2.6.25 */
#include <linux/types.h>
#include <linux/mroute.h>
#else
#include <netinet/ip_mroute.h>
#endif /* __linux__ */

#include <strings.h>
#ifdef RSRR
#include <sys/un.h>
#endif /* RSRR */
#include <time.h>

typedef u_int   u_int32;
typedef u_short u_int16;
typedef u_char  u_int8;

typedef void (*cfunc_t) (void *);
typedef void (*ihfunc_t) (int, fd_set *);

#include "dvmrp.h"     /* Added for further compatibility and convenience */
#include "pimdd.h"
#include "mrt.h"
#include "igmpv2.h"
#include "igmpv3.h"
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

/*
 * workaround for SunOS/Solaris/Illumos which defines this in
 * addition to the more logical __sun and __svr4__ macros.
 */
#ifdef sun
#undef sun
#endif

#define TRUE			1
#define FALSE			0

#ifndef MAX
#define MAX(a,b) (((a) >= (b))? (a) : (b))
#define MIN(a,b) (((a) <= (b))? (a) : (b))
#endif

/* From The Practice of Programming, by Kernighan and Pike */
#ifndef NELEMS
#define NELEMS(array) (sizeof(array) / sizeof(array[0]))
#endif

#define CREATE                  TRUE
#define DONT_CREATE             FALSE

#define EQUAL(s1, s2)		(strcmp((s1), (s2)) == 0)

#define JAN_1970                2208988800UL    /* 1970 - 1900 in seconds */

#define MINTTL			1  /* min TTL in the packets send locally */

#define MAX_IP_PACKET_LEN       576
#define MIN_IP_HEADER_LEN       20
#define MAX_IP_HEADER_LEN       60


/*
 * The IGMPv2 <netinet/in.h> defines INADDR_ALLRTRS_GROUP, but earlier
 * ones don't, so we define it conditionally here.
 */
#ifndef INADDR_ALLRTRS_GROUP 	/* address for multicast mtrace msg */
#define INADDR_ALLRTRS_GROUP	(in_addr_t)0xe0000002	/* 224.0.0.2 */
#endif

#ifndef INADDR_ALLRPTS_GROUP
#define INADDR_ALLRPTS_GROUP    (in_addr_t)0xe0000016	/* 224.0.0.22, IGMPv3 */
#endif

#ifndef INADDR_MAX_LOCAL_GROUP
#define INADDR_MAX_LOCAL_GROUP  (in_addr_t)0xe00000ff	/* 224.0.0.255 */
#endif

#define INADDR_ANY_N            (in_addr_t)0x00000000	/* INADDR_ANY in network order */
#define CLASSD_PREFIX           (in_addr_t)0xe0000000	/* 224.0.0.0 */
#define ALL_MCAST_GROUPS_ADDR   (in_addr_t)0xe0000000	/* 224.0.0.0 */
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
extern u_int		assert_timeout;
extern in_addr_t	allhosts_group;
extern in_addr_t	allrouters_group;
extern in_addr_t	allreports_group;
extern in_addr_t	allpimrouters_group;
extern vifbitmap_t      nbr_vifs;

#ifdef RSRR
extern int              rsrr_socket;
#endif /* RSRR */

extern u_long		virtual_time;
extern char		*config_file;
extern char		*prognm;
extern char		*ident;
extern char             versionstring[];

extern u_int32          default_route_metric;
extern u_int32          default_route_distance;

extern srcentry_t 	*srclist;
extern grpentry_t 	*grplist;

extern struct uvif	uvifs[MAXVIFS];
extern vifi_t		numvifs;
extern int              total_interfaces;
extern int              phys_vif;
extern int		udp_socket;

extern int		vifs_down;

extern char		s1[19];
extern char		s2[19];
extern char		s3[19];
extern char		s4[19];

#if !((defined(BSD) && (BSD >= 199103)) || defined(__linux__))
extern int		errno;
extern int		sys_nerr;
extern char *		sys_errlist[];
#endif


#ifndef IGMP_MEMBERSHIP_QUERY
#define IGMP_MEMBERSHIP_QUERY		IGMP_HOST_MEMBERSHIP_QUERY
#if !(defined(NetBSD) || defined(OpenBSD) || defined(__FreeBSD__))
#define IGMP_V1_MEMBERSHIP_REPORT	IGMP_HOST_MEMBERSHIP_REPORT
#define IGMP_V2_MEMBERSHIP_REPORT	IGMP_HOST_NEW_MEMBERSHIP_REPORT
#else
#define IGMP_V1_MEMBERSHIP_REPORT       IGMP_v1_HOST_MEMBERSHIP_REPORT
#define IGMP_V2_MEMBERSHIP_REPORT       IGMP_v2_HOST_MEMBERSHIP_REPORT
#endif
#define IGMP_V2_LEAVE_GROUP		IGMP_HOST_LEAVE_MESSAGE
#endif
#if defined(__FreeBSD__)		/* From FreeBSD 8.x */
#define IGMP_V3_MEMBERSHIP_REPORT       IGMP_v3_HOST_MEMBERSHIP_REPORT
#else
#define IGMP_V3_MEMBERSHIP_REPORT	0x22	/* Ver. 3 membership report */
#endif

#if defined(NetBSD) || defined(OpenBSD) || defined(__FreeBSD__)
#define IGMP_MTRACE_RESP                IGMP_MTRACE_REPLY
#define IGMP_MTRACE                     IGMP_MTRACE_QUERY
#endif

#ifndef IGMP_TIMER_SCALE
#define IGMP_TIMER_SCALE     10	    /* denotes that the igmp->timer filed */
				    /* specifies time in 10th of seconds  */
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
extern void     callout_init      (void);
extern void     free_all_callouts (void);
extern void     age_callout_queue (int);
extern int      timer_nextTimer   (void);
extern int      timer_setTimer    (int, cfunc_t, void *);
extern void     timer_clearTimer  (int);
extern int      timer_leftTimer   (int);

/* config.c */
extern void     config_set_ifflag       (uint32_t flag);
struct uvif    *config_find_ifname      (char *nm);
struct uvif    *config_find_ifaddr      (in_addr_t addr);
extern void	config_vifs_correlate	(void);
extern void     config_vifs_from_kernel (void);

/* cfparse.y */
extern void     config_vifs_from_file   ();

/* debug.c */
extern char     dumpfilename[];
extern int      log_syslog;
extern int      log_level;

extern char    *packet_kind  (u_int proto, u_int type, u_int code);
extern int      debug_kind   (u_int proto, u_int type, u_int code);
extern void     logit        (int, int, const char *, ...) __attribute__((format (printf, 3, 4)));
extern int      log_severity (u_int proto, u_int type, u_int code);

/* dvmrp_proto.c */
extern void	dvmrp_accept_probe        (u_int32 src, u_int32 dst, char *p, int datalen, u_int32 level);
extern void	dvmrp_accept_report       (u_int32 src, u_int32 dst, char *p, int datalen, u_int32 level);
extern void	dvmrp_accept_info_request (u_int32 src, u_int32 dst, u_char *p, int datalen);
extern void	dvmrp_accept_info_reply   (u_int32 src, u_int32 dst, u_char *p, int datalen);
extern void	dvmrp_accept_neighbors    (u_int32 src, u_int32 dst, u_char *p, int datalen, u_int32 level);
extern void	dvmrp_accept_neighbors2   (u_int32 src, u_int32 dst, u_char *p, int datalen, u_int32 level);
extern void	dvmrp_accept_prune        (u_int32 src, u_int32 dst, char *p, int datalen);
extern void	dvmrp_accept_graft        (u_int32 src, u_int32 dst, char *p, int datalen);
extern void	dvmrp_accept_g_ack        (u_int32 src, u_int32 dst, char *p, int datalen);

/* igmp.c */
extern void     init_igmp     ();
extern void     igmp_clean    ();
extern void     send_igmp     (char *buf, u_int32 src, u_int32 dst, int type, int code, u_int32 group, int datalen);

/* igmp_proto.c */
extern void     query_groups            (struct uvif *v);
extern void     accept_membership_query (u_int32 src, u_int32 dst, u_int32 group, int tmo, int ver);
extern void     accept_group_report     (u_int32 src, u_int32 dst, u_int32 group, int r_type);
extern void     accept_leave_message    (u_int32 src, u_int32 dst, u_int32 group);
extern void     accept_membership_report(uint32_t src, uint32_t dst, struct igmpv3_report *report, ssize_t reportlen);
extern int      check_grp_membership    (struct uvif *v, u_int32 group);

/* inet.c */
extern int      inet_cksum        (u_int16 *addr, u_int len);
extern int      inet_valid_host   (u_int32 naddr);
extern int      inet_valid_mask   (u_int32 mask);
extern int      inet_valid_subnet (u_int32 nsubnet, u_int32 nmask);
extern char    *inet_fmt          (u_int32, char *s);
#ifdef NOSUCHDEF
extern char    *inet_fmts         (u_int32 addr, u_int32 mask, char *s);
#endif /* NOSUCHDEF */
extern char    *netname           (u_int32 addr, u_int32 mask);
extern u_int32  inet_parse        (char *s, int n);

/* ipc.c */
extern void	ipc_init	(void);
extern void	ipc_exit	(void);

/* kern.c */
extern void     k_set_rcvbuf    (int socket, int bufsize, int minsize);
extern void     k_hdr_include   (int socket, int bool);
extern void     k_set_ttl       (int socket, int t);
extern void     k_set_loop      (int socket, int l);
extern void     k_set_if        (int socket, u_int32 ifa);
extern void     k_join          (int socket, u_int32 grp, u_int32 ifa);
extern void     k_leave         (int socket, u_int32 grp, u_int32 ifa);
extern void     k_init_pim      ();
extern void     k_stop_pim      ();
extern int      k_del_mfc       (int socket, u_int32 source, u_int32 group);
extern int      k_chg_mfc       (int socket, u_int32 source, u_int32 group, vifi_t iif, vifbitmap_t oifs);
extern void     k_add_vif       (int socket, vifi_t vifi, struct uvif *v);
extern void     k_del_vif       (int socket, vifi_t vifi, struct uvif *v);
extern int      k_get_vif_count (vifi_t vifi, struct vif_count *retval);
extern int      k_get_sg_cnt    (int socket, u_int32 source, u_int32 group, struct sg_count *retval);

/* main.c */
extern int      foreground;
extern char    *prognm;

extern int      register_input_handler	(int fd, ihfunc_t func);
extern int	daemon_restart		(char *buf, size_t len);
extern int	daemon_kill		(char *buf, size_t len);

/* mrt.c */
extern void         init_pim_mrt     ();
extern void         mrt_clean        ();
extern mrtentry_t   *find_route      (u_int32 source, u_int32 group, u_int16 flags, char create);
extern grpentry_t   *find_group      (u_int32 group);
extern srcentry_t   *find_source     (u_int32 source);
extern void         delete_mrtentry  (mrtentry_t *mrtentry_ptr);
extern void         delete_srcentry  (srcentry_t *srcentry_ptr);
extern void         delete_grpentry  (grpentry_t *grpentry_ptr);

/* pim.c */
extern void         init_pim         ();
extern void         pim_clean        ();
extern void         send_pim         (char *buf, u_int32 src, u_int32 dst, int type, int datalen);
extern void         send_pim_unicast (char *buf, u_int32 src, u_int32 dst, int type, int datalen);

/* pim_proto.c */
extern int receive_pim_hello         (u_int32 src, u_int32 dst, char *pim_message, ssize_t datalen);
extern int send_pim_hello            (struct uvif *v, u_int16 holdtime);
extern void delete_pim_nbr           (pim_nbr_entry_t *nbr_delete);
extern int receive_pim_join_prune    (u_int32 src, u_int32 dst, char *pim_message, int datalen);
extern int send_pim_jp               (mrtentry_t *mrtentry_ptr, int action, vifi_t vifi, u_int32 target_addr, u_int16 holdtime);
extern int receive_pim_assert        (u_int32 src, u_int32 dst, char *pim_message, int datalen);
extern int send_pim_assert           (u_int32 source, u_int32 group, vifi_t vifi, u_int32 local_preference, u_int32 local_metric);
extern void delete_pim_graft_entry   (mrtentry_t *mrtentry_ptr);
extern int receive_pim_graft         (u_int32 src, u_int32 dst,  char *pim_message, int datalen, int pimtype);
extern int send_pim_graft            (mrtentry_t *mrtentry_ptr);

/* route.c */
extern int    set_incoming           (srcentry_t *srcentry_ptr, int srctype);
extern pim_nbr_entry_t *find_pim_nbr (u_int32 source);
extern int    add_sg_oif             (mrtentry_t *mrtentry_ptr, vifi_t vifi, u_int16 holdtime, int update_holdtime);
extern void   add_leaf               (vifi_t vifi, u_int32 source, u_int32 group);
extern void   delete_leaf            (vifi_t vifi, u_int32 source, u_int32 group);
extern void   set_leaves             (mrtentry_t *mrtentry_ptr);
extern int    change_interfaces      (mrtentry_t *mrtentry_ptr, vifi_t new_iif, vifbitmap_t new_pruned_oifs, vifbitmap_t new_leaves_);
extern void   calc_oifs              (mrtentry_t *mrtentry_ptr, vifbitmap_t *oifs_ptr);
extern void   process_kernel_call    ();
extern int    delete_vif_from_mrt    (vifi_t vifi);
extern void   trigger_join_alert     (mrtentry_t *mrtentry_ptr);
extern void   trigger_prune_alert    (mrtentry_t *mrtentry_ptr);


/* routesock.c */
extern int    init_routesock         ();
extern void   routesock_clean        ();
extern int    k_req_incoming         (u_int32 source, struct rpfctl *rpfp);
extern int    routing_socket;

#ifdef RSRR
#define gtable mrtentry
#define RSRR_NOTIFICATION_OK        TRUE
#define RSRR_NOTIFICATION_FALSE    FALSE

/* rsrr.c */
extern void             rsrr_init        (void);
extern void             rsrr_clean       (void);
extern void             rsrr_cache_send  (struct gtable *, int);
extern void             rsrr_cache_clean (struct gtable *);
extern void             rsrr_cache_bring_up (struct gtable *);
#endif /* RSRR */

/* timer.c */
extern void init_timers                 ();
extern void age_vifs                    ();
extern void age_routes                  ();
extern int  clean_srclist               ();

/* trace.c */
/* u_int is promoted u_char */
extern void	accept_mtrace            (u_int32 src, u_int32 dst, u_int32 group, char *data, u_int no, int datalen);
extern void	accept_neighbor_request  (u_int32 src, u_int32 dst);
extern void	accept_neighbor_request2 (u_int32 src, u_int32 dst);

/* vif.c */
extern void    init_vifs               ();
extern void    stop_all_vifs           ();
extern void    check_vif_state         ();
extern vifi_t  local_address           (u_int32 src);
extern vifi_t  find_vif_direct         (u_int32 src);
extern vifi_t  find_vif_direct_local   (u_int32 src);
extern u_int32 max_local_address       (void);

/* compat declarations */
#ifndef strlcpy
extern size_t  strlcpy		     (char *dst, const char *src, size_t siz);
#endif

#ifndef strlcat
extern size_t  strlcat		     (char *dst, const char *src, size_t siz);
#endif

#ifndef tempfile
extern FILE   *tempfile		     (void);
#endif

/**
 * Local Variables:
 *  c-file-style: "cc-mode"
 * End:
 */
