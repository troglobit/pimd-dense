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

#include "defs.h"
#include "queue.h"

/*
 * Local functions definitions.
 */
static int parse_pim_hello (char *pktPtr, ssize_t datalen, u_int32 src, u_int16 *holdtime);
static int compare_metrics (u_int32 local_preference, u_int32 local_metric, u_int32 local_address,
			    u_int32 remote_preference, u_int32 remote_metric, u_int32 remote_address);

vifbitmap_t nbr_vifs;    /* Vifs that have one or more neighbors attached */

/************************************************************************
 *                        PIM_HELLO
 ************************************************************************/
int
receive_pim_hello(src, dst, pim_message, datalen)
    u_int32 src, dst;
    char *pim_message;
    ssize_t datalen;
{
    vifi_t vifi;
    struct uvif *v;
    pim_nbr_entry_t *nbr, *prev_nbr, *new_nbr;
    u_int16 holdtime = PIM_TIMER_HELLO_HOLDTIME;
    u_int8  *data_ptr;
    int state_change;
    srcentry_t *srcentry_ptr;
    srcentry_t *srcentry_ptr_next;
    mrtentry_t *mrtentry_ptr;

    /* Checksum */
    if (inet_cksum((u_int16 *)pim_message, datalen))
	return FALSE;

    if ((vifi = find_vif_direct(src)) == NO_VIF) {
	/* Either a local vif or somehow received PIM_HELLO from
	 * non-directly connected router. Ignore it.
	 */
	if (local_address(src) == NO_VIF)
	    logit(LOG_INFO, 0, "Ignoring PIM_HELLO from non-neighbor router %s",
		inet_fmt(src, s1));
	return FALSE;
    }

    v = &uvifs[vifi];
    if (v->uv_flags & (VIFF_DOWN | VIFF_DISABLED))
	return FALSE;    /* Shoudn't come on this interface */
    data_ptr = (u_int8 *)(pim_message + sizeof(pim_header_t));

    /* Get the Holdtime (in seconds) from the message. Return if error. */
    if (parse_pim_hello(pim_message, datalen, src, &holdtime) == FALSE)
	return FALSE;

    IF_DEBUG(DEBUG_PIM_HELLO | DEBUG_PIM_TIMER)
	logit(LOG_DEBUG, 0, "PIM HELLO holdtime from %s is %u",
	    inet_fmt(src, s1), holdtime);
    
    for (prev_nbr = NULL, nbr = v->uv_pim_neighbors; nbr; prev_nbr = nbr, nbr = nbr->next) {
	/* The PIM neighbors are sorted in decreasing order of the
	 * network addresses (note that to be able to compare them
	 * correctly we must translate the addresses in host order.
	 */
	if (ntohl(src) < ntohl(nbr->address))
	    continue;

	if (src == nbr->address) {
	    /* We already have an entry for this host */
	    if (0 == holdtime) {
		/* Looks like we have a nice neighbor who is going down
		 * and wants to inform us by sending "holdtime=0". Thanks
		 * buddy and see you again!
		 */
		logit(LOG_INFO, 0, "PIM HELLO received: neighbor %s going down",
		    inet_fmt(src, s1));
		delete_pim_nbr(nbr);
		return TRUE;
	    }

	    SET_TIMER(nbr->timer, holdtime);
	    return TRUE;
	}

	/*
	 * No entry for this neighbor. Exit the loop and create an
	 * entry for it.
	 */
	break;
    }
    
    /*
     * This is a new neighbor. Create a new entry for it.
     * It must be added right after `prev_nbr`
     */
    new_nbr = calloc(1, sizeof(pim_nbr_entry_t));
    if (!new_nbr)
	    logit(LOG_ERR, 0, "%s(): out of memory", __func__);

    new_nbr->address          = src;
    new_nbr->vifi             = vifi;
    SET_TIMER(new_nbr->timer, holdtime);
    new_nbr->next             = nbr;
    new_nbr->prev             = prev_nbr;
    
    if (prev_nbr)
	prev_nbr->next  = new_nbr;
    else
	v->uv_pim_neighbors = new_nbr;
    if (new_nbr->next)
	new_nbr->next->prev = new_nbr;

    v->uv_flags &= ~VIFF_NONBRS;
    v->uv_flags |= VIFF_PIM_NBR;
    VIFM_SET(vifi, nbr_vifs);

    /* Elect a new DR */
    if (ntohl(v->uv_lcl_addr) < ntohl(v->uv_pim_neighbors->address)) {
	/* The first address is the new potential remote
	 * DR address and it wins (is >) over the local address.
	 */
	v->uv_flags &= ~VIFF_DR;
    }

    /* Since a new neighbour has come up, let it know your existence */
    /* XXX: TODO: not in the spec,
     * but probably should send the message after a short random period?
     */
    send_pim_hello(v, PIM_TIMER_HELLO_HOLDTIME);
    
    /* Update the source entries */
    for (srcentry_ptr = srclist; srcentry_ptr; srcentry_ptr = srcentry_ptr_next) {
	srcentry_ptr_next = srcentry_ptr->next;
	
	if (srcentry_ptr->incoming == vifi)
	    continue;
	
	for (mrtentry_ptr = srcentry_ptr->mrtlink; mrtentry_ptr; mrtentry_ptr = mrtentry_ptr->srcnext) {
	    if (!(VIFM_ISSET(vifi, mrtentry_ptr->oifs))) {
		state_change = change_interfaces(mrtentry_ptr, srcentry_ptr->incoming,
						 mrtentry_ptr->pruned_oifs,
						 mrtentry_ptr->leaves);
		if (state_change == 1)
		    trigger_join_alert(mrtentry_ptr);
	    }
	}
    }
    
    return TRUE;
}


