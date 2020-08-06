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

srcentry_t		*srclist;
grpentry_t		*grplist;

/*
 * Local functions definition
 */
static srcentry_t *create_srcentry  (u_int32 source);
static int        search_srclist    (u_int32 source, srcentry_t **sourceEntry);
static int        search_srcmrtlink (srcentry_t *srcentry_ptr, u_int32 group, mrtentry_t **mrtPtr);
static void       insert_srcmrtlink (mrtentry_t *elementPtr, mrtentry_t *insertPtr, srcentry_t *srcListPtr);
static grpentry_t *create_grpentry  (u_int32 group);
static int        search_grplist    (u_int32 group, grpentry_t **groupEntry);
static int        search_grpmrtlink (grpentry_t *grpentry_ptr, u_int32 source, mrtentry_t **mrtPtr);
static void       insert_grpmrtlink (mrtentry_t *elementPtr, mrtentry_t *insertPtr, grpentry_t *grpListPtr);
static mrtentry_t *alloc_mrtentry   (srcentry_t *srcentry_ptr, grpentry_t *grpentry_ptr);
static mrtentry_t *create_mrtentry  (srcentry_t *srcentry_ptr, grpentry_t *grpentry_ptr, u_int16 flags);


void 
init_pim_mrt()
{
    /* TODO: delete any existing routing table */

    /* Initialize the source list */
    /* The first entry has address 'INADDR_ANY' and is not used */
    /* The order is the smallest address first. */
    srclist	        = calloc(1, sizeof(srcentry_t));
    if (!srclist)
	    logit(LOG_ERR, 0, "%s(): out of memory", __func__);
    srclist->next       = NULL;
    srclist->prev       = NULL;
    srclist->address	= INADDR_ANY_N;
    srclist->mrtlink    = NULL;
    srclist->incoming   = NO_VIF;
    srclist->upstream   = NULL;
    srclist->metric     = 0;
    srclist->preference = 0;
    RESET_TIMER(srclist->timer);
    
    /* Initialize the group list */
    /* The first entry has address 'INADDR_ANY' and is not used */
    /* The order is the smallest address first. */
    grplist	        = calloc(1, sizeof(grpentry_t));
    if (!grplist)
	    logit(LOG_ERR, 0, "%s(): out of memory", __func__);
    grplist->next       = NULL;
    grplist->prev       = NULL;
    grplist->group	= INADDR_ANY_N;
    grplist->mrtlink    = NULL;
}


void
mrt_clean(void)
{
    if (srclist)
	free(srclist);
    if (grplist)
	free(grplist);
    srclist = NULL;
    grplist = NULL;
}


grpentry_t*
find_group(group)
    u_int32 group;
{
    grpentry_t *grpentry_ptr;

    if (!IN_MULTICAST(ntohl(group)))
	return NULL;
    
    if (search_grplist(group, &grpentry_ptr) == TRUE)
	/* Group found! */
	return grpentry_ptr;

    return NULL;
}


srcentry_t *
find_source(source)
    u_int32 source;
{
    srcentry_t *srcentry_ptr;

    if (!inet_valid_host(source))
	return NULL;
    
    if (search_srclist(source, &srcentry_ptr) == TRUE)
	/* Source found! */
	return srcentry_ptr;

    return NULL;
}


