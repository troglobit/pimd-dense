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
 *  $Id: igmp_proto.c,v 1.4 1998/05/29 21:58:22 kurtw Exp $
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


typedef struct {
    vifi_t  vifi;
    struct listaddr *g;
    int    q_time;
} cbk_t;


/*
 * Forward declarations.
 */
static void DelVif __P((void *arg));
static int SetTimer __P((int vifi, struct listaddr *g));
static int DeleteTimer __P((int id));
static void SendQuery __P((void *arg));
static int SetQueryTimer __P((struct listaddr *g, vifi_t vifi, int to_expire,
			      int q_time));


/*
 * Send group membership queries on that interface if I am querier.
 */
void
query_groups(v)
    register struct uvif *v;
{
    register struct listaddr *g;
    
    v->uv_gq_timer = IGMP_QUERY_INTERVAL;
    if (v->uv_flags & VIFF_QUERIER)
	send_igmp(igmp_send_buf, v->uv_lcl_addr, allhosts_group,
		  IGMP_MEMBERSHIP_QUERY, 
		  (v->uv_flags & VIFF_IGMPV1) ? 0 :
		  IGMP_MAX_HOST_REPORT_DELAY * IGMP_TIMER_SCALE, 0, 0);
    /*
     * Decrement the old-hosts-present timer for each
     * active group on that vif.
     */
    for (g = v->uv_groups; g != NULL; g = g->al_next)
	if (g->al_old > TIMER_INTERVAL)
	    g->al_old -= TIMER_INTERVAL;
	else
	    g->al_old = 0;
}


/*
 * Process an incoming host membership query
 */
void
accept_membership_query(src, dst, group, tmo)
    u_int32 src, dst, group;
    int  tmo;
{
    register vifi_t vifi;
    register struct uvif *v;
    
    /* Ignore my own membership query */
    if (local_address(src) != NO_VIF)
	return;

    /* TODO: modify for DVMRP?? */
    if ((vifi = find_vif_direct(src)) == NO_VIF) {
	IF_DEBUG(DEBUG_IGMP)
	    log(LOG_INFO, 0,
		"ignoring group membership query from non-adjacent host %s",
		inet_fmt(src, s1));
	return;
    }
    
    v = &uvifs[vifi];
    
    if ((tmo == 0 && !(v->uv_flags & VIFF_IGMPV1)) ||
        (tmo != 0 &&  (v->uv_flags & VIFF_IGMPV1))) {
        int i;
	
        /*
         * Exponentially back-off warning rate
         */
        i = ++v->uv_igmpv1_warn;
        while (i && !(i & 1))
            i >>= 1;
        if (i == 1)
            log(LOG_WARNING, 0, "%s %s on vif %d, %s",
                tmo == 0 ? "Received IGMPv1 report from"
                         : "Received IGMPv2 report from",
                inet_fmt(src, s1),
                vifi,
                tmo == 0 ? "please configure vif for IGMPv1"
                         : "but I am configured for IGMPv1");
    }
    
    if (v->uv_querier == NULL || v->uv_querier->al_addr != src) {
        /*
         * This might be:
         * - A query from a new querier, with a lower source address
         *   than the current querier (who might be me)
         * - A query from a new router that just started up and doesn't
         *   know who the querier is.
         * - A query from the current querier
         */
        if (ntohl(src) < (v->uv_querier ? ntohl(v->uv_querier->al_addr)
                                   : ntohl(v->uv_lcl_addr))) {
            IF_DEBUG(DEBUG_IGMP)
		log(LOG_DEBUG, 0, "new querier %s (was %s) on vif %d",
		    inet_fmt(src, s1),
		    v->uv_querier ?
		    inet_fmt(v->uv_querier->al_addr, s2) :
		    "me", vifi);
            if (!v->uv_querier) {
                v->uv_querier = (struct listaddr *)
		    malloc(sizeof(struct listaddr));
		v->uv_querier->al_next = (struct listaddr *)NULL;
		v->uv_querier->al_timer = 0;
		v->uv_querier->al_genid = 0;
		/* TODO: write the protocol version */
		v->uv_querier->al_pv = 0;
		v->uv_querier->al_mv = 0;
		v->uv_querier->al_old = 0;
		v->uv_querier->al_index = 0;
		v->uv_querier->al_timerid = 0;
		v->uv_querier->al_query = 0;
		v->uv_querier->al_flags = 0;
		
                v->uv_flags &= ~VIFF_QUERIER;
            }
            v->uv_querier->al_addr = src;
            time(&v->uv_querier->al_ctime);
        }
    }
    
    /*
     * Reset the timer since we've received a query.
     */
    if (v->uv_querier && src == v->uv_querier->al_addr)
        v->uv_querier->al_timer = 0;
    
    /*
     * If this is a Group-Specific query which we did not source,
     * we must set our membership timer to [Last Member Query Count] *
     * the [Max Response Time] in the packet.
     */
    if (!(v->uv_flags & VIFF_IGMPV1) && group != 0 &&
	src != v->uv_lcl_addr) {
        register struct listaddr *g;
	
        IF_DEBUG(DEBUG_IGMP)
	    log(LOG_DEBUG, 0,
		"%s for %s from %s on vif %d, timer %d",
		"Group-specific membership query",
		inet_fmt(group, s2), inet_fmt(src, s1), vifi, tmo);
        
        for (g = v->uv_groups; g != NULL; g = g->al_next) {
            if (group == g->al_addr && g->al_query == 0) {
                /* setup a timeout to remove the group membership */
                if (g->al_timerid)
                    g->al_timerid = DeleteTimer(g->al_timerid);
                g->al_timer = IGMP_LAST_MEMBER_QUERY_COUNT *
		                        tmo / IGMP_TIMER_SCALE;
                /* use al_query to record our presence in last-member state */
                g->al_query = -1;
                g->al_timerid = SetTimer(vifi, g);
                IF_DEBUG(DEBUG_IGMP)
		    log(LOG_DEBUG, 0,
			"timer for grp %s on vif %d set to %d",
			inet_fmt(group, s2), vifi, g->al_timer);
                break;
            }
        }
    }
}


