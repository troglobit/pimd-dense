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
 *  $Id: pim.c,v 1.7 1998/12/22 21:50:17 kurtw Exp $
 */

#include "defs.h"

/*
 * Exported variables.
 */
char	*pim_recv_buf;		/* input packet buffer   */
char	*pim_send_buf;		/* output packet buffer  */

u_int32	allpimrouters_group;	/* ALL_PIM_ROUTERS group (net order) */
int	pim_socket;		/* socket for PIM control msgs */

u_int   assert_timeout;		/* 5-210, default: 180 */

/*
 * Local function definitions.
 */
static void pim_read   (int f, fd_set *rfd);
static void accept_pim (ssize_t recvlen);


void
init_pim()
{
    u_char *ip_opt;
    struct ip *ip;
    
    assert_timeout = PIM_ASSERT_TIMEOUT;

    if ((pim_socket = socket(AF_INET, SOCK_RAW, IPPROTO_PIM)) < 0) 
	logit(LOG_ERR, errno, "PIM socket");

    k_hdr_include(pim_socket, TRUE);      /* include IP header when sending */
    k_set_rcvbuf(pim_socket, SO_RECV_BUF_SIZE_MAX,
		 SO_RECV_BUF_SIZE_MIN);   /* lots of input buffering        */
    k_set_ttl(pim_socket, MINTTL);	  /* restrict multicasts to one hop */
    k_set_loop(pim_socket, FALSE);	  /* disable multicast loopback     */

    allpimrouters_group = htonl(INADDR_ALL_PIM_ROUTERS);

    pim_recv_buf = calloc(1, RECV_BUF_SIZE);
    if (!pim_recv_buf)
	    logit(LOG_ERR, 0, "%s(): out of memory", __func__);
    pim_send_buf = calloc(1, RECV_BUF_SIZE);
    if (!pim_send_buf)
	    logit(LOG_ERR, 0, "%s(): out of memory", __func__);

    /* One time setup in the buffers */
    ip           = (struct ip *)pim_send_buf;
    ip->ip_v     = IPVERSION;
    ip->ip_hl    = (IP_HEADER_RAOPT_LEN >> 2);
    ip->ip_tos   = 0xc0;                  /* Internet Control   */
    ip->ip_id    = 0;                     /* let kernel fill in */
    ip->ip_off   = 0;
    ip->ip_p     = IPPROTO_PIM;
    ip->ip_sum   = 0;                     /* let kernel fill in */

    /*
     * RFC2113 IP Router Alert.  Per spec this is required to
     * force certain routers/switches to inspect this frame.
     */
    ip_opt    = (u_char *)(pim_send_buf + IP_HEADER_RAOPT_LEN);
    ip_opt[0] = IPOPT_RA;
    ip_opt[1] = 4;
    ip_opt[2] = 0;
    ip_opt[3] = 0;

    if (register_input_handler(pim_socket, pim_read) < 0)
	logit(LOG_ERR, 0,  "cannot register pim_read() as an input handler");

    VIFM_CLRALL(nbr_vifs);
}


void
pim_clean(void)
{
    struct uvif *v;
    vifi_t vifi;

    /* inform all neighbors that I'm going to die */
    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	if ((v->uv_flags & VIFF_DISABLED) == 0)
	    send_pim_hello(v, 0);
    }

    if (pim_socket > 0)
	close(pim_socket);
    if (pim_recv_buf)
	free(pim_recv_buf);
    if (pim_send_buf)
	free(pim_send_buf);
    pim_recv_buf = NULL;
    pim_send_buf = NULL;
    pim_socket = 0;
}


/* Read a PIM message */
static void
pim_read(f, rfd)
    int f;
    fd_set *rfd;
{
    ssize_t pim_recvlen;
    socklen_t dummy = 0;
    sigset_t block, oblock;
    
    pim_recvlen = recvfrom(pim_socket, pim_recv_buf, RECV_BUF_SIZE,
			   0, NULL, &dummy);
    
    if (pim_recvlen < 0) {
	if (errno != EINTR)
	    logit(LOG_ERR, errno, "PIM recvfrom");
	return;
    }

    sigemptyset(&block);
    sigaddset(&block, SIGALRM);
    if (sigprocmask(SIG_BLOCK, &block, &oblock) < 0)
	logit(LOG_ERR, errno, "sigprocmask");
    
    accept_pim(pim_recvlen);
    
    sigprocmask(SIG_SETMASK, &oblock, (sigset_t *)NULL);
}


