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
 *  $Id: route.c,v 1.27 1998/12/30 20:26:21 kurtw Exp $
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

#include "defs.h"


static void   process_cache_miss  __P((struct igmpmsg *igmpctl));
static void   process_wrong_iif   __P((struct igmpmsg *igmpctl));

u_int32         default_source_preference = DEFAULT_LOCAL_PREF;
u_int32         default_source_metric     = DEFAULT_LOCAL_METRIC;

/* Return the iif for given address */
vifi_t
get_iif(address)
    u_int32 address;
{
    struct rpfctl rpfc;

    k_req_incoming(address, &rpfc);
    if (rpfc.rpfneighbor.s_addr == INADDR_ANY_N)
	return (NO_VIF);
    return (rpfc.iif);
}

/* Return the PIM neighbor toward a source */
/* If route not found or if a local source or if a directly connected source,
 * but is not PIM router, or if the first hop router is not a PIM router,
 * then return NULL.
 */
pim_nbr_entry_t *
find_pim_nbr(source)
    u_int32 source;
{
    struct rpfctl rpfc;
    pim_nbr_entry_t *pim_nbr;
    u_int32 next_hop_router_addr;

    if (local_address(source) != NO_VIF)
	return (pim_nbr_entry_t *)NULL;
    k_req_incoming(source, &rpfc);
    if ((rpfc.rpfneighbor.s_addr == INADDR_ANY_N)
	|| (rpfc.iif == NO_VIF))
	return (pim_nbr_entry_t *)NULL;
    next_hop_router_addr = rpfc.rpfneighbor.s_addr;
    for (pim_nbr = uvifs[rpfc.iif].uv_pim_neighbors;
	 pim_nbr != (pim_nbr_entry_t *)NULL;
	 pim_nbr = pim_nbr->next)
	if (pim_nbr->address == next_hop_router_addr)
	    return(pim_nbr);
    return (pim_nbr_entry_t *)NULL;
}


/* TODO: check again the exact setup if the source is local or directly
 * connected!!!
 */
/* TODO: XXX: change the metric and preference for all (S,G) entries per
 * source?
 */
/* PIMDM TODO - If possible, this would be the place to correct set the
 * source's preference and metric to that obtained from the kernel
 * and/or unicast routing protocol.  For now, set it to the configured
 * default for local pref/metric.
 */
/*
 * Set the iif, upstream router, preference and metric for the route
 * toward the source. Return TRUE is the route was found, othewise FALSE.
 * If srctype==PIM_IIF_SOURCE and if the source is directly connected
 * then the "upstream" is set to NULL. 
 * Note that srctype is a hold-over from the PIM-SM daemon and is unused.
 */