/*
 * Process an incoming group membership report.
 */
void
accept_group_report(src, dst, group, igmp_report_type)
    u_int32 src, dst, group;
    int  igmp_report_type;
{
    register vifi_t vifi;
    register struct uvif *v;
    register struct listaddr *g;

    if ((vifi = find_vif_direct_local(src)) == NO_VIF) {
	IF_DEBUG(DEBUG_IGMP)
	    log(LOG_INFO, 0,
		"ignoring group membership report from non-adjacent host %s",
		inet_fmt(src, s1));
	return;
    }
    
    IF_DEBUG(DEBUG_IGMP)
	log(LOG_INFO, 0,
	    "accepting IGMP group membership report: src %s, dst% s, grp %s",
	    inet_fmt(src, s1), inet_fmt(dst, s2), inet_fmt(group, s3));
    
    v = &uvifs[vifi];
    
    /*
     * Look for the group in our group list; if found, reset its timer.
     */
    for (g = v->uv_groups; g != NULL; g = g->al_next) {
        if (group == g->al_addr) {
            if (igmp_report_type == IGMP_V1_MEMBERSHIP_REPORT)
                g->al_old = DVMRP_OLD_AGE_THRESHOLD;
	    
            g->al_reporter = src;
	    
            /** delete old timers, set a timer for expiration **/
            g->al_timer = IGMP_GROUP_MEMBERSHIP_INTERVAL;
            if (g->al_query)
                g->al_query = DeleteTimer(g->al_query);
            if (g->al_timerid)
                g->al_timerid = DeleteTimer(g->al_timerid);
            g->al_timerid = SetTimer(vifi, g);
	    add_leaf(vifi, INADDR_ANY_N, group);
            break;
        }
    }
    
    /*
     * If not found, add it to the list and update kernel cache.
     */
    if (g == NULL) {
	g = (struct listaddr *)malloc(sizeof(struct listaddr));
        if (g == NULL)
            log(LOG_ERR, 0, "ran out of memory");    /* fatal */
	
        g->al_addr   = group;
        if (igmp_report_type == IGMP_V1_MEMBERSHIP_REPORT)
            g->al_old = DVMRP_OLD_AGE_THRESHOLD;
        else
            g->al_old = 0;
	
        /** set a timer for expiration **/
        g->al_query     = 0;
        g->al_timer     = IGMP_GROUP_MEMBERSHIP_INTERVAL;
        g->al_reporter  = src;
        g->al_timerid   = SetTimer(vifi, g);
        g->al_next      = v->uv_groups;
        v->uv_groups    = g;
        time(&g->al_ctime);

	add_leaf(vifi, INADDR_ANY_N, group);
    }
}