void
delete_pim_nbr(nbr_delete)
    pim_nbr_entry_t *nbr_delete;
{
    srcentry_t *srcentry_ptr;
    srcentry_t *srcentry_ptr_next;
    mrtentry_t *mrtentry_ptr, *mrtentry_next;
    grpentry_t *grpentry_ptr, *grpentry_next;
    struct uvif *v;
    int state_change;

    v = &uvifs[nbr_delete->vifi];

    /* Delete the entry from the pim_nbrs chain */
    if (nbr_delete->prev)
	nbr_delete->prev->next = nbr_delete->next;
    else
	v->uv_pim_neighbors = nbr_delete->next;
    if (nbr_delete->next)
	nbr_delete->next->prev = nbr_delete->prev;
    
    if (!v->uv_pim_neighbors) {
	/* This was our last neighbor. */
	v->uv_flags &= ~VIFF_PIM_NBR;
	v->uv_flags |= (VIFF_NONBRS | VIFF_DR);
	VIFM_CLR(nbr_delete->vifi, nbr_vifs);
    }
    else {
	if (ntohl(v->uv_lcl_addr) > ntohl(v->uv_pim_neighbors->address)) {
	    /*
	     * The first address is the new potential remote
	     * DR address, but the local address is the winner.
	     */
	    v->uv_flags |= VIFF_DR;
	}
    }

    /* Delete neighbor from all asserted_oifs and trigger assert reelection */
    for (grpentry_ptr = grplist; grpentry_ptr; grpentry_ptr = grpentry_next) {
	grpentry_next = grpentry_ptr->next;

	for (mrtentry_ptr = grpentry_ptr->mrtlink; mrtentry_ptr; mrtentry_ptr = mrtentry_next) {
	    mrtentry_next = mrtentry_ptr->grpnext;

	    if (mrtentry_ptr->flags & MRTF_ASSERTED) {
		if (VIFM_ISSET(nbr_delete->vifi, mrtentry_ptr->asserted_oifs)) {
		    VIFM_CLR(nbr_delete->vifi, mrtentry_ptr->asserted_oifs);

		    state_change = change_interfaces(mrtentry_ptr, mrtentry_ptr->incoming,
						     mrtentry_ptr->pruned_oifs,
						     mrtentry_ptr->leaves);
		    if (state_change == -1)
			trigger_prune_alert(mrtentry_ptr);
		    else if (state_change == 1)
			trigger_join_alert(mrtentry_ptr);
		}
	    }
	}
    }

    /* Update the source entries:
     * If the deleted nbr was my upstream, then reset incoming and
     * update all (S,G) entries for sources reachable through it.
     * If the deleted nbr was the last on a non-iif vif, then recalcuate
     * outgoing interfaces.
     */
    for (srcentry_ptr = srclist; srcentry_ptr; srcentry_ptr = srcentry_ptr_next) {
	srcentry_ptr_next = srcentry_ptr->next;
	
	/* The only time we don't need to scan all mrtentries is when the nbr
	 * was on the iif, but not the upstream nbr! 
	 */
	if (nbr_delete->vifi == srcentry_ptr->incoming &&
	    srcentry_ptr->upstream != nbr_delete)
	    continue;
	
	/* Reset the next hop (PIM) router */
	if (srcentry_ptr->upstream == nbr_delete) 
	    if (set_incoming(srcentry_ptr, PIM_IIF_SOURCE) == FALSE) {
		/* Coudn't reset it. Sorry, the hext hop router toward that
		 * source is probably not a PIM router, or cannot find route
		 * at all, hence I cannot handle this source and have to
		 * delete it.
		 */
		delete_srcentry(srcentry_ptr);
		free(nbr_delete);
		return;
	    }

	for (mrtentry_ptr = srcentry_ptr->mrtlink; mrtentry_ptr; mrtentry_ptr = mrtentry_ptr->srcnext) {
	    mrtentry_ptr->incoming   = srcentry_ptr->incoming;
	    mrtentry_ptr->upstream   = srcentry_ptr->upstream;
	    mrtentry_ptr->metric     = srcentry_ptr->metric;
	    mrtentry_ptr->preference = srcentry_ptr->preference;
	    state_change = change_interfaces(mrtentry_ptr, srcentry_ptr->incoming,
					     mrtentry_ptr->pruned_oifs,
					     mrtentry_ptr->leaves);
	    if (state_change == -1)
		trigger_prune_alert(mrtentry_ptr);
	    else if (state_change == 1)
		trigger_join_alert(mrtentry_ptr);
	}
    }
    
    free(nbr_delete);
}


/* TODO: simplify it! */
static int
parse_pim_hello(pim_message, datalen, src, holdtime)
    char *pim_message;
    ssize_t datalen;
    u_int32 src;
    u_int16 *holdtime;
{
    u_int8 *pim_hello_message;
    u_int8 *data_ptr;
    u_int16 option_type;
    u_int16 option_length;
    int holdtime_received_ok = FALSE;
    int option_total_length;

    pim_hello_message = (u_int8 *)(pim_message + sizeof(pim_header_t));
    datalen -= sizeof(pim_header_t);
    for ( ; datalen >= (ssize_t)sizeof(pim_hello_t); ) {
	/* Ignore any data if shorter than (pim_hello header) */
	data_ptr = pim_hello_message;
	GET_HOSTSHORT(option_type, data_ptr);
	GET_HOSTSHORT(option_length, data_ptr);

	switch (option_type) {
	case PIM_HELLO_HOLDTIME:
	    if (PIM_HELLO_HOLDTIME_LENGTH != option_length) {
		IF_DEBUG(DEBUG_PIM_HELLO)
		    logit(LOG_DEBUG, 0,
		       "PIM HELLO Holdtime from %s: invalid OptionLength = %u",
			inet_fmt(src, s1), option_length);
		return FALSE;
	    }
	    GET_HOSTSHORT(*holdtime, data_ptr);
	    holdtime_received_ok = TRUE;
	    break;

	default:
	    /* Ignore any unknown options */
	    break;
	}

	/* Move to the next option */
        /* XXX: TODO: If we are padding to the end of the 32 bit boundary,
         * use the first method to move to the next option, otherwise
         * simply (sizeof(pim_hello_t) + option_length).
         */
#ifdef BOUNDARY_32_BIT
	option_total_length = (sizeof(pim_hello_t) + (option_length & ~0x3) +
			       ((option_length & 0x3) ? 4 : 0));
#else
	option_total_length = (sizeof(pim_hello_t) + option_length);
#endif /* BOUNDARY_32_BIT */
	datalen -= option_total_length;
	pim_hello_message += option_total_length;
    }

    return holdtime_received_ok;
}


int
send_pim_hello(v, holdtime)
    struct uvif *v;
    u_int16 holdtime;
{
    char   *buf;
    u_int8 *data_ptr;
    ssize_t datalen;

    buf = pim_send_buf + IP_HEADER_RAOPT_LEN + sizeof(pim_header_t);
    data_ptr = (u_int8 *)buf;
    PUT_HOSTSHORT(PIM_HELLO_HOLDTIME, data_ptr);
    PUT_HOSTSHORT(PIM_HELLO_HOLDTIME_LENGTH, data_ptr);
    PUT_HOSTSHORT(holdtime, data_ptr);

    PUT_HOSTSHORT(PIM_HELLO_GENID, data_ptr);
    PUT_HOSTSHORT(PIM_HELLO_GENID_LEN, data_ptr);
    PUT_HOSTLONG(v->uv_genid, data_ptr);

    datalen = data_ptr - (u_int8 *)buf;

    send_pim(pim_send_buf, v->uv_lcl_addr, allpimrouters_group, PIM_HELLO,
	     datalen);
    SET_TIMER(v->uv_pim_hello_timer, PIM_TIMER_HELLO_PERIOD);

    return TRUE;
}