int
set_incoming(srcentry_ptr, srctype)
    srcentry_t *srcentry_ptr;
    int srctype;
{
    struct rpfctl rpfc;
    u_int32 source = srcentry_ptr->address;
    u_int32 neighbor_addr;
    register struct uvif *v;
    register pim_nbr_entry_t *n;

    /* Preference will be 0 if directly connected */
    srcentry_ptr->preference = 0; 
    srcentry_ptr->metric = 0;

    if ((srcentry_ptr->incoming = local_address(source)) != NO_VIF) {
	/* The source is a local address */
	/* TODO: set the upstream to myself? */
	srcentry_ptr->upstream = (pim_nbr_entry_t *)NULL;
	return (TRUE);
    }

    if ((srcentry_ptr->incoming = find_vif_direct(source)) == NO_VIF) {
    /* TODO: probably need to check the case if the iif is disabled */
	/* Use the lastest resource: the kernel unicast routing table */
	k_req_incoming(source, &rpfc);
	if ((rpfc.iif == NO_VIF) ||
	    rpfc.rpfneighbor.s_addr == INADDR_ANY_N) {
	    /* couldn't find a route */
	    IF_DEBUG(DEBUG_PIM_MRT | DEBUG_RPF)
		log(LOG_DEBUG, 0, "NO ROUTE found for %s",
		    inet_fmt(source, s1));
	    return(FALSE);
	}
	srcentry_ptr->incoming = rpfc.iif;
	neighbor_addr = rpfc.rpfneighbor.s_addr;
    }
    else {
	/* The source is directly connected. 
	 */
	srcentry_ptr->upstream = (pim_nbr_entry_t *)NULL;
	return (TRUE);
    }

    /* set the preference for sources that aren't directly connected. */
    v = &uvifs[srcentry_ptr->incoming];
    srcentry_ptr->preference = v->uv_local_pref;
    srcentry_ptr->metric = v->uv_local_metric;

    /*
     * The upstream router must be a (PIM router) neighbor, otherwise we
     * are in big trouble ;-)
     */
    for (n = v->uv_pim_neighbors; n != NULL; n = n->next) {
	if (ntohl(neighbor_addr) < ntohl(n->address))
	    continue;
	if (neighbor_addr == n->address) {
	    /*
	     *The upstream router is found in the list of neighbors.
	     * We are safe!
	     */
	    srcentry_ptr->upstream = n;
	    IF_DEBUG(DEBUG_RPF)
		log(LOG_DEBUG, 0,
		    "For src %s, iif is %d, next hop router is %s",
		    inet_fmt(source, s1), srcentry_ptr->incoming,
		    inet_fmt(neighbor_addr, s2));
	    return(TRUE);
	}
	else break;
    }
    
    /* TODO: control the number of messages! */
    log(LOG_INFO, 0,
	"For src %s, iif is %d, next hop router is %s: NOT A PIM ROUTER",
	inet_fmt(source, s1), srcentry_ptr->incoming,
	inet_fmt(neighbor_addr, s2));
    srcentry_ptr->upstream = (pim_nbr_entry_t *)NULL; 

    return(FALSE);
}


/* Set the leaves in a new mrtentry */
void set_leaves(mrtentry_ptr)
     mrtentry_t *mrtentry_ptr;
{
    vifi_t vifi;
    struct uvif *v;
    
    /* Check for a group report on each vif */
    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) 
	if(check_grp_membership(v, mrtentry_ptr->group->group)) 
	    VIFM_SET(vifi, mrtentry_ptr->leaves);
}


/* Handle new receiver
 *
 * TODO: XXX: currently `source` is not used. Will be used with IGMPv3 where
 * we have source-specific Join/Prune.
 */
void
add_leaf(vifi, source, group)
    vifi_t vifi;
    u_int32 source;
    u_int32 group;
{
    grpentry_t *grpentry_ptr;
    mrtentry_t *mrtentry_srcs;
    vifbitmap_t new_leaves;
    int state_change;

    grpentry_ptr = find_group(group);
    if (grpentry_ptr == (grpentry_t *)NULL)
	return;

    /* walk the source list for the group and add vif to oiflist */
    for (mrtentry_srcs = grpentry_ptr->mrtlink;
	 mrtentry_srcs != (mrtentry_t *)NULL;
	 mrtentry_srcs = mrtentry_srcs->grpnext) {

	/* if applicable, add the vif to the leaves */
	if (mrtentry_srcs->incoming == vifi) 
	    continue;

	if(!(VIFM_ISSET(vifi, mrtentry_srcs->leaves))) {

	    IF_DEBUG(DEBUG_MRT)
		log(LOG_DEBUG, 0, "Adding leaf vif %d for src %s group %s", 
		    vifi,
		    inet_fmt(mrtentry_srcs->source->address, s1),
		    inet_fmt(group, s2));
	    
	    VIFM_COPY(mrtentry_srcs->leaves, new_leaves);
	    VIFM_SET(vifi, new_leaves);    /* Add the leaf */
	    
	    state_change = 
		change_interfaces(mrtentry_srcs,
				  mrtentry_srcs->incoming,
				  mrtentry_srcs->pruned_oifs,
				  new_leaves);

	    /* Handle transition from negative cache */
	    if(state_change == 1) 
		trigger_join_alert(mrtentry_srcs);
	}
    }
}


/*
 * TODO: XXX: currently `source` is not used. To be used with IGMPv3 where
 * we have source-specific joins/prunes.
 */
