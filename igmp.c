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
 *  $Id: igmp.c,v 1.2 1998/12/22 21:50:17 kurtw Exp $
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

/*
 * Exported variables.
 */
char    *igmp_recv_buf;		/* input packet buffer               */
char    *igmp_send_buf;  	/* output packet buffer              */
int     igmp_socket;	      	/* socket for all network I/O        */
u_int32 allhosts_group;	      	/* allhosts  addr in net order       */
u_int32 allrouters_group;	/* All-Routers addr in net order     */

/*
 * Local functions definitions.
 */
static void igmp_read        __P((int i, fd_set *rfd));
static void accept_igmp      __P((int recvlen));


/*
 * Open and initialize the igmp socket, and fill in the non-changing
 * IP header fields in the output packet buffer.
 */
void 
init_igmp()
{
    struct ip *ip;
    
    igmp_recv_buf = malloc(RECV_BUF_SIZE);
    igmp_send_buf = malloc(RECV_BUF_SIZE);

    if ((igmp_socket = socket(AF_INET, SOCK_RAW, IPPROTO_IGMP)) < 0) 
	log(LOG_ERR, errno, "IGMP socket");
    
    k_hdr_include(igmp_socket, TRUE);	/* include IP header when sending */
    k_set_rcvbuf(igmp_socket, SO_RECV_BUF_SIZE_MAX,
		 SO_RECV_BUF_SIZE_MIN); /* lots of input buffering        */
    k_set_ttl(igmp_socket, MINTTL);	/* restrict multicasts to one hop */
    k_set_loop(igmp_socket, FALSE);	/* disable multicast loopback     */
    
    ip         = (struct ip *)igmp_send_buf;
    ip->ip_v   = IPVERSION;
    ip->ip_hl  = (sizeof(struct ip) >> 2);
    ip->ip_tos = 0xc0;                  /* Internet Control   */
    ip->ip_id  = 0;                     /* let kernel fill in */
    ip->ip_off = 0;
    ip->ip_ttl = MAXTTL;		/* applies to unicasts only */
    ip->ip_p   = IPPROTO_IGMP;
#ifdef Linux
    ip->ip_csum = 0;                    /* let kernel fill in               */
#else
    ip->ip_sum = 0;                     /* let kernel fill in               */
#endif /* Linux */

    /* Everywhere in the daemon we use network-byte-order */    
    allhosts_group = htonl(INADDR_ALLHOSTS_GROUP);
    allrouters_group = htonl(INADDR_ALLRTRS_GROUP);
    
    if (register_input_handler(igmp_socket, igmp_read) < 0)
	log(LOG_ERR, 0, "Couldn't register igmp_read as an input handler");
}


/* Read an IGMP message */
static void
igmp_read(i, rfd)
    int i;
    fd_set *rfd;
{
    register int igmp_recvlen;
    int dummy = 0;
    
    igmp_recvlen = recvfrom(igmp_socket, igmp_recv_buf, RECV_BUF_SIZE,
			    0, NULL, &dummy);
    
    if (igmp_recvlen < 0) {
	if (errno != EINTR)
	    log(LOG_ERR, errno, "IGMP recvfrom");
	return;
    }
    
    /* TODO: make it as a thread in the future releases */
    accept_igmp(igmp_recvlen);
}


/*
 * Process a newly received IGMP packet that is sitting in the input
 * packet buffer.
 */