/************************************************************************
 *                        PIM_JOIN_PRUNE
 ************************************************************************/

typedef struct {
    u_int32 source;
    u_int32 group;
    u_int32 target;
} join_delay_cbk_t;

typedef struct {
    vifi_t vifi;
    u_int32 source;
    u_int32 group;
    u_int16 holdtime;
} prune_delay_cbk_t;
    
static void 
delayed_join_job(arg)
     void *arg;
{
    join_delay_cbk_t *cbk = (join_delay_cbk_t *)arg;
    mrtentry_t *mrtentry_ptr;

    mrtentry_ptr = find_route(cbk->source, cbk->group, MRTF_SG, DONT_CREATE);
    if (!mrtentry_ptr)
	return;
    
    if (mrtentry_ptr->join_delay_timerid)
	timer_clearTimer(mrtentry_ptr->join_delay_timerid);

    if (mrtentry_ptr->upstream)
	send_pim_jp(mrtentry_ptr, PIM_ACTION_JOIN, mrtentry_ptr->incoming,
		    mrtentry_ptr->upstream->address, 0);

    free(cbk);
}

static void 
schedule_delayed_join(mrtentry_ptr, target) 
     mrtentry_t *mrtentry_ptr;
     u_int32 target;
{
    u_long random_delay;
    join_delay_cbk_t *cbk;
    
    /* Delete existing timer */
    if (mrtentry_ptr->join_delay_timerid) 
	timer_clearTimer(mrtentry_ptr->join_delay_timerid);

    random_delay = lrand48() % (long)PIM_RANDOM_DELAY_JOIN_TIMEOUT;
    
    IF_DEBUG(DEBUG_PIM_JOIN_PRUNE)
	logit(LOG_DEBUG, 0, "Scheduling join for src %s, grp %s, delay %lu",
	    inet_fmt(mrtentry_ptr->source->address, s1), 
	    inet_fmt(mrtentry_ptr->group->group, s2),
	    random_delay);

    if (random_delay == 0 && mrtentry_ptr->upstream) {
	send_pim_jp(mrtentry_ptr, PIM_ACTION_JOIN, mrtentry_ptr->incoming,
		    mrtentry_ptr->upstream->address, 0);
	return;
    }

    cbk = calloc(1, sizeof(join_delay_cbk_t));
    if (!cbk)
	    logit(LOG_ERR, 0, "%s(): out of memory", __func__);

    cbk->source = mrtentry_ptr->source->address;
    cbk->group = mrtentry_ptr->group->group;
    cbk->target = target;

    mrtentry_ptr->join_delay_timerid = timer_setTimer(random_delay, delayed_join_job, cbk);
}


static void 
delayed_prune_job(arg)
     void *arg;
{
    prune_delay_cbk_t *cbk = (prune_delay_cbk_t *)arg;
    mrtentry_t *mrtentry_ptr;
    vifbitmap_t new_pruned_oifs;
    int state_change;

    mrtentry_ptr = find_route(cbk->source, cbk->group, MRTF_SG, DONT_CREATE);
    if (!mrtentry_ptr)
	return;
    
    if (mrtentry_ptr->prune_delay_timerids[cbk->vifi])
	timer_clearTimer(mrtentry_ptr->prune_delay_timerids[cbk->vifi]);

    if (VIFM_ISSET(cbk->vifi, mrtentry_ptr->oifs)) {
	IF_DEBUG(DEBUG_PIM_JOIN_PRUNE)
	    logit(LOG_DEBUG, 0, "Deleting pruned vif %d for src %s, grp %s",
		cbk->vifi, 
		inet_fmt(cbk->source, s1), 
		inet_fmt(cbk->group, s2));

	VIFM_COPY(mrtentry_ptr->pruned_oifs, new_pruned_oifs);
	VIFM_SET(cbk->vifi, new_pruned_oifs);
	SET_TIMER(mrtentry_ptr->prune_timers[cbk->vifi], cbk->holdtime);

	state_change = change_interfaces(mrtentry_ptr,
					 mrtentry_ptr->incoming,
					 new_pruned_oifs,
					 mrtentry_ptr->leaves);

	/* Handle transition to negative cache */
	if (state_change == -1)
	    trigger_prune_alert(mrtentry_ptr);
    }

    free(cbk);
}

static void 
schedule_delayed_prune(mrtentry_ptr, vifi, holdtime) 
     mrtentry_t *mrtentry_ptr;
     vifi_t vifi;
     u_int16 holdtime;
{
    prune_delay_cbk_t *cbk;
    
    /* Delete existing timer */
    if (mrtentry_ptr->prune_delay_timerids[vifi]) 
	timer_clearTimer(mrtentry_ptr->prune_delay_timerids[vifi]);

    cbk = calloc(1, sizeof(prune_delay_cbk_t));
    if (!cbk)
	    logit(LOG_ERR, 0, "%s(): out of memory", __func__);

    cbk->vifi = vifi;
    cbk->source = mrtentry_ptr->source->address;
    cbk->group = mrtentry_ptr->group->group;
    cbk->holdtime = holdtime;

    mrtentry_ptr->prune_delay_timerids[vifi] =
	timer_setTimer((u_int16)PIM_RANDOM_DELAY_JOIN_TIMEOUT+1,
		       delayed_prune_job, cbk);
}