mrtentry_t *
find_route(source, group, flags, create)
    u_int32 source, group;
    u_int16 flags;
    char create;
{
    srcentry_t *srcentry_ptr;
    grpentry_t *grpentry_ptr;
    mrtentry_t *mrtentry_ptr;

    if (!IN_MULTICAST(ntohl(group)))
	return NULL;
    
    if (!inet_valid_host(source))
	return NULL;
    
    if (create == DONT_CREATE) {
	if (search_grplist(group, &grpentry_ptr) == FALSE) 
	    return NULL;

	/* Search for the source */
	if (search_grpmrtlink(grpentry_ptr, source, &mrtentry_ptr) == TRUE)
	    /* Exact (S,G) entry found */
	    return mrtentry_ptr;

	return NULL;
    }

    /* Creation allowed */

    grpentry_ptr = create_grpentry(group);
    if (!grpentry_ptr)
	return NULL;

    /* Setup the (S,G) routing entry */
    srcentry_ptr = create_srcentry(source);
    if (!srcentry_ptr) {
	if (!grpentry_ptr->mrtlink)
	    /* New created grpentry. Delete it. */
	    delete_grpentry(grpentry_ptr);

	return NULL;
    }

    mrtentry_ptr = create_mrtentry(srcentry_ptr, grpentry_ptr, MRTF_SG);
    if (!mrtentry_ptr) {
	if (!grpentry_ptr->mrtlink)
	    /* New created grpentry. Delete it. */
	    delete_grpentry(grpentry_ptr);

	if (!srcentry_ptr->mrtlink)
	    /* New created srcentry. Delete it. */
	    delete_srcentry(srcentry_ptr);

	return NULL;
    }
    
    if (mrtentry_ptr->flags & MRTF_NEW) {
	/* The mrtentry pref/metric should be the pref/metric of the 
	 * _upstream_ assert winner. Since this isn't known now, 
	 * set it to the config'ed default
	 */
	mrtentry_ptr->incoming = srcentry_ptr->incoming;
	mrtentry_ptr->upstream = srcentry_ptr->upstream;
	mrtentry_ptr->metric   = srcentry_ptr->metric;
	mrtentry_ptr->preference = srcentry_ptr->preference;
    }
    
    return mrtentry_ptr;
}


void
delete_srcentry(srcentry_ptr)
    srcentry_t *srcentry_ptr;
{
    mrtentry_t *mrtentry_ptr;
    mrtentry_t *mrtentry_next;

    if (!srcentry_ptr)
	return;

    /* TODO: XXX: the first entry is unused and always there */
    srcentry_ptr->prev->next = 	srcentry_ptr->next;
    if (srcentry_ptr->next)
	srcentry_ptr->next->prev = srcentry_ptr->prev;
    
    for (mrtentry_ptr = srcentry_ptr->mrtlink;
	 mrtentry_ptr;
	 mrtentry_ptr = mrtentry_next) {
	mrtentry_next = mrtentry_ptr->srcnext;
	if (mrtentry_ptr->grpprev)
	    mrtentry_ptr->grpprev->grpnext = mrtentry_ptr->grpnext;
	else {
	    mrtentry_ptr->group->mrtlink = mrtentry_ptr->grpnext;
	    if (!mrtentry_ptr->grpnext) {
		/* Delete the group entry */
		delete_grpentry(mrtentry_ptr->group);
	    }
	}
	if (mrtentry_ptr->grpnext)
	    mrtentry_ptr->grpnext->grpprev = mrtentry_ptr->grpprev;
	FREE_MRTENTRY(mrtentry_ptr);
    }

    free(srcentry_ptr);
}


void
delete_grpentry(grpentry_ptr)
    grpentry_t *grpentry_ptr;
{
    mrtentry_t *mrtentry_ptr;
    mrtentry_t *mrtentry_next;
    
    if (!grpentry_ptr)
	return;

    /* TODO: XXX: the first entry is unused and always there */
    grpentry_ptr->prev->next = grpentry_ptr->next;
    if (grpentry_ptr->next)
	grpentry_ptr->next->prev = grpentry_ptr->prev;
    
    for (mrtentry_ptr = grpentry_ptr->mrtlink; mrtentry_ptr; mrtentry_ptr = mrtentry_next) {
	mrtentry_next = mrtentry_ptr->grpnext;
	if (mrtentry_ptr->srcprev)
	    mrtentry_ptr->srcprev->srcnext = mrtentry_ptr->srcnext;
	else {
	    mrtentry_ptr->source->mrtlink = mrtentry_ptr->srcnext;
	    if (!mrtentry_ptr->srcnext) {
		/* Delete the srcentry if this was the last routing entry */
		delete_srcentry(mrtentry_ptr->source);
	    }
	}
	if (mrtentry_ptr->srcnext)
	    mrtentry_ptr->srcnext->srcprev = mrtentry_ptr->srcprev;
	FREE_MRTENTRY(mrtentry_ptr);
    }

    free(grpentry_ptr);
}