void
delete_leaf(vifi, source, group)
    vifi_t vifi;
    u_int32 source;
    u_int32 group;
{
    grpentry_t *grpentry_ptr;
    mrtentry_t *mrtentry_srcs;
    vifbitmap_t new_leaves;
    int state_change;

    /* mrtentry_t *mrtentry_ptr;
     * mrtentry_t *mrtentry_srcs;
     * vifbitmap_t new_oifs;
     * vifbitmap_t old_oifs;
     * vifbitmap_t new_leaves;
     */

    grpentry_ptr = find_group(group);
    if (grpentry_ptr == (grpentry_t *)NULL)
	return;

    /* walk the source list for the group and delete vif to leaves */
    for (mrtentry_srcs = grpentry_ptr->mrtlink;
	 mrtentry_srcs != (mrtentry_t *)NULL;
	 mrtentry_srcs = mrtentry_srcs->grpnext) {

	/* if applicable, delete the vif from the leaves */
	if (mrtentry_srcs->incoming == vifi) 
	    continue;

	if(VIFM_ISSET(vifi, mrtentry_srcs->leaves)) {

	    IF_DEBUG(DEBUG_MRT)
		log(LOG_DEBUG, 0, "Deleting leaf vif %d for src %s, group %s",
		    vifi,
		    inet_fmt(mrtentry_srcs->source->address, s1), 
		    inet_fmt(group, s2));
	    
	    VIFM_COPY(mrtentry_srcs->leaves, new_leaves);
	    VIFM_CLR(vifi, new_leaves);    /* Remove the leaf */
	    
	    state_change = 
		change_interfaces(mrtentry_srcs,
				  mrtentry_srcs->incoming,
				  mrtentry_srcs->pruned_oifs,
				  new_leaves);

	    /* Handle transition to negative cache */
	    if(state_change == -1)
		trigger_prune_alert(mrtentry_srcs);
	}
    }
}

void
calc_oifs(mrtentry_ptr, oifs_ptr)
    mrtentry_t *mrtentry_ptr;
    vifbitmap_t *oifs_ptr;
{
    vifbitmap_t oifs;

    /*
     * oifs =
     * ((nbr_ifs - my_prune) + my_leaves) - incoming_interface,
     * i.e. `leaves` have higher priority than `prunes`. 
     * Asserted oifs (those that lost assert) are handled as pruned oifs.
     * The incoming interface is always deleted from the oifs
     */

    if (mrtentry_ptr == (mrtentry_t *)NULL) {
        VIFM_CLRALL(*oifs_ptr);
        return;
    }
 
    VIFM_COPY(nbr_vifs, oifs);
    VIFM_CLR_MASK(oifs, mrtentry_ptr->pruned_oifs);
    VIFM_MERGE(oifs, mrtentry_ptr->leaves, oifs);
    VIFM_CLR(mrtentry_ptr->incoming, oifs);
    VIFM_COPY(oifs, *oifs_ptr);
}


/*
 * Set the iif, join/prune/leaves/asserted interfaces. Calculate and
 * set the oifs.
 * Return 1 if oifs change from NULL to not-NULL.
 * Return -1 if oifs change from non-NULL to NULL
 *  else return 0
 * If the iif change or if the oifs change from NULL to non-NULL
 * or vice-versa, then schedule that mrtentry join/prune timer to
 * timeout immediately. 
 */