/* TODO: when parsing, check if we go beyong message size */
int
receive_pim_join_prune(src, dst, pim_message, datalen)
    u_int32 src, dst;
    char *pim_message;
    int datalen;
{
    vifi_t vifi;
    struct uvif *v;
    pim_encod_uni_addr_t uni_target_addr;
    pim_encod_grp_addr_t encod_group;
    pim_encod_src_addr_t encod_src;
    u_int8 *data_ptr;
    u_int8 num_groups;
    u_int16 holdtime;
    u_int16 num_j_srcs;
    u_int16 num_p_srcs;
    u_int32 source;
    u_int32 group;
    u_int32 s_mask;
    u_int32 g_mask;
    u_int8 s_flags;
    u_int8 reserved;
    mrtentry_t *mrtentry_ptr;
    pim_nbr_entry_t *upstream_router;
    vifbitmap_t new_pruned_oifs;
    int state_change;

    if ((vifi = find_vif_direct(src)) == NO_VIF) {
        /* Either a local vif or somehow received PIM_JOIN_PRUNE from
         * non-directly connected router. Ignore it.
         */
        if (local_address(src) == NO_VIF)
            logit(LOG_INFO, 0,
                "Ignoring PIM_JOIN_PRUNE from non-neighbor router %s",
                inet_fmt(src, s1));
        return FALSE;
    }

    /* Checksum */
    if (inet_cksum((u_int16 *)pim_message, datalen))
        return FALSE;
 
    v = &uvifs[vifi];
    if (uvifs[vifi].uv_flags & (VIFF_DOWN | VIFF_DISABLED | VIFF_NONBRS))
        return FALSE;    /* Shoudn't come on this interface */

    data_ptr = (u_int8 *)(pim_message + sizeof(pim_header_t));

    /* Get the target address */
    GET_EUADDR(&uni_target_addr, data_ptr);
    GET_BYTE(reserved, data_ptr);
    GET_BYTE(num_groups, data_ptr);
    if (num_groups == 0)
        return FALSE;    /* No indication for groups in the message */

    GET_HOSTSHORT(holdtime, data_ptr);

    IF_DEBUG(DEBUG_PIM_JOIN_PRUNE)
	logit(LOG_DEBUG, 0,
	    "PIM Join/Prune received from %s : target %s, holdtime %d",
	    inet_fmt(src, s1), inet_fmt(uni_target_addr.unicast_addr, s2), 
	    holdtime);
    
    if ((uni_target_addr.unicast_addr != v->uv_lcl_addr) && 
	(uni_target_addr.unicast_addr != INADDR_ANY_N))  {
        /* if I am not the target of the join or prune message */
        /* Join Suppression: when receiving a join not addressed to me,
	 * if I am delaying a join for this (S,G) then cancel the delayed 
	 * join.
	 * Prune Soliticiting Joins: when receiving a prune not addressed to
	 * me on a LAN, schedule delayed join if I have downstream receivers.
         */

        upstream_router = find_pim_nbr(uni_target_addr.unicast_addr);
        if (upstream_router == (pim_nbr_entry_t *)NULL)
            return FALSE;   /* I have no such neighbor */

        while (num_groups--) {
            GET_EGADDR(&encod_group, data_ptr);
            GET_HOSTSHORT(num_j_srcs, data_ptr);
            GET_HOSTSHORT(num_p_srcs, data_ptr);
            MASKLEN_TO_MASK(encod_group.masklen, g_mask);
            group = encod_group.mcast_addr;
            if (!IN_MULTICAST(ntohl(group))) {
                data_ptr +=
                    (num_j_srcs + num_p_srcs) * sizeof(pim_encod_src_addr_t);
                continue; /* Ignore this group and jump to the next */
            }

            while (num_j_srcs--) {
                GET_ESADDR(&encod_src, data_ptr);
                source = encod_src.src_addr;
                if (!inet_valid_host(source))
                    continue;

                s_flags = encod_src.flags;
                MASKLEN_TO_MASK(encod_src.masklen, s_mask);

                /* (S,G) Join suppresion */
                mrtentry_ptr = find_route(source, group, MRTF_SG, DONT_CREATE);
		if (!mrtentry_ptr)
		    continue;

		IF_DEBUG(DEBUG_PIM_JOIN_PRUNE)
		    logit(LOG_DEBUG, 0,
			"\tJOIN src %s, group %s - canceling delayed join",
			inet_fmt(source, s1), inet_fmt(group, s2));
		
		/* Cancel the delayed join */
		if (mrtentry_ptr->join_delay_timerid) {
		    timer_clearTimer(mrtentry_ptr->join_delay_timerid);
		    mrtentry_ptr->join_delay_timerid = 0;
		}
	    } 

            while (num_p_srcs--) {
                GET_ESADDR(&encod_src, data_ptr);
                source = encod_src.src_addr;
                if (!inet_valid_host(source))
                    continue;

                s_flags = encod_src.flags;

		/* if P2P link (not addressed to me) ignore 
		 */
		if (uvifs[vifi].uv_flags & VIFF_POINT_TO_POINT) 
		    continue;

		/* if non-null oiflist then schedule delayed join
		 */
                mrtentry_ptr = find_route(source, group, MRTF_SG, DONT_CREATE);
		if (!mrtentry_ptr)
		    continue;

		if (!(VIFM_ISEMPTY(mrtentry_ptr->oifs))) {
		    IF_DEBUG(DEBUG_PIM_JOIN_PRUNE)
			logit(LOG_DEBUG, 0,
			    "\tPRUNE src %s, group %s - scheduling delayed join",
			    inet_fmt(source, s1), inet_fmt(group, s2));
		    
		    schedule_delayed_join(mrtentry_ptr, uni_target_addr.unicast_addr);
		}
	    }

	} /* while groups */

	return TRUE;
    } /* if not unicast target */

    /* I am the target of this join/prune:
     * For joins, cancel delayed prunes that I have scheduled.
     * For prunes, echo the prune and schedule delayed prunes on LAN or 
     * prune immediately on point-to-point links.
     */
    else {
	while (num_groups--) {
	    GET_EGADDR(&encod_group, data_ptr);
	    GET_HOSTSHORT(num_j_srcs, data_ptr);
	    GET_HOSTSHORT(num_p_srcs, data_ptr);
	    MASKLEN_TO_MASK(encod_group.masklen, g_mask);
	    group = encod_group.mcast_addr;
	    if (!IN_MULTICAST(ntohl(group))) {
		data_ptr +=
		    (num_j_srcs + num_p_srcs) * sizeof(pim_encod_src_addr_t);
		continue; /* Ignore this group and jump to the next */
	    }
	    
	    while (num_j_srcs--) {
		GET_ESADDR(&encod_src, data_ptr);
		source = encod_src.src_addr;
		if (!inet_valid_host(source))
		    continue;

		s_flags = encod_src.flags;
		MASKLEN_TO_MASK(encod_src.masklen, s_mask);
	    
                mrtentry_ptr = find_route(source, group, MRTF_SG, DONT_CREATE);
		if (!mrtentry_ptr)
		    continue;

		IF_DEBUG(DEBUG_PIM_JOIN_PRUNE)
		    logit(LOG_DEBUG, 0,
			"\tJOIN src %s, group %s - canceling delayed prune",
			inet_fmt(source, s1), inet_fmt(group, s2));
		
		/* Cancel the delayed prune */
		if (mrtentry_ptr->prune_delay_timerids[vifi]) {
		    timer_clearTimer(mrtentry_ptr->prune_delay_timerids[vifi]);
		    mrtentry_ptr->prune_delay_timerids[vifi] = 0;
		}	    
	    }

            while (num_p_srcs--) {
                GET_ESADDR(&encod_src, data_ptr);
                source = encod_src.src_addr;
                if (!inet_valid_host(source))
                    continue;

                s_flags = encod_src.flags;

                mrtentry_ptr = find_route(source, group, MRTF_SG, DONT_CREATE);
		if (!mrtentry_ptr)
		    continue;

		/* if P2P link (addressed to me) prune immediately
		 */
		if (uvifs[vifi].uv_flags & VIFF_POINT_TO_POINT) {
		    if (VIFM_ISSET(vifi, mrtentry_ptr->pruned_oifs)) {

			IF_DEBUG(DEBUG_PIM_JOIN_PRUNE)
			    logit(LOG_DEBUG, 0,
				"\tPRUNE(P2P) src %s, group %s - pruning vif",
				inet_fmt(source, s1), inet_fmt(group, s2));
		
			IF_DEBUG(DEBUG_MRT)
			    logit(LOG_DEBUG, 0, "Deleting pruned vif %d for src %s, grp %s",
				vifi, 
				inet_fmt(source, s1), inet_fmt(group, s2));

			VIFM_COPY(mrtentry_ptr->pruned_oifs, new_pruned_oifs);
			VIFM_SET(vifi, new_pruned_oifs);
			SET_TIMER(mrtentry_ptr->prune_timers[vifi], holdtime);

			state_change = change_interfaces(mrtentry_ptr,
							 mrtentry_ptr->incoming,
							 new_pruned_oifs,
							 mrtentry_ptr->leaves);

			/* Handle transition to negative cache */
			if (state_change == -1)
			    trigger_prune_alert(mrtentry_ptr);
		    } /* if is pruned */
		} /* if p2p */

		/* if LAN link, echo the prune and schedule delayed
		 * oif deletion
		 */
		else {
		    IF_DEBUG(DEBUG_PIM_JOIN_PRUNE)
			logit(LOG_DEBUG, 0,
			    "\tPRUNE(LAN) src %s, group %s - scheduling delayed prune",
			    inet_fmt(source, s1), inet_fmt(group, s2));
		    
		    send_pim_jp(mrtentry_ptr, PIM_ACTION_PRUNE, vifi,
				uni_target_addr.unicast_addr, holdtime);
		    schedule_delayed_prune(mrtentry_ptr, vifi, holdtime);
		}
	    }
	} /* while groups */
    } /* else I am unicast target */

    return TRUE;
}