void
delete_mrtentry(mrtentry_ptr)
    mrtentry_t *mrtentry_ptr;
{
    if (!mrtentry_ptr)
	return;

    /* Delete the kernel cache first */
    k_del_mfc(igmp_socket, mrtentry_ptr->source->address, mrtentry_ptr->group->group);

#ifdef RSRR
    /* Tell the reservation daemon */
    rsrr_cache_clean(mrtentry_ptr);
#endif /* RSRR */

   /* (S,G) mrtentry */
    /* Delete from the grpentry MRT chain */
    if (mrtentry_ptr->grpprev)
	mrtentry_ptr->grpprev->grpnext = mrtentry_ptr->grpnext;
    else {
	mrtentry_ptr->group->mrtlink = mrtentry_ptr->grpnext;
	if (!mrtentry_ptr->grpnext)
	    delete_grpentry(mrtentry_ptr->group);
    }
	
    if (mrtentry_ptr->grpnext)
	mrtentry_ptr->grpnext->grpprev = mrtentry_ptr->grpprev;
    
    /* Delete from the srcentry MRT chain */
    if (mrtentry_ptr->srcprev)
	mrtentry_ptr->srcprev->srcnext = mrtentry_ptr->srcnext;
    else {
	mrtentry_ptr->source->mrtlink = mrtentry_ptr->srcnext;
	/* Delete the srcentry if this is the last routing entry */
	if (!mrtentry_ptr->srcnext)
	    delete_srcentry(mrtentry_ptr->source);
    }
    if (mrtentry_ptr->srcnext)
	mrtentry_ptr->srcnext->srcprev = mrtentry_ptr->srcprev;

    delete_pim_graft_entry(mrtentry_ptr);
    FREE_MRTENTRY(mrtentry_ptr);
}


static int
search_srclist(source, sourceEntry)
    u_int32 source;
    srcentry_t **sourceEntry;
{
    srcentry_t *s_prev,*s;
    u_int32 source_h = ntohl(source);
    
    for (s_prev = srclist, s = s_prev->next; s; s_prev = s, s = s->next) {
	/* The srclist is ordered with the smallest addresses first.
	 * The first entry is not used.
	 */
	if (ntohl(s->address) < source_h)
	    continue;

	if (s->address == source) {
	    *sourceEntry = s;
	    return TRUE;
	}
	break;  
    }
    *sourceEntry = s_prev;   /* The insertion point is between s_prev and s */

    return FALSE;
}


static int
search_grplist(group, groupEntry)
    u_int32 group;
    grpentry_t **groupEntry;
{
    grpentry_t *g_prev, *g;
    u_int32 group_h = ntohl(group);
    
    for (g_prev = grplist, g = g_prev->next; g; g_prev = g, g = g->next) {
	/* The grplist is ordered with the smallest address first.
	 * The first entry is not used.
	 */
	if (ntohl(g->group) < group_h)
	    continue;

	if (g->group == group) {
	    *groupEntry = g;
	    return TRUE;
	}
	break;
    }
    *groupEntry = g_prev;    /* The insertion point is between g_prev and g */

    return FALSE;
}