/* TODO: send PIM prune message if the last member? */
void
accept_leave_message(src, dst, group)
    u_int32 src, dst, group;
{
    register vifi_t vifi;
    register struct uvif *v;
    register struct listaddr *g;

    /* TODO: modify for DVMRP ??? */    
    if ((vifi = find_vif_direct_local(src)) == NO_VIF) {
	IF_DEBUG(DEBUG_IGMP)
            log(LOG_INFO, 0,
                "ignoring group leave report from non-adjacent host %s",
                inet_fmt(src, s1));
        return;
    }
    
    IF_DEBUG(DEBUG_IGMP)
	log(LOG_INFO, 0,
	    "accepting IGMP leave message: src %s, dst% s, grp %s",
	    inet_fmt(src, s1), inet_fmt(dst, s2), inet_fmt(group, s3));

    v = &uvifs[vifi];
    
    if (!(v->uv_flags & (VIFF_QUERIER | VIFF_DR))
	|| (v->uv_flags & VIFF_IGMPV1))
        return;
    
    /*
     * Look for the group in our group list in order to set up a short-timeout
     * query.
     */
    for (g = v->uv_groups; g != NULL; g = g->al_next) {
        if (group == g->al_addr) {
            IF_DEBUG(DEBUG_IGMP)
		log(LOG_DEBUG, 0,
		    "[vif.c, _accept_leave_message] %d %d \n",
		    g->al_old, g->al_query);
	    
            /* Ignore the leave message if there are old hosts present */
            if (g->al_old)
                return;
	    
            /* still waiting for a reply to a query, ignore the leave */
            if (g->al_query)
                return;
	    
            /** delete old timer set a timer for expiration **/
            if (g->al_timerid)
                g->al_timerid = DeleteTimer(g->al_timerid);
	    
#if IGMP_LAST_MEMBER_QUERY_COUNT != 2
/*
This code needs to be updated to keep a counter of the number
of queries remaining.
*/
#endif
	    /** send a group specific querry **/
	    g->al_timer = IGMP_LAST_MEMBER_QUERY_INTERVAL *
		(IGMP_LAST_MEMBER_QUERY_COUNT + 1);
	    if (v->uv_flags & VIFF_QUERIER)
		send_igmp(igmp_send_buf, v->uv_lcl_addr, g->al_addr,
			  IGMP_MEMBERSHIP_QUERY, 
			  IGMP_LAST_MEMBER_QUERY_INTERVAL * IGMP_TIMER_SCALE,
			  g->al_addr, 0);
	    g->al_query = SetQueryTimer(g, vifi,
					IGMP_LAST_MEMBER_QUERY_INTERVAL,
					IGMP_LAST_MEMBER_QUERY_INTERVAL * IGMP_TIMER_SCALE);
	    g->al_timerid = SetTimer(vifi, g);
            break;
        }
    }
}


/*
 * Time out record of a group membership on a vif
 */
static void
DelVif(arg)
    void *arg;
{
    cbk_t *cbk = (cbk_t *)arg;
    vifi_t vifi = cbk->vifi;
    struct uvif *v = &uvifs[vifi];
    struct listaddr *a, **anp, *g = cbk->g;

    /*
     * Group has expired
     * delete all kernel cache entries with this group
     */
    if (g->al_query)
        DeleteTimer(g->al_query);

    delete_leaf(vifi, INADDR_ANY_N, g->al_addr);

    anp = &(v->uv_groups);
    while ((a = *anp) != NULL) {
        if (a == g) {
            *anp = a->al_next;
            free((char *)a);
        } else {
            anp = &a->al_next;
        }
    }

    free(cbk);
}


/*
 * Set a timer to delete the record of a group membership on a vif.
 */
static int
SetTimer(vifi, g)
    vifi_t vifi;
    struct listaddr *g;
{
    cbk_t *cbk;
    
    cbk = (cbk_t *) malloc(sizeof(cbk_t));
    cbk->vifi = vifi;
    cbk->g = g;
    return timer_setTimer(g->al_timer, DelVif, cbk);
}


/*
 * Delete a timer that was set above.
 */
static int
DeleteTimer(id)
    int id;
{
    timer_clearTimer(id);
    return 0;
}


/*
 * Send a group-specific query.
 */
static void
SendQuery(arg)
    void *arg;
{
    cbk_t *cbk = (cbk_t *)arg;
    register struct uvif *v = &uvifs[cbk->vifi];

    if (v->uv_flags & VIFF_QUERIER)
	send_igmp(igmp_send_buf, v->uv_lcl_addr, cbk->g->al_addr,
		  IGMP_MEMBERSHIP_QUERY,
		  cbk->q_time, cbk->g->al_addr, 0);
    cbk->g->al_query = 0;
    free(cbk);
}


/*
 * Set a timer to send a group-specific query.
 */
static int
SetQueryTimer(g, vifi, to_expire, q_time)
    struct listaddr *g;
    vifi_t vifi;
    int to_expire;
    int q_time;
{
    cbk_t *cbk;

    cbk = (cbk_t *) malloc(sizeof(cbk_t));
    cbk->g = g;
    cbk->q_time = q_time;
    cbk->vifi = vifi;
    return timer_setTimer(to_expire, SendQuery, cbk);
}

/* Checks for IGMP group membership: returns TRUE if there is a receiver for the
 * group on the given vif, or returns FALSE otherwise.
 */
int check_grp_membership(v, group)
    struct uvif *v;
    u_int32 group;
{
    register struct listaddr *g;

    /*
     * Look for the group in our group list;
     */
    for (g = v->uv_groups; g != NULL; g = g->al_next) {
        if (group == g->al_addr) 
        	return TRUE;
    }
	return FALSE;
}