int
send_pim_jp(mrtentry_ptr, action, vifi, target_addr, holdtime)
     mrtentry_t *mrtentry_ptr;
     int action;           /* PIM_ACTION_JOIN or PIM_ACTION_PRUNE */
     vifi_t vifi;          /* vif to send join/prune on */
     u_int32 target_addr;  /* encoded unicast target neighbor */
     u_int16 holdtime;     /* holdtime */
{
    u_int8 *data_ptr, *data_start_ptr;
	
    if (!mrtentry_ptr->upstream)
    	/* No upstream neighbor - don't send */
    	return FALSE;
    
    data_ptr = (u_int8 *)(pim_send_buf + IP_HEADER_RAOPT_LEN + sizeof(pim_header_t));
    data_start_ptr = data_ptr;

    IF_DEBUG(DEBUG_PIM_JOIN_PRUNE)
	logit(LOG_DEBUG, 0,
	    "Sending %s:  vif %s, src %s, group %s, target %s, holdtime %d",
	    action==PIM_ACTION_JOIN ? "JOIN" : "PRUNE",
	    inet_fmt(uvifs[vifi].uv_lcl_addr, s1),
	    inet_fmt(mrtentry_ptr->source->address, s2), 
	    inet_fmt(mrtentry_ptr->group->group, s3),
	    inet_fmt(target_addr, s4), holdtime);

    PUT_EUADDR(target_addr, data_ptr);   /* encoded unicast target addr */
    PUT_BYTE(0, data_ptr);		     /* Reserved */
    *data_ptr++ = (u_int8)1;             /* number of groups */
    PUT_HOSTSHORT(holdtime, data_ptr);   /* holdtime */
    
    /* data_ptr points at the first, and only encoded mcast group */
    PUT_EGADDR(mrtentry_ptr->group->group, SINGLE_GRP_MSKLEN, 0, data_ptr);
    
    /* set the number of join and prune sources */
    if (action == PIM_ACTION_JOIN) {
	PUT_HOSTSHORT(1, data_ptr);
	PUT_HOSTSHORT(0, data_ptr);
    } else if (action == PIM_ACTION_PRUNE) {
	PUT_HOSTSHORT(0, data_ptr);
	PUT_HOSTSHORT(1, data_ptr);
    }	
    
    PUT_ESADDR(mrtentry_ptr->source->address, SINGLE_SRC_MSKLEN,
               0, data_ptr);
    
    /* Cancel active graft */
    delete_pim_graft_entry(mrtentry_ptr);
    
    send_pim(pim_send_buf, uvifs[vifi].uv_lcl_addr, allpimrouters_group,
             PIM_JOIN_PRUNE, data_ptr - data_start_ptr);
    
    return TRUE;
}


/************************************************************************
 *                        PIM_ASSERT
 ************************************************************************/

/* Notes on assert prefs/metrics
 *   - For downstream routers, compare pref/metric previously received from
 *     winner against those in message.
 *     ==> store assert winner's pref/metric in mrtentry
 *   - For upstream router compare my actualy pref/metric for the source
 *     against those received in message.
 *     ==> store my actual pref/metric in srcentry
 */