static srcentry_t *
create_srcentry(source)
    u_int32 source;
{
    srcentry_t *srcentry_ptr;
    srcentry_t *srcentry_prev;

    if (search_srclist(source, &srcentry_prev) == TRUE)
	return srcentry_prev;
    
    srcentry_ptr = calloc(1, sizeof(srcentry_t));
    if (!srcentry_ptr) {
	logit(LOG_WARNING, 0, "Memory allocation error for srcentry %s", inet_fmt(source, s1));
	return NULL;
    }

    srcentry_ptr->address = source;
    /*
     * Free the memory if there is error getting the iif and
     * the next hop (upstream) router.
     */ 
    if (set_incoming(srcentry_ptr, PIM_IIF_SOURCE) == FALSE) {
	free(srcentry_ptr);
	return NULL;
    }

    srcentry_ptr->mrtlink = NULL;
    RESET_TIMER(srcentry_ptr->timer);
    srcentry_ptr->next	= srcentry_prev->next;
    srcentry_prev->next = srcentry_ptr;
    srcentry_ptr->prev = srcentry_prev;
    if (srcentry_ptr->next)
	srcentry_ptr->next->prev = srcentry_ptr;
    
    IF_DEBUG(DEBUG_MFC)
	logit(LOG_DEBUG, 0, "create source entry, source %s", inet_fmt(source, s1));

    return srcentry_ptr;
}


static grpentry_t *
create_grpentry(group)
    u_int32 group;
{
    grpentry_t *grpentry_ptr;
    grpentry_t *grpentry_prev;

    if (search_grplist(group, &grpentry_prev) == TRUE)
	return grpentry_prev;
    
    grpentry_ptr = calloc(1, sizeof(grpentry_t));
    if (!grpentry_ptr) {
	logit(LOG_WARNING, 0, "Memory allocation error for grpentry %s", inet_fmt(group, s1));
	return NULL;
    }

    grpentry_ptr->group	        = group;
    grpentry_ptr->mrtlink       = NULL;

    /* Now it is safe to include the new group entry */
    grpentry_ptr->next		= grpentry_prev->next;
    grpentry_prev->next         = grpentry_ptr;
    grpentry_ptr->prev          = grpentry_prev;
    if (grpentry_ptr->next)
	grpentry_ptr->next->prev = grpentry_ptr;
    
    IF_DEBUG(DEBUG_MFC)
	logit(LOG_DEBUG, 0, "create group entry, group %s", inet_fmt(group, s1));

    return grpentry_ptr;
}


/*
 * Return TRUE if the entry is found and then *mrtPtr is set to point to that
 * entry. Otherwise return FALSE and *mrtPtr points the the previous entry
 * (or NULL if first in the chain.
 */
static int
search_srcmrtlink(srcentry_ptr, group, mrtPtr)
    srcentry_t *srcentry_ptr;	
    u_int32 group;
    mrtentry_t **mrtPtr;
{
    mrtentry_t *mrtentry_ptr;
    mrtentry_t *m_prev = NULL;
    u_int32 group_h = ntohl(group);
    
    for (mrtentry_ptr = srcentry_ptr->mrtlink;
	 mrtentry_ptr;
	 m_prev = mrtentry_ptr, mrtentry_ptr = mrtentry_ptr->srcnext) {
	/* The entries are ordered with the smaller group address first.
	 * The addresses are in network order.
	 */
	if (ntohl(mrtentry_ptr->group->group) < group_h)
	    continue;

	if (mrtentry_ptr->group->group == group) {
	    *mrtPtr = mrtentry_ptr;
	    return TRUE;
	}
	break;
    }
    *mrtPtr = m_prev;

    return FALSE;
}


/*
 * Return TRUE if the entry is found and then *mrtPtr is set to point to that
 * entry. Otherwise return FALSE and *mrtPtr points the the previous entry
 * (or NULL if first in the chain.
 */
static int
search_grpmrtlink(grpentry_ptr, source, mrtPtr)
    grpentry_t *grpentry_ptr;
    u_int32 source;
    mrtentry_t **mrtPtr;
{
    mrtentry_t *mrtentry_ptr;
    mrtentry_t *m_prev = NULL;
    u_int32 source_h = ntohl(source);
    
    for (mrtentry_ptr = grpentry_ptr->mrtlink;
	 mrtentry_ptr;
	 m_prev = mrtentry_ptr, mrtentry_ptr = mrtentry_ptr->grpnext) {
	/* The entries are ordered with the smaller source address first.
	 * The addresses are in network order.
	 */
	if (ntohl(mrtentry_ptr->source->address) < source_h)
	    continue;

	if (source == mrtentry_ptr->source->address) {
	    *mrtPtr = mrtentry_ptr;
	    return TRUE;
	}
	break;
    }
    *mrtPtr = m_prev;

    return FALSE;
}