static void 
accept_igmp(recvlen)
    int recvlen;
{
    register u_int32 src, dst, group;
    struct ip *ip;
    struct igmp *igmp;
    int ipdatalen, iphdrlen, igmpdatalen;
    
    if (recvlen < sizeof(struct ip)) {
	log(LOG_WARNING, 0,
	    "received packet too short (%u bytes) for IP header", recvlen);
	return;
    }
    
    ip        = (struct ip *)igmp_recv_buf;
    src       = ip->ip_src.s_addr;
    dst       = ip->ip_dst.s_addr;
    
    /* packets sent up from kernel to daemon have ip->ip_p = 0 */
    if (ip->ip_p == 0) {
	if (src == 0 || dst == 0)
	    log(LOG_WARNING, 0, "kernel request not accurate, src %s dst %s",
		inet_fmt(src, s1), inet_fmt(dst, s2));
	else
	    process_kernel_call();
	return;
    }
    
    iphdrlen  = ip->ip_hl << 2;
#ifdef RAW_INPUT_IS_RAW
    ipdatalen = ntohs(ip->ip_len) - iphdrlen;
#else
    ipdatalen = ip->ip_len;
#endif
    if (iphdrlen + ipdatalen != recvlen) {
	log(LOG_WARNING, 0,
	    "received packet from %s shorter (%u bytes) than hdr+data length (%u+%u)",
	    inet_fmt(src, s1), recvlen, iphdrlen, ipdatalen);
	return;
    }
    
    igmp        = (struct igmp *)(igmp_recv_buf + iphdrlen);
    group       = igmp->igmp_group.s_addr;
    igmpdatalen = ipdatalen - IGMP_MINLEN;
    if (igmpdatalen < 0) {
	log(LOG_WARNING, 0,
	    "received IP data field too short (%u bytes) for IGMP, from %s",
	    ipdatalen, inet_fmt(src, s1));
	return;
    }

/* TODO: too noisy. Remove it? */
#ifdef NOSUCHDEF
    IF_DEBUG(DEBUG_PKT | debug_kind(IPPROTO_IGMP, igmp->igmp_type,
				    igmp->igmp_code))
	log(LOG_DEBUG, 0, "RECV %s from %-15s to %s",
	    packet_kind(IPPROTO_IGMP, igmp->igmp_type, igmp->igmp_code),
	    inet_fmt(src, s1), inet_fmt(dst, s2));
#endif /* NOSUCHDEF */
    
    switch (igmp->igmp_type) {
    case IGMP_MEMBERSHIP_QUERY:
	accept_membership_query(src, dst, group, igmp->igmp_code);
	return;
	
    case IGMP_V1_MEMBERSHIP_REPORT:
    case IGMP_V2_MEMBERSHIP_REPORT:
	accept_group_report(src, dst, group, igmp->igmp_type);
	return;
	
    case IGMP_V2_LEAVE_GROUP:
	accept_leave_message(src, dst, group);
	return;
	
    case IGMP_DVMRP:
	/* XXX: TODO: most of the stuff below is not implemented. We are still
	 * only PIM router.
	 */
	group = ntohl(group);

	switch (igmp->igmp_code) {
	case DVMRP_PROBE:
	    dvmrp_accept_probe(src, dst, (char *)(igmp+1), igmpdatalen, group);
	    return;
	    
	case DVMRP_REPORT:
	    dvmrp_accept_report(src, dst, (char *)(igmp+1), igmpdatalen,
				group);
	    return;

	case DVMRP_ASK_NEIGHBORS:
	    accept_neighbor_request(src, dst);
	    return;

	case DVMRP_ASK_NEIGHBORS2:
	    accept_neighbor_request2(src, dst);
	    return;
	    
	case DVMRP_NEIGHBORS:
	    dvmrp_accept_neighbors(src, dst, (u_char *)(igmp+1), igmpdatalen,
				   group);
	    return;

	case DVMRP_NEIGHBORS2:
	    dvmrp_accept_neighbors2(src, dst, (u_char *)(igmp+1), igmpdatalen,
				    group);
	    return;
	    
	case DVMRP_PRUNE:
	    dvmrp_accept_prune(src, dst, (char *)(igmp+1), igmpdatalen);
	    return;
	    
	case DVMRP_GRAFT:
	    dvmrp_accept_graft(src, dst, (char *)(igmp+1), igmpdatalen);
	    return;
	    
	case DVMRP_GRAFT_ACK:
	    dvmrp_accept_g_ack(src, dst, (char *)(igmp+1), igmpdatalen);
	    return;
	    
	case DVMRP_INFO_REQUEST:
	    dvmrp_accept_info_request(src, dst, (char *)(igmp+1), igmpdatalen);
	    return;

	case DVMRP_INFO_REPLY:
	    dvmrp_accept_info_reply(src, dst, (char *)(igmp+1), igmpdatalen);
	    return;
	    
	default:
	    log(LOG_INFO, 0,
		"ignoring unknown DVMRP message code %u from %s to %s",
		igmp->igmp_code, inet_fmt(src, s1), inet_fmt(dst, s2));
	    return;
	}
	
    case IGMP_PIM:
	return;    /* TODO: this is PIM v1 message. Handle it?. */
	
    case IGMP_MTRACE_RESP:
	return;    /* TODO: implement it */
	
    case IGMP_MTRACE:
	accept_mtrace(src, dst, group, (char *)(igmp+1), igmp->igmp_code,
		      igmpdatalen);
	return;
	
    default:
	log(LOG_INFO, 0,
	    "ignoring unknown IGMP message type %x from %s to %s",
	    igmp->igmp_type, inet_fmt(src, s1), inet_fmt(dst, s2));
	return;
    }
}