static void
accept_pim(recvlen)
    ssize_t recvlen;
{
    u_int32 src, dst;
    struct ip *ip;
    pim_header_t *pim;
    ssize_t iphdrlen, pimlen;
    
    if (recvlen < MIN_IP_HEADER_LEN) {
	logit(LOG_WARNING, 0, "packet too short (%zd bytes) for IP header", recvlen);
	return;
    }
    
    ip	        = (struct ip *)pim_recv_buf;
    src	        = ip->ip_src.s_addr;
    dst	        = ip->ip_dst.s_addr;
    iphdrlen    = ip->ip_hl << 2;
    
    pim         = (pim_header_t *)(pim_recv_buf + iphdrlen);
    pimlen	= recvlen - iphdrlen;
    if (pimlen < (ssize_t)sizeof(*pim)) {
	logit(LOG_WARNING, 0, "IP data field too short (%zd bytes) for PIM header, from %s to %s",
	      pimlen, inet_fmt(src, s1), inet_fmt(dst, s2));
	return;
    }
    
#ifdef NOSUCHDEF   /* TODO: delete. Too noisy */
    IF_DEBUG(DEBUG_PIM_DETAIL) {
	IF_DEBUG(DEBUG_PIM) {
	    logit(LOG_DEBUG, 0, "Receiving %s from %-15s to %s ",
		packet_kind(IPPROTO_PIM, pim->pim_type, 0), 
		inet_fmt(src, s1), inet_fmt(dst, s2));
	    logit(LOG_DEBUG, 0, "PIM type is %u", pim->pim_type);
	}
    }
#endif /* NOSUCHDEF */


    /* TODO: Check PIM version */
    /* TODO: check the dest. is ALL_PIM_ROUTERS (if multicast address) */
    /* TODO: Checksum verification is done in each of the processing functions.
     * No need for chechsum, if already done in the kernel?
     */
    switch (pim->pim_type) {
    case PIM_HELLO:      
	receive_pim_hello(src, dst, (char *)(pim), pimlen); 
	break;
    case PIM_REGISTER:   
	logit(LOG_INFO, 0, "ignore %s from %s to %s",
	    packet_kind(IPPROTO_PIM, pim->pim_type, 0), inet_fmt(src, s1),
	    inet_fmt(dst, s2));
	break;
    case PIM_REGISTER_STOP:   
	logit(LOG_INFO, 0, "ignore %s from %s to %s",
	    packet_kind(IPPROTO_PIM, pim->pim_type, 0), inet_fmt(src, s1),
	    inet_fmt(dst, s2));
	break;
    case PIM_JOIN_PRUNE: 
	receive_pim_join_prune(src, dst, (char *)(pim), pimlen); 
	break;
    case PIM_BOOTSTRAP:
	logit(LOG_INFO, 0, "ignore %s from %s to %s",
	    packet_kind(IPPROTO_PIM, pim->pim_type, 0), inet_fmt(src, s1),
	    inet_fmt(dst, s2));
	break;
    case PIM_ASSERT:     
	receive_pim_assert(src, dst, (char *)(pim), pimlen); 
	break;
    case PIM_GRAFT:
    case PIM_GRAFT_ACK:
	receive_pim_graft(src, dst, (char *)(pim), pimlen, pim->pim_type);
	break;
    case PIM_CAND_RP_ADV:
	logit(LOG_INFO, 0, "ignore %s from %s to %s",
	    packet_kind(IPPROTO_PIM, pim->pim_type, 0), inet_fmt(src, s1),
	    inet_fmt(dst, s2));
	break;
    default:
	logit(LOG_INFO, 0,
	    "ignore unknown PIM message code %u from %s to %s",
	    pim->pim_type, inet_fmt(src, s1), inet_fmt(dst, s2));
	break;
    }
}


/*
 * Send a multicast PIM packet from src to dst, PIM message type = "type"
 * and data length (after the PIM header) = "datalen"
 */
