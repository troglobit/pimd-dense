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
 *  $Id: trace.h,v 1.1.1.1 1998/05/11 17:39:34 kurtw Exp $
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


/*
 * The packet format for a traceroute request.
 */
struct tr_query {
    u_int32  tr_src;		/* traceroute source */
    u_int32  tr_dst;		/* traceroute destination */
    u_int32  tr_raddr;		/* traceroute response address */
#if defined(BYTE_ORDER) && (BYTE_ORDER == LITTLE_ENDIAN)
    struct {
	u_int	qid : 24;	/* traceroute query id */
	u_int	ttl : 8;	/* traceroute response ttl */
    } q;
#else
    struct {
	u_int   ttl : 8;	/* traceroute response ttl */
	u_int   qid : 24;	/* traceroute query id */
    } q;
#endif /* BYTE_ORDER */
};

#define tr_rttl q.ttl
#define tr_qid  q.qid

/*
 * Traceroute response format.  A traceroute response has a tr_query at the
 * beginning, followed by one tr_resp for each hop taken.
 */
struct tr_resp {
    u_int32 tr_qarr;		/* query arrival time */
    u_int32 tr_inaddr;		/* incoming interface address */
    u_int32 tr_outaddr;		/* outgoing interface address */
    u_int32 tr_rmtaddr;		/* parent address in source tree */
    u_int32 tr_vifin;		/* input packet count on interface */
    u_int32 tr_vifout;		/* output packet count on interface */
    u_int32 tr_pktcnt;		/* total incoming packets for src-grp */
    u_char  tr_rproto;		/* routing protocol deployed on router */
    u_char  tr_fttl;		/* ttl required to forward on outvif */
    u_char  tr_smask;		/* subnet mask for src addr */
    u_char  tr_rflags;		/* forwarding error codes */
};

/* defs within mtrace */
#define QUERY	1
#define RESP	2
#define QLEN	sizeof(struct tr_query)
#define RLEN	sizeof(struct tr_resp)

/* fields for tr_rflags (forwarding error codes) */
#define TR_NO_ERR	0       /* No error */
#define TR_WRONG_IF	1       /* traceroute arrived on non-oif */
#define TR_PRUNED	2       /* router has sent a prune upstream */
#define TR_OPRUNED	3       /* stop forw. after request from next hop rtr*/
#define TR_SCOPED	4       /* group adm. scoped at this hop */
#define TR_NO_RTE	5       /* no route for the source */
#define TR_NO_LHR       6       /* not the last-hop router */
#define TR_NO_FWD	7       /* not forwarding for this (S,G). Reason = ? */
#define TR_RP           8       /* I am the RP/Core */
#define TR_IIF          9       /* request arrived on the iif */
#define TR_NO_MULTI     0x0a    /* multicast disabled on that interface */
#define TR_NO_SPACE	0x81    /* no space to insert responce data block */
#define TR_OLD_ROUTER	0x82    /* previous hop does not support traceroute */
#define TR_ADMIN_PROHIB 0x83    /* traceroute adm. prohibited */

/* fields for tr_smask */
#define TR_GROUP_ONLY   0x2f    /* forwarding solely on group state */
#define TR_SUBNET_COUNT 0x40    /* pkt count for (S,G) is for source network */

/* fields for packets count */
#define TR_CANT_COUNT   0xffffffff  /* no count can be reported */

/* fields for tr_rproto (routing protocol) */
#define PROTO_DVMRP	   1
#define PROTO_MOSPF	   2
#define PROTO_PIM	   3
#define PROTO_CBT 	   4
#define PROTO_PIM_SPECIAL  5
#define PROTO_PIM_STATIC   6
#define PROTO_DVMRP_STATIC 7

#define MASK_TO_VAL(x, i) { \
			u_int32 _x = ntohl(x); \
			(i) = 1; \
			while ((_x) <<= 1) \
				(i)++; \
			};

#define VAL_TO_MASK(x, i) { \
			x = htonl(~((1 << (32 - (i))) - 1)); \
			};

#define NBR_VERS(n)	(((n)->al_pv << 8) + (n)->al_mv)
