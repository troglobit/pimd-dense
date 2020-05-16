/*
 *  Copyright (c) 1998 by the University of Southern California.
 *  All rights reserved.
 *
 *  Permission to use, copy, modify, and distribute this software and
 *  its documentation in source and binary forms for lawful
 *  purposes and without fee is hereby granted, provided
 *  that the above copyright notice appear in all copies and that both
 *  the copyright notice and this permission notice appear in supporting
 *  documentation, and that any documentation, advertising materials,
 *  and other materials related to such distribution and use acknowledge
 *  that the software was developed by the University of Southern
 *  California and/or Information Sciences Institute.
 *  The name of the University of Southern California may not
 *  be used to endorse or promote products derived from this software
 *  without specific prior written permission.
 *
 *  THE UNIVERSITY OF SOUTHERN CALIFORNIA DOES NOT MAKE ANY REPRESENTATIONS
 *  ABOUT THE SUITABILITY OF THIS SOFTWARE FOR ANY PURPOSE.  THIS SOFTWARE IS
 *  PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES,
 *  INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, TITLE, AND 
 *  NON-INFRINGEMENT.
 *
 *  IN NO EVENT SHALL USC, OR ANY OTHER CONTRIBUTOR BE LIABLE FOR ANY
 *  SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES, WHETHER IN CONTRACT,
 *  TORT, OR OTHER FORM OF ACTION, ARISING OUT OF OR IN CONNECTION WITH,
 *  THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 *  Other copyrights might apply to parts of this software and are so
 *  noted when applicable.
 */
/*
 *  Questions concerning this software should be directed to 
 *  Pavlin Ivanov Radoslavov (pavlin@catarina.usc.edu)
 *
 *  $Id: kern.c,v 1.2 1998/06/01 22:27:14 kurtw Exp $
 */
/*
 * Part of this program has been derived from mrouted.
 * The mrouted program is covered by the license in the accompanying file
 * named "LICENSE.mrouted".
 *
 * The mrouted program is COPYRIGHT 1989 by The Board of Trustees of
 * Leland Stanford Junior University.
 *
 */

#include "defs.h"

#ifdef RAW_OUTPUT_IS_RAW
int curttl = 0;
#endif


/*
 * Open/init the multicast routing in the kernel and sets the MRT_ASSERT
 * flag in the kernel.
 * 
 */
void
k_init_pim(socket)
  int socket;
{
    int v = 1;
    
    if (setsockopt(socket, IPPROTO_IP, MRT_INIT, (char *)&v, sizeof(int)) < 0)
	log(LOG_ERR, errno, "cannot enable multicast routing in kernel");
    
    if(setsockopt(socket, IPPROTO_IP, MRT_ASSERT, (char *)&v, sizeof(int)) < 0)
	log(LOG_ERR, errno, "cannot set ASSERT flag in kernel");
}


/*
 * Stops the multicast routing in the kernel and resets the MRT_ASSERT
 * flag in the kernel.
 */
void
k_stop_pim(socket)
    int socket;
{
    int v = 0;
    
    if (setsockopt(socket, IPPROTO_IP, MRT_DONE, (char *)NULL, 0) < 0)
	log(LOG_ERR, errno, "cannot disable multicast routing in kernel");
    
    if(setsockopt(socket, IPPROTO_IP, MRT_ASSERT, (char *)&v, sizeof(int)) < 0)
	log(LOG_ERR, errno, "cannot reset ASSERT flag in kernel");
}


/*
 * Set the socket receiving buffer. `bufsize` is the preferred size,
 * `minsize` is the smallest acceptable size.
 */
void k_set_rcvbuf(socket, bufsize, minsize)
    int socket;
    int bufsize;
    int minsize;
{
    int delta = bufsize / 2;
    int iter = 0;
    
    /*
     * Set the socket buffer.  If we can't set it as large as we
     * want, search around to try to find the highest acceptable
     * value.  The highest acceptable value being smaller than
     * minsize is a fatal error.
     */
    if (setsockopt(socket, SOL_SOCKET, SO_RCVBUF,
		   (char *)&bufsize, sizeof(bufsize)) < 0) {
	bufsize -= delta;
	while (1) {
	    iter++;
	    if (delta > 1)
	      delta /= 2;
	    
	    if (setsockopt(socket, SOL_SOCKET, SO_RCVBUF,
			   (char *)&bufsize, sizeof(bufsize)) < 0) {
		bufsize -= delta;
	    } else {
		if (delta < 1024)
		    break;
		bufsize += delta;
	    }
	}
	if (bufsize < minsize) {
	    log(LOG_ERR, 0, "OS-allowed buffer size %u < app min %u",
		bufsize, minsize);
	    /*NOTREACHED*/
	}
    }
    IF_DEBUG(DEBUG_KERN)
	log(LOG_DEBUG, 0, "Got %d byte buffer size in %d iterations",
	    bufsize, iter);
}