static void
insert_srcmrtlink(mrtentry_new, mrtentry_prev, srcentry_ptr)
    mrtentry_t *mrtentry_new;
    mrtentry_t *mrtentry_prev;
    srcentry_t *srcentry_ptr;
{
    if (!mrtentry_prev) {
	/* Has to be insert as the head entry for this source */
	mrtentry_new->srcnext = srcentry_ptr->mrtlink;
	mrtentry_new->srcprev = NULL;
	srcentry_ptr->mrtlink = mrtentry_new;
    }
    else {
	/* Insert right after the mrtentry_prev */
	mrtentry_new->srcnext = mrtentry_prev->srcnext;
	mrtentry_new->srcprev = mrtentry_prev;
	mrtentry_prev->srcnext = mrtentry_new;
    }
    if (mrtentry_new->srcnext)
	mrtentry_new->srcnext->srcprev = mrtentry_new;
}


static void
insert_grpmrtlink(mrtentry_new, mrtentry_prev, grpentry_ptr)
    mrtentry_t *mrtentry_new;
    mrtentry_t *mrtentry_prev;
    grpentry_t *grpentry_ptr;
{
    if (!mrtentry_prev) {
	/* Has to be insert as the head entry for this group */
	mrtentry_new->grpnext = grpentry_ptr->mrtlink;
	mrtentry_new->grpprev = NULL;
	grpentry_ptr->mrtlink = mrtentry_new;
    }
    else {
	/* Insert right after the mrtentry_prev */
	mrtentry_new->grpnext = mrtentry_prev->grpnext;
	mrtentry_new->grpprev = mrtentry_prev;
	mrtentry_prev->grpnext = mrtentry_new;
    }
    if (mrtentry_new->grpnext)
	mrtentry_new->grpnext->grpprev = mrtentry_new;
}


