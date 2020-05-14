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
 *  $Id: dvmrp_proto.c,v 1.1.1.1 1998/05/11 17:39:34 kurtw Exp $
 */

#include "defs.h"


/* TODO */
/*
 * Process an incoming neighbor probe message.
 */
void
dvmrp_accept_probe(src, dst, p, datalen, level)
    u_int32 src;
    u_int32 dst;
    char *p;
    int datalen;
    u_int32 level;
{
    return;
}


/* TODO */
/*
 * Process an incoming route report message.
 */
void
dvmrp_accept_report(src, dst, p, datalen, level)
    u_int32 src;
    u_int32 dst; 
    register char *p;
    register int datalen;
    u_int32 level;
{
    return;
}


/* TODO */
void
dvmrp_accept_info_request(src, dst, p, datalen)
    u_int32 src;
    u_int32 dst;
    u_char *p;
    int datalen;
{
    return;
}


/*
 * Process an incoming info reply message.
 */
void
dvmrp_accept_info_reply(src, dst, p, datalen)
    u_int32 src;
    u_int32 dst;
    u_char *p;
    int datalen;
{
    IF_DEBUG(DEBUG_PKT)
	log(LOG_DEBUG, 0, "ignoring spurious DVMRP info reply from %s to %s",
	    inet_fmt(src, s1), inet_fmt(dst, s2));
}


/*
 * Process an incoming neighbor-list message.
 */
void
dvmrp_accept_neighbors(src, dst, p, datalen, level)
    u_int32 src;
    u_int32 dst;
    u_char *p;
    int datalen;
    u_int32 level;
{
    log(LOG_INFO, 0, "ignoring spurious DVMRP neighbor list from %s to %s",
        inet_fmt(src, s1), inet_fmt(dst, s2));
}


/*
 * Process an incoming neighbor-list message.
 */
void
dvmrp_accept_neighbors2(src, dst, p, datalen, level)
    u_int32 src;
    u_int32 dst;
    u_char *p;
    int datalen;
    u_int32 level;
{
    IF_DEBUG(DEBUG_PKT)
	log(LOG_DEBUG, 0,
	    "ignoring spurious DVMRP neighbor list2 from %s to %s",
	    inet_fmt(src, s1), inet_fmt(dst, s2));
}


/* TODO */
/*
 * Takes the prune message received and then strips it to
 * determine the (src, grp) pair to be pruned.
 *
 * Adds the router to the (src, grp) entry then.
 *
 * Determines if further packets have to be sent down that vif
 *
 * Determines if a corresponding prune message has to be generated
 */
void
dvmrp_accept_prune(src, dst, p, datalen)
    u_int32 src;
    u_int32 dst;
    char *p;
    int datalen;
{
    return;
}


/* TODO */
/* determine the multicast group and src
 * 
 * if it does, then determine if a prune was sent 
 * upstream.
 * if prune sent upstream, send graft upstream and send
 * ack downstream.
 * 
 * if no prune sent upstream, change the forwarding bit
 * for this interface and send ack downstream.
 *
 * if no entry exists for this group send ack downstream.
 */
void
dvmrp_accept_graft(src, dst, p, datalen)
    u_int32     src;
    u_int32     dst;
    char        *p;
    int         datalen;
{
    return;
}


/* TODO */
/*
 * find out which group is involved first of all 
 * then determine if a graft was sent.
 * if no graft sent, ignore the message
 * if graft was sent and the ack is from the right 
 * source, remove the graft timer so that we don't 
 * have send a graft again
 */
void
dvmrp_accept_g_ack(src, dst, p, datalen)
    u_int32     src;
    u_int32     dst;
    char        *p;
    int         datalen;
{
    return;
}