/*
 * Set/reset the IP_HDRINCL option. My guess is we don't need it for raw
 * sockets, but having it here won't hurt. Well, unless you are running
 * an older version of FreeBSD (older than 2.2.2). If the multicast
 * raw packet is bigger than 208 bytes, then IP_HDRINCL triggers a bug
 * in the kernel and "panic". The kernel patch for netinet/ip_raw.c
 * coming with this distribution fixes it.
 */
void k_hdr_include(socket, bool)
    int socket;
    int bool;
{
#ifdef IP_HDRINCL
    if (setsockopt(socket, IPPROTO_IP, IP_HDRINCL,
		   (char *)&bool, sizeof(bool)) < 0)
	log(LOG_ERR, errno, "setsockopt IP_HDRINCL %u", bool);
#endif
}


/*
 * Set the default TTL for the multicast packets outgoing from this
 * socket.
 * TODO: Does it affect the unicast packets?
 */
void k_set_ttl(socket, t)
    int socket;
    int t;
{
#ifdef RAW_OUTPUT_IS_RAW
    curttl = t;
#else
    u_char ttl;
    
    ttl = t;
    if (setsockopt(socket, IPPROTO_IP, IP_MULTICAST_TTL,
		   (char *)&ttl, sizeof(ttl)) < 0)
	log(LOG_ERR, errno, "setsockopt IP_MULTICAST_TTL %u", ttl);
#endif
}


/*
 * Set/reset the IP_MULTICAST_LOOP. Set/reset is specified by "flag".
 */
void k_set_loop(socket, flag)
    int socket;
    int flag;
{
    u_char loop;

    loop = flag;
    if (setsockopt(socket, IPPROTO_IP, IP_MULTICAST_LOOP,
		   (char *)&loop, sizeof(loop)) < 0)
	log(LOG_ERR, errno, "setsockopt IP_MULTICAST_LOOP %u", loop);
}


/*
 * Set the IP_MULTICAST_IF option on local interface ifa.
 */
void k_set_if(socket, ifa)
    int socket;
    u_int32 ifa;
{
    struct in_addr adr;

    adr.s_addr = ifa;
    if (setsockopt(socket, IPPROTO_IP, IP_MULTICAST_IF,
		   (char *)&adr, sizeof(adr)) < 0)
	log(LOG_ERR, errno, "setsockopt IP_MULTICAST_IF %s",
	    inet_fmt(ifa, s1));
}


/*
 * Join a multicast grp group on local interface ifa.
 */
void k_join(socket, grp, ifa)
    int socket;
    u_int32 grp;
    u_int32 ifa;
{
    struct ip_mreq mreq;
    
    mreq.imr_multiaddr.s_addr = grp;
    mreq.imr_interface.s_addr = ifa;

    if (setsockopt(socket, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		   (char *)&mreq, sizeof(mreq)) < 0)
	log(LOG_WARNING, errno, "cannot join group %s on interface %s",
	    inet_fmt(grp, s1), inet_fmt(ifa, s2));
}


/*
 * Leave a multicats grp group on local interface ifa.
 */
void k_leave(socket, grp, ifa)
    int socket;
    u_int32 grp;
    u_int32 ifa;
{
    struct ip_mreq mreq;

    mreq.imr_multiaddr.s_addr = grp;
    mreq.imr_interface.s_addr = ifa;
    
    if (setsockopt(socket, IPPROTO_IP, IP_DROP_MEMBERSHIP,
		   (char *)&mreq, sizeof(mreq)) < 0)
	log(LOG_WARNING, errno, "cannot leave group %s on interface %s",
	    inet_fmt(grp, s1), inet_fmt(ifa, s2));
}


/*
 * Add a virtual interface in the kernel.
 */
void k_add_vif(socket, vifi, v)
    int socket;
    vifi_t vifi;
    struct uvif *v;
{
    struct vifctl vc;

    vc.vifc_vifi            = vifi;
    /* TODO: only for DVMRP tunnels?
    vc.vifc_flags           = v->uv_flags & VIFF_KERNEL_FLAGS;
    */
    vc.vifc_flags           = v->uv_flags;
    vc.vifc_threshold       = v->uv_threshold;
    vc.vifc_rate_limit	    = v->uv_rate_limit;
    vc.vifc_lcl_addr.s_addr = v->uv_lcl_addr;
    vc.vifc_rmt_addr.s_addr = v->uv_rmt_addr;

    if (setsockopt(socket, IPPROTO_IP, MRT_ADD_VIF,
		   (char *)&vc, sizeof(vc)) < 0)
	log(LOG_ERR, errno, "setsockopt MRT_ADD_VIF on vif %d", vifi);
}