int
receive_pim_assert(src, dst, pim_message, datalen)
    u_int32 src, dst;
    char *pim_message;
    int datalen;
{
    vifi_t vifi;
    pim_encod_uni_addr_t eusaddr;
    pim_encod_grp_addr_t egaddr;
    u_int32 source, group;
    mrtentry_t *mrtentry_ptr;
    u_int8 *data_ptr;
    struct uvif *v;
    u_int32 assert_preference;
    u_int32 assert_metric;
    u_int32 local_metric;
    u_int32 local_preference;
    u_int8  local_wins;
    vifbitmap_t new_pruned_oifs;
    int state_change;
    
    if ((vifi = find_vif_direct(src)) == NO_VIF) {
        /* Either a local vif or somehow received PIM_ASSERT from
         * non-directly connected router. Ignore it.
         */
        if (local_address(src) == NO_VIF)
            logit(LOG_INFO, 0, "Ignoring PIM_ASSERT from non-neighbor router %s",
                inet_fmt(src, s1));
        return FALSE;
    }
    
    /* Checksum */
    if (inet_cksum((u_int16 *)pim_message, datalen))
        return FALSE;
    
    v = &uvifs[vifi];
    if (uvifs[vifi].uv_flags & (VIFF_DOWN | VIFF_DISABLED | VIFF_NONBRS))
        return FALSE;    /* Shoudn't come on this interface */
    data_ptr = (u_int8 *)(pim_message + sizeof(pim_header_t));

    /* Get the group and source addresses */
    GET_EGADDR(&egaddr, data_ptr);
    GET_EUADDR(&eusaddr, data_ptr);
    
    /* Get the metric related info */
    GET_HOSTLONG(assert_preference, data_ptr);
    GET_HOSTLONG(assert_metric, data_ptr);

    source = eusaddr.unicast_addr;
    group = egaddr.mcast_addr;
 
    IF_DEBUG(DEBUG_PIM_ASSERT)
	logit(LOG_DEBUG, 0, "PIM Assert received from %s: src %s, grp %s, pref %d, metric %d",
	      inet_fmt(src, s1), inet_fmt(source, s2), inet_fmt(group, s3),
	      assert_preference, assert_metric);
 
    mrtentry_ptr = find_route(source, group, MRTF_SG, CREATE);
    if (!mrtentry_ptr)
	return FALSE;

    if (mrtentry_ptr->flags & MRTF_NEW) {
	/* For some reason, it's possible for asserts to be processed
	 * before the data alerts a cache miss.  Therefore, when an
	 * assert is received, create (S,G) state and continue, since
	 * we know by the assert that there are upstream forwarders. 
	 */
	IF_DEBUG(DEBUG_PIM_ASSERT)
	    logit(LOG_DEBUG, 0, "\tNo MRT entry - creating...");

	mrtentry_ptr->flags &= ~MRTF_NEW;
	
	/* Set oifs */	
	set_leaves(mrtentry_ptr);
	calc_oifs(mrtentry_ptr, &(mrtentry_ptr->oifs));
	
	/* Add it to the kernel */
	k_chg_mfc(igmp_socket, source, group, mrtentry_ptr->incoming,
		  mrtentry_ptr->oifs);
	
#ifdef RSRR
	rsrr_cache_send(mrtentry_ptr, RSRR_NOTIFICATION_OK);
#endif /* RSRR */
	
	/* No need to call change_interfaces, but check for NULL oiflist */
	if (VIFM_ISEMPTY(mrtentry_ptr->oifs))
	    trigger_prune_alert(mrtentry_ptr);	
    }

    /* If arrived on iif, I'm downstream of the asserted LAN.
     * If arrived on oif, I'm upstream of the asserted LAN.
     */
    if (vifi == mrtentry_ptr->incoming) {
	/* assert arrived on iif ==> I'm a downstream router */

	/* Ignore assert message if we do not have an upstream router */
	if (mrtentry_ptr->upstream == NULL)
	    return FALSE;

	/* Determine local (really that of upstream nbr!) pref/metric */
	local_metric = mrtentry_ptr->metric;
	local_preference = mrtentry_ptr->preference;
 
	if (mrtentry_ptr->upstream->address == src &&
	    assert_preference == local_preference &&
	    assert_metric == local_metric)
	   
	   /* if assert from previous winner w/ same pref/metric, 
	    * then assert sender wins again */
	    local_wins = FALSE;
	else
	    /* assert from someone else or something changed */
	    local_wins = compare_metrics(local_preference, local_metric,
					 mrtentry_ptr->upstream->address,
					 assert_preference, assert_metric, 
					 src);
	
	/* This is between the assert sender and previous winner or rpf 
	 * (who is the "local" in this case).
	 */
	if (local_wins == TRUE) {
	    /* the assert-sender loses, so discard the assert */
	    IF_DEBUG(DEBUG_PIM_ASSERT)
		logit(LOG_DEBUG, 0,
		    "\tAssert sender %s loses", inet_fmt(src, s1));
	    return TRUE;
	}
	
	/* The assert sender wins: upstream must be changed to the winner */
	IF_DEBUG(DEBUG_PIM_ASSERT)
	    logit(LOG_DEBUG, 0,
		"\tAssert sender %s wins", inet_fmt(src, s1));
	if (mrtentry_ptr->upstream->address != src) {
	    IF_DEBUG(DEBUG_PIM_ASSERT)
		logit(LOG_DEBUG, 0,
		    "\tChanging upstream nbr to %s", inet_fmt(src, s1));
	    mrtentry_ptr->preference = assert_preference;
	    mrtentry_ptr->metric = assert_metric;
	    mrtentry_ptr->upstream = find_pim_nbr(src);
	}
	SET_TIMER(mrtentry_ptr->assert_timer, assert_timeout);
	mrtentry_ptr->flags |= MRTF_ASSERTED;

	/* Send a join for the S,G if oiflist is non-empty */
	if (!(VIFM_ISEMPTY(mrtentry_ptr->oifs))) 
	    send_pim_jp(mrtentry_ptr, PIM_ACTION_JOIN, mrtentry_ptr->incoming,
			src, 0);
	
    } /* if assert on iif */

    /* If the assert arrived on an oif: */
    else {
	if (!(VIFM_ISSET(vifi, mrtentry_ptr->oifs))) 
	    return FALSE;
	/* assert arrived on oif ==> I'm a upstream router */

	/* Determine local pref/metric */
	local_metric = mrtentry_ptr->source->metric;
	local_preference = mrtentry_ptr->source->preference;

        local_wins = compare_metrics(local_preference, local_metric,
				     v->uv_lcl_addr, 
				     assert_preference, assert_metric, src);

	if (local_wins == FALSE) {
	    /* Assert sender wins - prune the interface */
	    IF_DEBUG(DEBUG_PIM_ASSERT)
		logit(LOG_DEBUG, 0, "\tAssert sender %s wins - pruning, timeout %d sec...",
		      inet_fmt(src, s1), assert_timeout);

	    VIFM_SET(vifi, mrtentry_ptr->asserted_oifs);
	    SET_TIMER(mrtentry_ptr->assert_timer, assert_timeout);
	    mrtentry_ptr->flags |= MRTF_ASSERTED;

	    state_change = 
		change_interfaces(mrtentry_ptr,
				  mrtentry_ptr->incoming,
				  mrtentry_ptr->pruned_oifs,
				  mrtentry_ptr->leaves);

	    /* Handle transition to negative cache */
	    if (state_change == -1)
		trigger_prune_alert(mrtentry_ptr);

	} /* assert sender wins */

	else {

	    /* Local wins (assert sender loses):
	     * send assert and schedule prune
	     */

	    IF_DEBUG(DEBUG_PIM_ASSERT)
		logit(LOG_DEBUG, 0,
		    "\tAssert sender %s loses - sending assert and scheuling prune", 
		    inet_fmt(src, s1));

	    if (!(VIFM_ISSET(vifi, mrtentry_ptr->leaves))) {
		/* No directly connected receivers - delay prune */
		send_pim_jp(mrtentry_ptr, PIM_ACTION_PRUNE, vifi, 
			    v->uv_lcl_addr, PIM_JOIN_PRUNE_HOLDTIME);
		schedule_delayed_prune(mrtentry_ptr, vifi, 
				       PIM_JOIN_PRUNE_HOLDTIME);
	    }
	    send_pim_assert(source, group, vifi, 
			    mrtentry_ptr->source->preference,
			    mrtentry_ptr->source->metric);
	}

    } /* if assert on oif */
	
    return TRUE;
}