int
change_interfaces(mrtentry_ptr, new_iif, new_pruned_oifs,
		  new_leaves_)
    mrtentry_t *mrtentry_ptr;
    vifi_t new_iif;
    vifbitmap_t new_pruned_oifs;
    vifbitmap_t new_leaves_;
{
    vifbitmap_t old_pruned_oifs;
    vifbitmap_t old_leaves;
    vifbitmap_t new_leaves;
    vifbitmap_t new_real_oifs;    /* The result oifs */
    vifbitmap_t old_real_oifs;
    vifi_t      old_iif;
    int return_value;
    
    if (mrtentry_ptr == (mrtentry_t *)NULL)
	return (0);

    VIFM_COPY(new_leaves_, new_leaves);

    old_iif = mrtentry_ptr->incoming;
    VIFM_COPY(mrtentry_ptr->leaves, old_leaves);
    VIFM_COPY(mrtentry_ptr->pruned_oifs, old_pruned_oifs);

    VIFM_COPY(mrtentry_ptr->oifs, old_real_oifs);
    
    mrtentry_ptr->incoming = new_iif;
    VIFM_COPY(new_pruned_oifs, mrtentry_ptr->pruned_oifs);
    VIFM_COPY(new_leaves, mrtentry_ptr->leaves);
    calc_oifs(mrtentry_ptr, &new_real_oifs);

    if (VIFM_ISEMPTY(old_real_oifs)) {
	if (VIFM_ISEMPTY(new_real_oifs))
	    return_value = 0;
	else
	    return_value = 1;
    } else {
	if (VIFM_ISEMPTY(new_real_oifs))
	    return_value = -1;
	else
	    return_value = 0;
    }

    if ((VIFM_SAME(new_real_oifs, old_real_oifs))
	&& (new_iif == old_iif))
	return 0;                  /* Nothing to change */

    VIFM_COPY(new_real_oifs, mrtentry_ptr->oifs);
    
    k_chg_mfc(igmp_socket, mrtentry_ptr->source->address,
	      mrtentry_ptr->group->group, new_iif, new_real_oifs);

#ifdef RSRR
    rsrr_cache_send(mrtentry_ptr, RSRR_NOTIFICATION_OK);
#endif /* RSRR */

    return (return_value);
}
    

/* TODO: implement it. Required to allow changing of the physical interfaces
 * configuration without need to restart pimd.
 */
int
delete_vif_from_mrt(vifi)
vifi_t vifi;
{
    return TRUE;
}


static u_int16
max_prune_timeout(mrtentry_ptr)
     mrtentry_t *mrtentry_ptr;
{
    vifi_t vifi;
    u_int16 time_left, max_holdtime = 0;

    for(vifi=0; vifi < numvifs; ++vifi) 
	if(VIFM_ISSET(vifi, mrtentry_ptr->pruned_oifs))
	    IF_TIMER_SET(mrtentry_ptr->prune_timers[vifi]) 
		/* XXX - too expensive ? */
		/* XXX: TIMER implem. dependency! */
		if(mrtentry_ptr->prune_timers[vifi] > max_holdtime) 
		    max_holdtime = time_left;
    
    if(max_holdtime == 0) 
	max_holdtime = (u_int16)PIM_JOIN_PRUNE_HOLDTIME;

    return(max_holdtime);
}


void process_kernel_call()
{
    register struct igmpmsg *igmpctl; /* igmpmsg control struct */
    
    igmpctl = (struct igmpmsg *) igmp_recv_buf;
    
    switch (igmpctl->im_msgtype) {
    case IGMPMSG_NOCACHE:
	process_cache_miss(igmpctl);
	break;
    case IGMPMSG_WRONGVIF:
	process_wrong_iif(igmpctl);
	break;
    default:
	IF_DEBUG(DEBUG_KERN)
	    log(LOG_DEBUG, 0, "Unknown kernel_call code");
	break;
    }
}


/*
 * Protocol actions:
 *   1. Create (S,G) entry (find_route(CREATE))
 *      a. set iif and oifs
 */
static void
process_cache_miss(igmpctl)
    struct igmpmsg *igmpctl;
{
    u_int32 source;
    u_int32 group;
    mrtentry_t *mrtentry_ptr;

    /*
     * When there is a cache miss, we check only the header of the packet
     * (and only it should be sent up by the kernel.
     */

    group  = igmpctl->im_dst.s_addr;
    source = igmpctl->im_src.s_addr;
    
    IF_DEBUG(DEBUG_MFC)
	log(LOG_DEBUG, 0, "Cache miss, src %s, dst %s",
	    inet_fmt(source, s1), inet_fmt(group, s2)); 

    /* Don't create routing entries for the LAN scoped addresses */
    if (ntohl(group) <= INADDR_MAX_LOCAL_GROUP)
	return; 
    
    /* Create the (S,G) entry */
    mrtentry_ptr = find_route(source, group, MRTF_SG, CREATE);
    if (mrtentry_ptr == (mrtentry_t *)NULL)
	return;
    mrtentry_ptr->flags &= ~MRTF_NEW;
    
    /* Initialize Entry Timer */
    SET_TIMER(mrtentry_ptr->timer, PIM_DATA_TIMEOUT);
    
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
    if(VIFM_ISEMPTY(mrtentry_ptr->oifs))
	trigger_prune_alert(mrtentry_ptr);
}