/*
 * Delete a virtual interface in the kernel.
 */
void k_del_vif(socket, vifi)
    int socket;
    vifi_t vifi;
{
    if (setsockopt(socket, IPPROTO_IP, MRT_DEL_VIF,
		   (char *)&vifi, sizeof(vifi)) < 0)
	log(LOG_ERR, errno, "setsockopt MRT_DEL_VIF on vif %d", vifi);
}


/*
 * Delete all MFC entries for particular routing entry from the kernel.
 */
int
k_del_mfc(socket, source, group)
    int socket;
    u_int32 source;
    u_int32 group;
{
    struct mfcctl mc;

    mc.mfcc_origin.s_addr   = source;
    mc.mfcc_mcastgrp.s_addr = group;
	
    if (setsockopt(socket, IPPROTO_IP, MRT_DEL_MFC, (char *)&mc,
		   sizeof(mc)) < 0) {
	log(LOG_WARNING, errno, "setsockopt k_del_mfc");
	return FALSE;
    }
	
    IF_DEBUG(DEBUG_MFC)
	log(LOG_DEBUG, 0, "Deleted MFC entry: src %s, grp %s",
	    inet_fmt(mc.mfcc_origin.s_addr, s1),
	    inet_fmt(mc.mfcc_mcastgrp.s_addr, s2));

    return(TRUE);
}


/*
 * Install/modify a MFC entry in the kernel
 */
int
k_chg_mfc(socket, source, group, iif, oifs)
    int socket;
    u_int32 source;
    u_int32 group;
    vifi_t iif;
    vifbitmap_t oifs;
{
    struct mfcctl mc;
    vifi_t vifi;
    struct uvif *v;

    mc.mfcc_origin.s_addr = source;
#ifdef OLD_KERNEL
    mc.mfcc_originmas.s_addr = 0xffffffff;    /* Got it from mrouted-3.9 */
#endif /* OLD_KERNEL */
    mc.mfcc_mcastgrp.s_addr = group;
    mc.mfcc_parent = iif;
    for (vifi = 0, v = uvifs; vifi < numvifs; vifi++, v++) {
	if (VIFM_ISSET(vifi, oifs))
	    mc.mfcc_ttls[vifi] = v->uv_threshold;
	else
	    mc.mfcc_ttls[vifi] = 0;
    }
    
    if (setsockopt(socket, IPPROTO_IP, MRT_ADD_MFC, (char *)&mc,
                   sizeof(mc)) < 0) {
        log(LOG_WARNING, errno,
	    "setsockopt MRT_ADD_MFC for source %s and group %s",
	    inet_fmt(source, s1), inet_fmt(group, s2));
        return(FALSE);
    }
    return(TRUE);
}


/*
 * Get packet counters for particular interface
 */
/*
 * XXX: TODO: currently not used, but keep just in case we need it later.
 */
int k_get_vif_count(vifi, retval)
    vifi_t vifi;
    struct vif_count *retval;
{
    struct sioc_vif_req vreq;
    
    vreq.vifi = vifi;
    if (ioctl(udp_socket, SIOCGETVIFCNT, (char *)&vreq) < 0) {
	log(LOG_WARNING, errno, "SIOCGETVIFCNT on vif %d", vifi);
	retval->icount = retval->ocount = retval->ibytes =
	    retval->obytes = 0xffffffff;
	return (1);
    }
    retval->icount = vreq.icount;
    retval->ocount = vreq.ocount;
    retval->ibytes = vreq.ibytes;
    retval->obytes = vreq.obytes;
    return (0);
}


/*
 * Gets the number of packets, bytes, and number op packets arrived
 * on wrong if in the kernel for particular (S,G) entry.
 */
int
k_get_sg_cnt(socket, source, group, retval)
    int socket;    /* udp_socket */
    u_int32 source;
    u_int32 group;
    struct sg_count *retval;
{
    struct sioc_sg_req sgreq;
    
    sgreq.src.s_addr = source;
    sgreq.grp.s_addr = group;
    if ((ioctl(socket, SIOCGETSGCNT, (char *)&sgreq) < 0)
	|| (sgreq.wrong_if == 0xffffffff)) {
	/* XXX: ipmulti-3.5 has bug in ip_mroute.c, get_sg_cnt():
	 * the return code is always 0, so this is why we need to check
	 * the wrong_if value.
	 */
	log(LOG_WARNING, errno, "SIOCGETSGCNT on (%s %s)",
	    inet_fmt(source, s1), inet_fmt(group, s2));
	retval->pktcnt = retval->bytecnt = retval->wrong_if = ~0;
	return(1);
    }
    retval->pktcnt = sgreq.pktcnt;
    retval->bytecnt = sgreq.bytecnt;
    retval->wrong_if = sgreq.wrong_if;
    return(0);
}