static mrtentry_t *
alloc_mrtentry(srcentry_ptr, grpentry_ptr)
    srcentry_t *srcentry_ptr;
    grpentry_t *grpentry_ptr;
{
    mrtentry_t *mrtentry_ptr;
    u_int16 i, *i_ptr;
    u_long  *il_ptr;
    u_int8  vif_numbers;
    
    mrtentry_ptr = calloc(1, sizeof(mrtentry_t));
    if (!mrtentry_ptr) {
	logit(LOG_WARNING, 0, "alloc_mrtentry(): out of memory");
	return NULL;
    }
    
    /*
     * grpnext, grpprev, srcnext, srcprev will be setup when we link the
     * mrtentry to the source and group chains
     */
    mrtentry_ptr->source  = srcentry_ptr;
    mrtentry_ptr->group   = grpentry_ptr;
    mrtentry_ptr->incoming = NO_VIF;
    VIFM_CLRALL(mrtentry_ptr->leaves);
    VIFM_CLRALL(mrtentry_ptr->pruned_oifs);
    VIFM_CLRALL(mrtentry_ptr->oifs);
    mrtentry_ptr->upstream = NULL;
    mrtentry_ptr->metric = 0;
    mrtentry_ptr->preference = 0;
#ifdef RSRR
    mrtentry_ptr->rsrr_cache = NULL;
#endif /* RSRR */

/*
 * XXX: TODO: if we are short in memory, we can reserve as few as possible
 * space for vif timers (per group and/or routing entry), but then everytime
 * when a new interfaces is configured, the router will be restarted and
 * will delete the whole routing table. The "memory is cheap" solution is
 * to reserve timer space for all potential vifs in advance and then no
 * need to delete the routing table and disturb the forwarding.
 */
#ifdef SAVE_MEMORY
    mrtentry_ptr->prune_timers         = calloc(numvifs, sizeof(u_int16));
    mrtentry_ptr->prune_delay_timerids = calloc(numvifs, sizeof(u_long));
    mrtentry_ptr->last_assert          = calloc(numvifs, sizeof(u_long));
    mrtentry_ptr->last_prune           = calloc(numvifs, sizeof(u_long));
    vif_numbers = numvifs;
#else
    mrtentry_ptr->prune_timers         = calloc(total_interfaces, sizeof(u_int16));
    mrtentry_ptr->prune_delay_timerids = calloc(total_interfaces, sizeof(u_long));
    mrtentry_ptr->last_assert          = calloc(total_interfaces, sizeof(u_long));
    mrtentry_ptr->last_prune           = calloc(total_interfaces, sizeof(u_long));
    vif_numbers = total_interfaces;
#endif /* SAVE_MEMORY */

    if (!mrtentry_ptr->prune_timers)
	    goto nomem;
    if (!mrtentry_ptr->prune_delay_timerids)
	    goto nomem;
    if (!mrtentry_ptr->last_assert)
	    goto nomem;
    if (!mrtentry_ptr->last_prune) {
    nomem:
	logit(LOG_WARNING, 0, "alloc_mrtentry(): out of memory");
	FREE_MRTENTRY(mrtentry_ptr);
	return NULL;
    }

    /* Reset the timers */
    for (i = 0, i_ptr = mrtentry_ptr->prune_timers; i < vif_numbers;
	 i++, i_ptr++)
	RESET_TIMER(*i_ptr);
    for (i = 0, il_ptr = mrtentry_ptr->prune_delay_timerids; i < vif_numbers;
	 i++, il_ptr++)
	RESET_TIMER(*il_ptr);
    for (i = 0, il_ptr = mrtentry_ptr->last_assert; i < vif_numbers;
	 i++, il_ptr++)
	RESET_TIMER(*il_ptr);
    for (i = 0, il_ptr = mrtentry_ptr->last_prune; i < vif_numbers;
	 i++, il_ptr++)
	RESET_TIMER(*il_ptr);

    mrtentry_ptr->flags = MRTF_NEW;
    RESET_TIMER(mrtentry_ptr->timer);
    mrtentry_ptr->join_delay_timerid = 0;
    RESET_TIMER(mrtentry_ptr->assert_timer);
    mrtentry_ptr->graft = NULL;

    return mrtentry_ptr;
}


static mrtentry_t *
create_mrtentry(srcentry_ptr, grpentry_ptr, flags)
    srcentry_t *srcentry_ptr;
    grpentry_t *grpentry_ptr;
    u_int16 flags;
{
    mrtentry_t *r_new;
    mrtentry_t *r_grp_insert, *r_src_insert; /* pointers to insert */
    u_int32 source;
    u_int32 group;

    /* (S,G) entry */
    source = srcentry_ptr->address;
    group  = grpentry_ptr->group;
    
    if (search_grpmrtlink(grpentry_ptr, source, &r_grp_insert) == TRUE)
	return r_grp_insert;

    if (search_srcmrtlink(srcentry_ptr, group, &r_src_insert) == TRUE) {
	/*
	 * Hmmm, search_grpmrtlink() didn't find the entry, but
	 * search_srcmrtlink() did find it! Shoudn't happen. Panic!
	 */
	logit(LOG_ERR, 0, "MRT inconsistency for src %s and grp %s\n",
	      inet_fmt(source, s1), inet_fmt(group, s2));
	/* not reached but to make lint happy */
	return NULL;
    }

    /*
     * Create and insert in group mrtlink and source mrtlink chains.
     */
    r_new = alloc_mrtentry(srcentry_ptr, grpentry_ptr);
    if (!r_new)
	return NULL;

    /*
     * r_new has to be insert right after r_grp_insert in the
     * grp mrtlink chain and right after r_src_insert in the 
     * src mrtlink chain
     */
    insert_grpmrtlink(r_new, r_grp_insert, grpentry_ptr);
    insert_srcmrtlink(r_new, r_src_insert, srcentry_ptr);
    r_new->flags |= MRTF_SG;

    return r_new;
}