/*
 * A multicast packet has been received on wrong iif by the kernel.
 * If the packet was received on a point-to-point interface, rate-limit
 * prunes.  if the packet was received on a LAN interface, rate-limit 
 * asserts.
 */
static void
process_wrong_iif(igmpctl)
    struct igmpmsg *igmpctl;
{
    u_int32 source;
    u_int32 group;
    vifi_t  vifi;
    mrtentry_t *mrtentry_ptr;

    group  = igmpctl->im_dst.s_addr;
    source = igmpctl->im_src.s_addr;
    vifi   = igmpctl->im_vif;

    /* PIMDM TODO Don't create routing entries for the LAN scoped addresses */
    if (ntohl(group) <= INADDR_MAX_LOCAL_GROUP)
	return;

    /* Ratelimit prunes or asserts */
    if(uvifs[vifi].uv_flags & VIFF_POINT_TO_POINT) {

	mrtentry_ptr = find_route(source, group, MRTF_SG, DONT_CREATE);
	if(mrtentry_ptr == (mrtentry_t *)NULL)
	    return;

	/* Wrong vif on P2P interface - rate-limit prunes */

	if(mrtentry_ptr->last_prune[vifi] == virtual_time)
	    /* Skip due to rate-limiting */
	    return;
	mrtentry_ptr->last_prune[vifi] = virtual_time;

	if(uvifs[vifi].uv_rmt_addr)
	    send_pim_jp(mrtentry_ptr, PIM_ACTION_PRUNE, vifi, 
			uvifs[vifi].uv_rmt_addr, 
			max_prune_timeout(mrtentry_ptr));
	else 
	    log(LOG_WARNING, 0, 
		"Can't send wrongvif prune on p2p %s: no remote address",
		uvifs[vifi].uv_lcl_addr);
    } else {
	u_int32 pref, metric;

	/* Wrong vif on LAN interface - rate-limit asserts */

	mrtentry_ptr = find_route(source, group, MRTF_SG, DONT_CREATE);
	if(mrtentry_ptr == (mrtentry_t *)NULL) {
	    pref = 0x7fffffff;
	    metric = 0x7fffffff;
	}
	else {
	    pref = mrtentry_ptr->source->preference;
	    metric = mrtentry_ptr->source->metric;
	}
	    
	if(mrtentry_ptr->last_assert[vifi] == virtual_time)
	    /* Skip due to rate-limiting */
	    return;
	mrtentry_ptr->last_assert[vifi] = virtual_time;

	/* Send the assert */
	send_pim_assert(source, group, vifi, pref, metric);
    }
}


void trigger_prune_alert(mrtentry_ptr)
     mrtentry_t *mrtentry_ptr;
{
    IF_DEBUG(DEBUG_MRT)
	log(LOG_DEBUG, 0, "Now negative cache for src %s, grp %s - pruning",
	    inet_fmt(mrtentry_ptr->source->address, s1), 
	    inet_fmt(mrtentry_ptr->group->group, s2));

    /* Set the entry timer to the max of the prune timers */
    SET_TIMER(mrtentry_ptr->timer, max_prune_timeout(mrtentry_ptr));

    /* Send a prune */
    if(mrtentry_ptr->upstream) 
	send_pim_jp(mrtentry_ptr, PIM_ACTION_PRUNE, mrtentry_ptr->incoming,
		    mrtentry_ptr->upstream->address, 
		    max_prune_timeout(mrtentry_ptr));
}

void trigger_join_alert(mrtentry_ptr)
     mrtentry_t *mrtentry_ptr;
{
    IF_DEBUG(DEBUG_MRT)
	log(LOG_DEBUG, 0, "Now forwarding state for src %s, grp %s - grafting",
	    inet_fmt(mrtentry_ptr->source->address, s1), 
	    inet_fmt(mrtentry_ptr->group->group, s2));

    /* Refresh the entry timer */
    SET_TIMER(mrtentry_ptr->timer, PIM_DATA_TIMEOUT);

    /* Send graft */
    send_pim_graft(mrtentry_ptr);
}