void 
send_pim(buf, src, dst, type, datalen)
    char *buf;
    u_int32 src, dst;
    int type, datalen;
{
    struct sockaddr_in sdst;
    struct ip *ip;
    pim_header_t *pim;
    int sendlen;
#ifdef RAW_OUTPUT_IS_RAW
    extern int curttl;
#endif /* RAW_OUTPUT_IS_RAW */
    int setloop = 0;

    /* Prepare the IP header */
    ip                 = (struct ip *)buf;
    ip->ip_len	       = IP_HEADER_RAOPT_LEN + sizeof(pim_header_t) + datalen;
    ip->ip_id          = 0;                 /* let kernel fill in */
    ip->ip_off         = 0;
    ip->ip_src.s_addr  = src;
    ip->ip_dst.s_addr  = dst;
    ip->ip_ttl         = MAXTTL;            /* applies to unicast only */
    sendlen            = ip->ip_len;
#ifdef RAW_OUTPUT_IS_RAW
    ip->ip_len         = htons(ip->ip_len);
#endif
    
    /* Prepare the PIM packet */
    pim		       = (pim_header_t *)(buf + IP_HEADER_RAOPT_LEN);
    pim->pim_type      = type;
    pim->pim_vers      = PIM_PROTOCOL_VERSION;
    pim->pim_reserved  = 0;
    pim->pim_cksum     = 0;
   /* TODO: XXX: if start using this code for PIM_REGISTERS, exclude the
    * encapsulated packet from the checsum.
    */
    pim->pim_cksum      = inet_cksum((u_int16 *)pim,
				    sizeof(pim_header_t) + datalen);
    
    if (IN_MULTICAST(ntohl(dst))) {
	k_set_if(pim_socket, src);
	if ((dst == allhosts_group) || (dst == allrouters_group) || 
	    (dst == allpimrouters_group)) {
	    setloop = 1;
	    k_set_loop(pim_socket, TRUE);  
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
    if (sendto(pim_socket, buf, sendlen, 0, 
	       (struct sockaddr *)&sdst, sizeof(sdst)) < 0) {
	if (errno == ENETDOWN)
	    check_vif_state();
	else
	    logit(LOG_WARNING, errno, "sendto from %s to %s",
		inet_fmt(src, s1), inet_fmt(dst, s2));
	if (setloop)
	    k_set_loop(pim_socket, FALSE); 
	return;
    }
    
    if (setloop)
	k_set_loop(pim_socket, FALSE); 
    
    IF_DEBUG(DEBUG_PIM_DETAIL) {
	IF_DEBUG(DEBUG_PIM) {
	    logit(LOG_DEBUG, 0, "SENT %s from %-15s to %s",
		packet_kind(IPPROTO_PIM, type, 0),
		src == INADDR_ANY_N ? "INADDR_ANY" :
		inet_fmt(src, s1), inet_fmt(dst, s2));
	}
    }
}

u_int pim_send_cnt = 0;
#define SEND_DEBUG_NUMBER 50


/* TODO: This can be merged with the above procedure */
/*
 * Send an unicast PIM packet from src to dst, PIM message type = "type"
 * and data length (after the PIM common header) = "datalen"
 */
void 
send_pim_unicast(buf, src, dst, type, datalen)
    char *buf;
    u_int32 src, dst;
    int type, datalen;
{
    struct sockaddr_in sdst;
    struct ip *ip;
    pim_header_t *pim;
    int sendlen;
#ifdef RAW_OUTPUT_IS_RAW
    extern int curttl;
#endif /* RAW_OUTPUT_IS_RAW */
    
    /* Prepare the IP header */
    ip                 = (struct ip *)buf;
    ip->ip_len         = IP_HEADER_RAOPT_LEN + sizeof(pim_header_t) + datalen;
    ip->ip_src.s_addr  = src;
    ip->ip_dst.s_addr  = dst;
    sendlen            = ip->ip_len;
    /* TODO: XXX: setup the TTL from the inner mcast packet? */
    ip->ip_ttl         = MAXTTL;
#ifdef RAW_OUTPUT_IS_RAW
    ip->ip_len         = htons(ip->ip_len);
#endif /* RAW_OUTPUT_IS_RAW */
    
    /* Prepare the PIM packet */
    pim		           = (pim_header_t *)(buf + IP_HEADER_RAOPT_LEN);
    pim->pim_vers           = PIM_PROTOCOL_VERSION;
    pim->pim_type          = type;
    pim->pim_cksum	   = 0;

    bzero((void *)&sdst, sizeof(sdst));
    sdst.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
    sdst.sin_len = sizeof(sdst);
#endif
    sdst.sin_addr.s_addr = dst;
    if (sendto(pim_socket, buf, sendlen, 0, 
	       (struct sockaddr *)&sdst, sizeof(sdst)) < 0) {
	if (errno == ENETDOWN)
	    check_vif_state();
	else
	    logit(LOG_WARNING, errno, "sendto from %s to %s",
		inet_fmt(src, s1), inet_fmt(dst, s2));
    }
    
    IF_DEBUG(DEBUG_PIM_DETAIL) {
	IF_DEBUG(DEBUG_PIM) {
/* TODO: use pim_send_cnt ?
	if (++pim_send_cnt > SEND_DEBUG_NUMBER) {
	    pim_send_cnt = 0;
	    logit(LOG_DEBUG, 0, "sending %s from %-15s to %s",
		packet_kind(IPPROTO_PIM, type, 0),
		inet_fmt(src, s1), inet_fmt(dst, s2));
	}
*/
	    logit(LOG_DEBUG, 0, "sending %s from %-15s to %s",
		packet_kind(IPPROTO_PIM, type, 0),
		inet_fmt(src, s1), inet_fmt(dst, s2));
	}
    }
}

/**
 * Local Variables:
 *  c-file-style: "cc-mode"
 * End:
 */