int 
send_pim_assert(source, group, vifi, local_preference, local_metric)
    u_int32 source;
    u_int32 group;
    vifi_t vifi;
    u_int32 local_preference;
    u_int32 local_metric;
{
    u_int8 *data_ptr;
    u_int8 *data_start_ptr;

    data_ptr = (u_int8 *)(pim_send_buf + IP_HEADER_RAOPT_LEN + sizeof(pim_header_t));
    data_start_ptr = data_ptr;
    PUT_EGADDR(group, SINGLE_GRP_MSKLEN, 0, data_ptr);
    PUT_EUADDR(source, data_ptr);

    PUT_HOSTLONG(local_preference, data_ptr);
    PUT_HOSTLONG(local_metric, data_ptr);
    send_pim(pim_send_buf, uvifs[vifi].uv_lcl_addr, allpimrouters_group,
             PIM_ASSERT, data_ptr - data_start_ptr);

    return TRUE;
}


/* Return TRUE if the local win, otherwise FALSE */
static int
compare_metrics(local_preference, local_metric, local_address,
		remote_preference, remote_metric, remote_address)
    u_int32 local_preference;
    u_int32 local_metric;
    u_int32 local_address;
    u_int32 remote_preference;
    u_int32 remote_metric;
    u_int32 remote_address;
{
    /* Now lets see who has a smaller gun (aka "asserts war") */
    /* FYI, the smaller gun...err metric wins, but if the same
     * caliber, then the bigger network address wins. The order of
     * treatment is: preference, metric, address.
     */
    /* The RPT bits are already included as the most significant bits
     * of the preferences.
     */
    if (remote_preference > local_preference)
	return TRUE;
    if (remote_preference < local_preference)
	return FALSE;
    if (remote_metric > local_metric)
	return TRUE;
    if (remote_metric < local_metric)
	return FALSE;
    if (ntohl(local_address) > ntohl(remote_address))
	return TRUE;
    return FALSE;
}




/************************************************************************
 *                        PIM_GRAFT
 ************************************************************************/


u_long              graft_retrans_timer;   /* Graft retransmission timer */
static LIST_HEAD(, pim_graft_entry) graft_list = LIST_HEAD_INITIALIZER(); /* Active grafting entries */


void
delete_pim_graft_entry(mrtentry_ptr)
     mrtentry_t *mrtentry_ptr;
{
    pim_graft_entry_t *e;

    if (!mrtentry_ptr || !mrtentry_ptr->graft)
	return;

    e = mrtentry_ptr->graft;
    LIST_REMOVE(e, link);

    mrtentry_ptr->graft = NULL;
    free(e);

    /* Stop the timer if there are no more entries */
    if (LIST_EMPTY(&graft_list)) {
	timer_clearTimer(graft_retrans_timer);
	RESET_TIMER(graft_retrans_timer);
    }
}


int retransmit_pim_graft(mrtentry_ptr)
     mrtentry_t *mrtentry_ptr;
{
    u_int8 *data_ptr, *data_start_ptr;
	
    data_ptr = (u_int8 *)(pim_send_buf + IP_HEADER_RAOPT_LEN + sizeof(pim_header_t));
    data_start_ptr = data_ptr;

    if (!mrtentry_ptr->upstream) {
    	/* No upstream neighbor - don't send */
    	return FALSE;
    }
    
    IF_DEBUG(DEBUG_PIM_GRAFT)
	logit(LOG_DEBUG, 0,
	    "Sending GRAFT:  vif %s, src %s, grp %s, dst %s",
	    inet_fmt(uvifs[mrtentry_ptr->incoming].uv_lcl_addr, s1),
	    inet_fmt(mrtentry_ptr->source->address, s2), 
	    inet_fmt(mrtentry_ptr->group->group, s3),
	    inet_fmt(mrtentry_ptr->upstream->address, s4));

    
    PUT_EUADDR(mrtentry_ptr->upstream->address, data_ptr); /* unicast target */
    PUT_BYTE(0, data_ptr);		 /* Reserved */
    *data_ptr++ = (u_int8)1;             /* number of groups */
    PUT_HOSTSHORT(0, data_ptr);          /* no holdtime */
    
    /* data_ptr points at the first, and only encoded mcast group */
    PUT_EGADDR(mrtentry_ptr->group->group, SINGLE_GRP_MSKLEN, 0, data_ptr);
    
    /* set the number of join(graft) and prune sources */
    PUT_HOSTSHORT(1, data_ptr);
    PUT_HOSTSHORT(0, data_ptr);

    PUT_ESADDR(mrtentry_ptr->source->address, SINGLE_SRC_MSKLEN, 0, data_ptr);
    
    send_pim(pim_send_buf, uvifs[mrtentry_ptr->incoming].uv_lcl_addr, 
	     mrtentry_ptr->upstream->address,
	     PIM_GRAFT, data_ptr - data_start_ptr);
    
    return TRUE;
}


static void
retransmit_all_pim_grafts(arg)
     void *arg; /* UNUSED */
{
    pim_graft_entry_t *e, *tmp;

    IF_DEBUG(DEBUG_PIM_GRAFT)
	logit(LOG_DEBUG, 0, "Retransmitting all pending PIM-Grafts");
  
    LIST_FOREACH_SAFE(e, &graft_list, link, tmp) {
	IF_DEBUG(DEBUG_PIM_GRAFT)
	    logit(LOG_DEBUG, 0, "\tGRAFT src %s, grp %s",
		inet_fmt(e->mrtlink->source->address, s1),
		inet_fmt(e->mrtlink->group->group, s2));

	retransmit_pim_graft(e->mrtlink);
    }

    if (!LIST_EMPTY(&graft_list))
	timer_setTimer(PIM_GRAFT_RETRANS_PERIOD, retransmit_all_pim_grafts, NULL);
}