void
send_igmp(buf, src, dst, type, code, group, datalen)
    char *buf;
    u_int32 src, dst;
    int type, code;
    u_int32 group;
    int datalen;
{
    struct sockaddr_in sdst;
    struct ip *ip;
    struct igmp *igmp;
    int sendlen;
#ifdef RAW_OUTPUT_IS_RAW
    extern int curttl;
#endif /* RAW_OUTPUT_IS_RAW */
    int setloop = 0;

    /* Prepare the IP header */
    ip 			    = (struct ip *)buf;
    ip->ip_len              = sizeof(struct ip) + IGMP_MINLEN + datalen;
    ip->ip_src.s_addr       = src; 
    ip->ip_dst.s_addr       = dst;
    sendlen                 = ip->ip_len;
#ifdef RAW_OUTPUT_IS_RAW
    ip->ip_len              = htons(ip->ip_len);
#endif /* RAW_OUTPUT_IS_RAW */

    igmp                    = (struct igmp *)(buf + sizeof(struct ip));
    igmp->igmp_type         = type;
    igmp->igmp_code         = code;
    igmp->igmp_group.s_addr = group;
    igmp->igmp_cksum        = 0;
    igmp->igmp_cksum        = inet_cksum((u_int16 *)igmp,
					 IGMP_MINLEN + datalen);
    
    if (IN_MULTICAST(ntohl(dst))){
	k_set_if(igmp_socket, src);
	if (type != IGMP_DVMRP || dst == allhosts_group) {
	    setloop = 1;
	    k_set_loop(igmp_socket, TRUE);
	}
#ifdef RAW_OUTPUT_IS_RAW
	ip->ip_ttl = curttl;
    } else {
	ip->ip_ttl = MAXTTL;
#endif /* RAW_OUTPUT_IS_RAW */
    }
    
    bzero((void *)&sdst, sizeof(sdst));
    sdst.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
    sdst.sin_len = sizeof(sdst);
#endif
    sdst.sin_addr.s_addr = dst;
    if (sendto(igmp_socket, igmp_send_buf, sendlen, 0,
	       (struct sockaddr *)&sdst, sizeof(sdst)) < 0) {
	if (errno == ENETDOWN)
	    check_vif_state();
	else
	    log(log_level(IPPROTO_IGMP, type, code), errno,
		"sendto to %s on %s", inet_fmt(dst, s1), inet_fmt(src, s2));
	if (setloop)
	    k_set_loop(igmp_socket, FALSE);
	return;
    }
    
    if (setloop)
	k_set_loop(igmp_socket, FALSE);
    
    IF_DEBUG(DEBUG_PKT|debug_kind(IPPROTO_IGMP, type, code))
	log(LOG_DEBUG, 0, "SENT %s from %-15s to %s",
	    packet_kind(IPPROTO_IGMP, type, code),
	    src == INADDR_ANY_N ? "INADDR_ANY" :
	    inet_fmt(src, s1), inet_fmt(dst, s2));
}