int
receive_pim_graft(src, dst, pim_message, datalen, pimtype)
    u_int32 src, dst;
    char *pim_message;
    int datalen;
    int pimtype;
{
    vifi_t vifi;
    struct uvif *v;
    pim_encod_uni_addr_t uni_target_addr;
    pim_encod_grp_addr_t encod_group;
    pim_encod_src_addr_t encod_src;
    u_int8 *data_ptr;
    u_int8 num_groups;
    u_int16 holdtime;
    u_int16 num_j_srcs;
    u_int16 num_p_srcs;
    u_int32 source;
    u_int32 group;
    u_int32 s_mask;
    u_int32 g_mask;
    u_int8 s_flags;
    u_int8 reserved;
    mrtentry_t *mrtentry_ptr;
    int state_change;

    if ((vifi = find_vif_direct(src)) == NO_VIF) {
        /* Either a local vif or somehow received PIM_GRAFT from
         * non-directly connected router. Ignore it.
         */
        if (local_address(src) == NO_VIF)
            logit(LOG_INFO, 0, "Ignoring PIM_GRAFT from non-neighbor router %s", inet_fmt(src, s1));
        return FALSE;
    }

    /* Checksum */
    if (inet_cksum((u_int16 *)pim_message, datalen))
        return FALSE;
 
    v = &uvifs[vifi];
    if (uvifs[vifi].uv_flags & (VIFF_DOWN | VIFF_DISABLED | VIFF_NONBRS))
        return FALSE;    /* Shoudn't come on this interface */

    data_ptr = (u_int8 *)(pim_message + sizeof(pim_header_t));

    /* Get the target address */
    GET_EUADDR(&uni_target_addr, data_ptr);
    GET_BYTE(reserved, data_ptr);
    GET_BYTE(num_groups, data_ptr);
    if (num_groups == 0)
        return FALSE;    /* No indication for groups in the message */
    GET_HOSTSHORT(holdtime, data_ptr);

    IF_DEBUG(DEBUG_PIM_GRAFT)
	logit(LOG_DEBUG, 0, "PIM %s received from %s on vif %s",
	      pimtype == PIM_GRAFT ? "GRAFT" : "GRAFT-ACK",
	      inet_fmt(src, s1), inet_fmt(uvifs[vifi].uv_lcl_addr, s2));
    
    while (num_groups--) {
	GET_EGADDR(&encod_group, data_ptr);
	GET_HOSTSHORT(num_j_srcs, data_ptr);
	GET_HOSTSHORT(num_p_srcs, data_ptr);
	MASKLEN_TO_MASK(encod_group.masklen, g_mask);
	group = encod_group.mcast_addr;
	if (!IN_MULTICAST(ntohl(group))) {
	    data_ptr += (num_j_srcs + num_p_srcs) * sizeof(pim_encod_src_addr_t);
	    continue; /* Ignore this group and jump to the next */
	}
	
	while (num_j_srcs--) {
	    GET_ESADDR(&encod_src, data_ptr);
	    source = encod_src.src_addr;
	    if (!inet_valid_host(source))
		continue;

	    s_flags = encod_src.flags;
	    MASKLEN_TO_MASK(encod_src.masklen, s_mask);
	    
	    mrtentry_ptr = find_route(source, group, MRTF_SG, DONT_CREATE);
	    if (!mrtentry_ptr)
		continue;

	    if (pimtype == PIM_GRAFT) {
		/* Graft */
		IF_DEBUG(DEBUG_PIM_GRAFT)
		    logit(LOG_DEBUG, 0, "\tGRAFT src %s, group %s - forward data on vif %d",
			  inet_fmt(source, s1), inet_fmt(group, s2), vifi);
		
		/* Cancel any delayed prune */
		if (mrtentry_ptr->prune_delay_timerids[vifi]) {
		    timer_clearTimer(mrtentry_ptr->prune_delay_timerids[vifi]);
		    mrtentry_ptr->prune_delay_timerids[vifi] = 0;
		}	    
		
		/* Add to oiflist (unprune) */
		if (VIFM_ISSET(vifi, mrtentry_ptr->pruned_oifs)) {
		    VIFM_CLR(vifi, mrtentry_ptr->pruned_oifs);
		    SET_TIMER(mrtentry_ptr->prune_timers[vifi], 0);
		    state_change =  change_interfaces(mrtentry_ptr,
						      mrtentry_ptr->incoming,
						      mrtentry_ptr->pruned_oifs,
						      mrtentry_ptr->leaves);
		    if (state_change == 1)
			trigger_join_alert(mrtentry_ptr);
		}
	    }  /* Graft */
	    else {
		/* Graft-Ack */
		if (mrtentry_ptr->graft)
		    delete_pim_graft_entry(mrtentry_ptr);
	    }
	}
	/* Ignore anything in the prune portion of the message! */
    }

    /* Respond to graft with a graft-ack */
    if (pimtype == PIM_GRAFT) {
	IF_DEBUG(DEBUG_PIM_GRAFT)
	    logit(LOG_DEBUG, 0, "Sending GRAFT-ACK: vif %s, dst %s",
		inet_fmt(uvifs[vifi].uv_lcl_addr, s1), inet_fmt(src, s2));
	bcopy(pim_message, pim_send_buf + IP_HEADER_RAOPT_LEN, datalen);
	send_pim(pim_send_buf, uvifs[vifi].uv_lcl_addr, 
		 src, PIM_GRAFT_ACK, datalen - sizeof(pim_header_t));
    }
	
    return TRUE;
}

int 
send_pim_graft(mrtentry_ptr)
     mrtentry_t *mrtentry_ptr;
{
    pim_graft_entry_t *e;
    int was_sent = 0;

    if (mrtentry_ptr->graft)
	return FALSE;		/* Already sending grafts */

    /* Send the first graft */
    was_sent = retransmit_pim_graft(mrtentry_ptr);
    if (!was_sent)
	return FALSE;

    /* Set up retransmission */
    e = calloc(1, sizeof(pim_graft_entry_t));
    if (!e) {
	logit(LOG_WARNING, 0, "Memory allocation error for graft entry src %s, grp %s",
	      inet_fmt(mrtentry_ptr->source->address, s1),
	      inet_fmt(mrtentry_ptr->group->group, s2));
	return FALSE;
    }

    LIST_INSERT_HEAD(&graft_list, e, link);

    e->mrtlink = mrtentry_ptr;
    mrtentry_ptr->graft = e;
	
    /* Set up timer if not running */
    if (!graft_retrans_timer) {
	int id;

	id = timer_setTimer(PIM_GRAFT_RETRANS_PERIOD, retransmit_all_pim_grafts, NULL);
	if (id != -1)
	    SET_TIMER(graft_retrans_timer, id);
    }

    return TRUE;
}

/**
 * Local Variables:
 *  c-file-style: "cc-mode"
 * End:
 */
