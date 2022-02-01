/*
 * Copyright (c) 1998-2001
 * University of Southern California/Information Sciences Institute.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 *  $Id: igmp_proto.c,v 1.15 2001/09/10 20:31:36 pavlin Exp $
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


typedef struct {
    vifi_t  vifi;
    struct listaddr *g;
    int    q_time;
    int    q_len;
} cbk_t;


/*
 * Forward declarations.
 */
static void DelVif        (void *arg);
static int  SetVerTimer   (vifi_t vifi, struct listaddr *g);
static int  SetTimer      (int vifi, struct listaddr *g);
static int  DeleteTimer   (int id);
static void SendQuery     (void *arg);
static int  SetQueryTimer (struct listaddr *g, vifi_t vifi, int to_expire, int q_time, int q_len);


/*
 * Send group membership queries on that interface if I am querier.
 */
void
query_groups(v)
    struct uvif *v;
{
    struct listaddr *g;
    
    v->uv_gq_timer = IGMP_QUERY_INTERVAL;
    if (v->uv_flags & VIFF_QUERIER) {
	int datalen;
	int code;

	if (v->uv_stquery_cnt)
	    v->uv_stquery_cnt--;
	if (v->uv_stquery_cnt)
	    v->uv_gq_timer = IGMP_STARTUP_QUERY_INTERVAL;
	else
	    v->uv_gq_timer = IGMP_QUERY_INTERVAL;

	/* IGMPv2 and v3 */
	code = IGMP_MAX_HOST_REPORT_DELAY * IGMP_TIMER_SCALE;

	/*
	 * IGMP version to use depends on the compatibility mode of the
	 * interface, which may channge at runtime depending on version
	 * of the end devices on a segment.
	 */
	if (v->uv_flags & VIFF_IGMPV1) {
	    /*
	     * RFC 3376: When in IGMPv1 mode, routers MUST send Periodic
	     *           Queries with a Max Response Time of 0
	     */
	    datalen = 0;
	    code = 0;
	} else if (v->uv_flags & VIFF_IGMPV2) {
	    /*
	     * RFC 3376: When in IGMPv2 mode, routers MUST send Periodic
	     *           Queries truncated at the Group Address field
	     *           (i.e., 8 bytes long)
	     */
	    datalen = 0;
	} else {
	    datalen = 4;
	}

	IF_DEBUG(DEBUG_IGMP)
	    logit(LOG_DEBUG, 0, "Sending IGMP v%s query on %s",
		  datalen == 4 ? "3" : "2", v->uv_name);

	send_igmp(igmp_send_buf, v->uv_lcl_addr, allhosts_group,
		  IGMP_MEMBERSHIP_QUERY, code, 0, datalen);
    }

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
accept_membership_query(src, dst, group, tmo, ver)
    u_int32 src, dst, group;
    int     tmo, ver;
{
    vifi_t vifi;
    struct uvif *v;
    
    /* Ignore my own membership query */
    if (local_address(src) != NO_VIF)
	return;

    /* TODO: modify for DVMRP?? */
    if ((vifi = find_vif_direct(src)) == NO_VIF) {
	IF_DEBUG(DEBUG_IGMP)
	    logit(LOG_INFO, 0,
		"ignoring group membership query from non-adjacent host %s",
		inet_fmt(src, s1));
	return;
    }
    
    v = &uvifs[vifi];
    
    /* Do not accept messages of higher version than current
     * compatibility mode as specified in RFC 3376 - 7.3.1
     */
    if (v->uv_querier) {
	if ((ver == 3 && (v->uv_flags & VIFF_IGMPV2)) ||
	    (ver == 2 && (v->uv_flags & VIFF_IGMPV1))) {
	    int i;
	
	    /*
	     * Exponentially back-off warning rate
	     */
	    i = ++v->uv_igmpv1_warn;
	    while (i && !(i & 1))
		i >>= 1;
	    if (i == 1)
		logit(LOG_WARNING, 0, "Received IGMP v%d query from %s on %s,"
		      " but interface is in IGMP v%d compat mode",
		      ver, inet_fmt(src, s1), v->uv_name,
		      (v->uv_flags & VIFF_IGMPV1) ? 1 : 2);
	    return;
	}
    }
    
    if (v->uv_querier == NULL || v->uv_querier->al_addr != src) {
	uint32_t cur = v->uv_querier ? v->uv_querier->al_addr : v->uv_lcl_addr;

	/*
	 * This might be:
	 * - A query from a new querier, with a lower source address
	 *   than the current querier (who might be me)
	 * - A query from a new router that just started up and doesn't
	 *   know who the querier is.
	 * - A proxy query (source address 0.0.0.0), never wins elections
	 */
	if (!ntohl(src)) {
	    logit(LOG_DEBUG, 0, "Ignoring proxy query on %s", v->uv_name);
	    return;
	}

	if (ntohl(src) < ntohl(cur)) {
            IF_DEBUG(DEBUG_IGMP)
		logit(LOG_DEBUG, 0, "new querier %s (was %s) on %s", inet_fmt(src, s1),
		    v->uv_querier ? inet_fmt(v->uv_querier->al_addr, s2) : "me", v->uv_name);
            if (!v->uv_querier) {
                v->uv_querier = calloc(1, sizeof(struct listaddr));
		if (!v->uv_querier)
		    logit(LOG_ERR, 0, "%s(): out of memory", __func__);

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
    if (!(v->uv_flags & VIFF_IGMPV1) && group != 0 && src != v->uv_lcl_addr) {
        struct listaddr *g;
	
        IF_DEBUG(DEBUG_IGMP)
	    logit(LOG_DEBUG, 0, "%s for %s from %s on vif %d, timer %d",
		  "Group-specific membership query",
		  inet_fmt(group, s2), inet_fmt(src, s1), vifi, tmo);
        
        for (g = v->uv_groups; g != NULL; g = g->al_next) {
            if (group == g->al_addr && g->al_query == 0) {
                /* setup a timeout to remove the group membership */
                if (g->al_timerid)
                    g->al_timerid = DeleteTimer(g->al_timerid);
                g->al_timer = IGMP_LAST_MEMBER_QUERY_COUNT * tmo / IGMP_TIMER_SCALE;
                /* use al_query to record our presence in last-member state */
                g->al_query = -1;
                g->al_timerid = SetTimer(vifi, g);
                IF_DEBUG(DEBUG_IGMP)
		    logit(LOG_DEBUG, 0, "timer for grp %s on vif %d set to %lu",
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
    vifi_t vifi;
    struct uvif *v;
    struct listaddr *g;

    inet_fmt(src, s1);
    inet_fmt(dst, s2);
    inet_fmt(group, s3);

    /* Do not filter LAN scoped groups */
    if (ntohl(group) <= INADDR_MAX_LOCAL_GROUP) { /* group <= 224.0.0.255? */
	IF_DEBUG(DEBUG_IGMP)
	    logit(LOG_DEBUG, 0, "    %-16s LAN scoped group, skipping.", s3);
	return;
    }

    if ((vifi = find_vif_direct_local(src)) == NO_VIF) {
	IF_DEBUG(DEBUG_IGMP)
	    logit(LOG_INFO, 0, "ignoring group membership report from non-adjacent host %s", s1);
	return;
    }
    
    IF_DEBUG(DEBUG_IGMP)
	logit(LOG_INFO, 0, "accepting IGMP group membership report: src %s, dst %s, grp %s", s1, s2, s3);
    
    v = &uvifs[vifi];
    
    /*
     * Look for the group in our group list; if found, reset its timer.
     */
    for (g = v->uv_groups; g != NULL; g = g->al_next) {
        if (group == g->al_addr) {
	    int old_report = 0;

            if (igmp_report_type == IGMP_V1_MEMBERSHIP_REPORT) {
                g->al_old = DVMRP_OLD_AGE_THRESHOLD;
		old_report = 1;

		if (g->al_pv > 1) {
		    IF_DEBUG(DEBUG_IGMP)
			logit(LOG_DEBUG, 0, "Change IGMP compatibility mode to v1 for group %s", s3);
		    g->al_pv = 1;
		}
	    } else if (igmp_report_type == IGMP_V2_MEMBERSHIP_REPORT) {
		old_report = 1;

		if (g->al_pv > 2) {
		    IF_DEBUG(DEBUG_IGMP)
			logit(LOG_DEBUG, 0, "Change IGMP compatibility mode to v2 for group %s", s3);
		    g->al_pv = 2;
		}
	    }

            g->al_reporter = src;
	    
            /** delete old timers, set a timer for expiration **/
            g->al_timer = IGMP_GROUP_MEMBERSHIP_INTERVAL;
            if (g->al_query)
                g->al_query = DeleteTimer(g->al_query);
            if (g->al_timerid)
                g->al_timerid = DeleteTimer(g->al_timerid);
            g->al_timerid = SetTimer(vifi, g);

	    /* Reset timer for switching version back every time an older version report is received */
	    if (g->al_pv < 3 && old_report) {
		if (g->al_versiontimer)
			g->al_versiontimer = DeleteTimer(g->al_versiontimer);

		g->al_versiontimer = SetVerTimer(vifi, g);
	    }

	    add_leaf(vifi, INADDR_ANY_N, group);
            break;
        }
    }
    
    /*
     * If not found, add it to the list and update kernel cache.
     */
    if (!g) {
	g = calloc(1, sizeof(struct listaddr));
        if (!g)
	    logit(LOG_ERR, 0, "%s(): out of memory", __func__);

        g->al_addr   = group;
        if (igmp_report_type == IGMP_V1_MEMBERSHIP_REPORT) {
            g->al_old = DVMRP_OLD_AGE_THRESHOLD;
	    IF_DEBUG(DEBUG_IGMP)
		logit(LOG_DEBUG, 0, "Change IGMP compatibility mode to v1 for group %s", s3);
	    g->al_pv = 1;
	} else if (igmp_report_type == IGMP_V2_MEMBERSHIP_REPORT) {
	    IF_DEBUG(DEBUG_IGMP)
		logit(LOG_DEBUG, 0, "Change IGMP compatibility mode to v2 for group %s", s3);
	    g->al_pv = 2;
	} else {
	    g->al_pv = 3;
	}
	
        /** set a timer for expiration **/
        g->al_query     = 0;
        g->al_timer     = IGMP_GROUP_MEMBERSHIP_INTERVAL;
        g->al_reporter  = src;
        g->al_timerid   = SetTimer(vifi, g);

	/* Set timer for swithing version back if an older version report is received */
	if (g->al_pv < 3)
	    g->al_versiontimer = SetVerTimer(vifi, g);

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
    vifi_t vifi;
    struct uvif *v;
    struct listaddr *g;

    inet_fmt(src, s1);
    inet_fmt(dst, s2);
    inet_fmt(group, s3);

    /* TODO: modify for DVMRP ??? */    
    if ((vifi = find_vif_direct_local(src)) == NO_VIF) {
	IF_DEBUG(DEBUG_IGMP)
            logit(LOG_INFO, 0, "ignoring group leave report from non-adjacent host %s", s1);
        return;
    }
    
    IF_DEBUG(DEBUG_IGMP)
	logit(LOG_INFO, 0, "accepting IGMP leave message: src %s, dst %s, grp %s", s1, s2, s3);

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
	    int datalen;
	    int code;

            IF_DEBUG(DEBUG_IGMP)
		logit(LOG_DEBUG, 0, "%s(): old=%d query=%lu", __func__, g->al_old, g->al_query);
	    
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

	    /* IGMPv2 and v3 */
	    code = IGMP_LAST_MEMBER_QUERY_INTERVAL * IGMP_TIMER_SCALE;

	    /* Use lowest IGMP version */
	    if (v->uv_flags & VIFF_IGMPV2 || g->al_pv <= 2) {
		datalen = 0;
	    } else if (v->uv_flags & VIFF_IGMPV1 || g->al_pv == 1) {
		datalen = 0;
		code = 0;
	    } else {
		datalen = 4;
	    }

	    /** send a group specific querry **/
	    if (v->uv_flags & VIFF_QUERIER) {
		IF_DEBUG(DEBUG_IGMP)
		    logit(LOG_DEBUG, 0, "%s(): Sending IGMP v%s query (al_pv=%d)",
			  __func__, datalen == 4 ? "3" : "2", g->al_pv);

		send_igmp(igmp_send_buf, v->uv_lcl_addr, g->al_addr,
			  IGMP_MEMBERSHIP_QUERY, code, g->al_addr, datalen);
	    }

	    g->al_timer = IGMP_LAST_MEMBER_QUERY_INTERVAL * (IGMP_LAST_MEMBER_QUERY_COUNT + 1);
	    g->al_query = SetQueryTimer(g, vifi, IGMP_LAST_MEMBER_QUERY_INTERVAL, code, datalen);
	    g->al_timerid = SetTimer(vifi, g);
            break;
        }
    }
}


/*
 * Loop through and process all sources in a v3 record.
 *
 * Parameters:
 *     igmp_report_type   Report type of IGMP message
 *     igmp_src           Src address of IGMP message
 *     group              Multicast group
 *     sources            Pointer to the beginning of sources list in the IGMP message
 *     canary             Pointer to the end of IGMP message
 *
 * Returns:
 *     1 if succeeded, 0 if failed
 */
int accept_sources(int igmp_report_type, uint32_t igmp_src, uint32_t group, uint8_t *sources,
    uint8_t *canary, int rec_num_sources)
{
    uint8_t *src;
    int i;

    for (i = 0, src = sources; i < rec_num_sources; ++i, src += 4) {
	struct in_addr *ina = (struct in_addr *)src;

        if ((src + 4) > canary) {
	    IF_DEBUG(DEBUG_IGMP)
		logit(LOG_DEBUG, 0, "Invalid IGMPv3 report, too many sources, would overflow.");
            return 0;
        }

	IF_DEBUG(DEBUG_IGMP)
	    logit(LOG_DEBUG, 0, "Add source (%s,%s)", inet_fmt(ina->s_addr, s2), inet_fmt(group, s1));

        accept_group_report(igmp_src, ina->s_addr, group, igmp_report_type);
    }

    return 1;
}


/*
 * Handle IGMP v3 membership reports (join/leave)
 */
void accept_membership_report(uint32_t src, uint32_t dst, struct igmpv3_report *report, ssize_t reportlen)
{
    uint8_t *canary = (uint8_t *)report + reportlen;
    struct igmpv3_grec *record;
    int num_groups, i;

    num_groups = ntohs(report->ngrec);
    if (num_groups < 0) {
	logit(LOG_INFO, 0, "Invalid Membership Report from %s: num_groups = %d",
	      inet_fmt(src, s1), num_groups);
	return;
    }

    IF_DEBUG(DEBUG_IGMP)
	logit(LOG_DEBUG, 0, "IGMP v3 report, %zd bytes, from %s to %s with %d group records.",
	      reportlen, inet_fmt(src, s1), inet_fmt(dst, s2), num_groups);

    record = &report->grec[0];

    for (i = 0; i < num_groups; i++) {
	struct in_addr  rec_group;
	uint8_t        *sources;
	int             rec_type;
	int             rec_auxdatalen;
	int             rec_num_sources;
	int             j, rc;
	int record_size = 0;

	rec_num_sources = ntohs(record->grec_nsrcs);
	rec_auxdatalen = record->grec_auxwords;
	record_size = sizeof(struct igmpv3_grec) + sizeof(uint32_t) * rec_num_sources + rec_auxdatalen;
	if ((uint8_t *)record + record_size > canary) {
	    logit(LOG_INFO, 0, "Invalid group report %p > %p",
		  (uint8_t *)record + record_size, canary);
	    return;
	}

	rec_type = record->grec_type;
	rec_group.s_addr = (in_addr_t)record->grec_mca;
	sources = (u_int8_t *)record->grec_src;

	switch (rec_type) {
	    case IGMP_MODE_IS_EXCLUDE:
	    case IGMP_CHANGE_TO_EXCLUDE_MODE:
		if (rec_num_sources == 0) {
		    /* RFC 5790: TO_EX({}) can be interpreted as a (*,G)
		     *           join, i.e., to include all sources.
		     */
		    accept_group_report(src, 0, rec_group.s_addr, report->type);
		} else {
		    /* RFC 5790: LW-IGMPv3 does not use TO_EX({x}),
		     *           i.e., filter with non-null source.
		     */
		    logit(LOG_DEBUG, 0, "IS_EX/TO_EX({x}), not unsupported, RFC5790.");
		}
		break;

	    case IGMP_MODE_IS_INCLUDE:
	    case IGMP_CHANGE_TO_INCLUDE_MODE:
		accept_leave_message(src, dst, rec_group.s_addr);
		if (rec_num_sources == 0) {
		    /* RFC5790: TO_IN({}) can be interpreted as an
		     *          IGMPv2 (*,G) leave.
		     */
		    accept_leave_message(src, 0, rec_group.s_addr);
		} else {
		    /* RFC5790: TO_IN({x}), regular RFC3376 (S,G)
		     *          join with >= 1 source, 'S'.
		     */
		    rc = accept_sources(report->type, src, rec_group.s_addr,
					sources, canary, rec_num_sources);
		    if (rc)
			return;
		}
		break;

	    case IGMP_ALLOW_NEW_SOURCES:
		/* RFC5790: Same as TO_IN({x}) */
		if (!accept_sources(report->type, src, rec_group.s_addr,
				    sources, canary, rec_num_sources))
		    return;
		break;

	    case IGMP_BLOCK_OLD_SOURCES:
		/* RFC5790: Instead of TO_EX({x}) */
		for (j = 0; j < rec_num_sources; j++) {
		    uint8_t *gsrc = (uint8_t *)&record->grec_src[j];
		    struct in_addr *ina = (struct in_addr *)gsrc;

		    if (gsrc > canary) {
			logit(LOG_INFO, 0, "Invalid group record");
			return;
		    }

		    IF_DEBUG(DEBUG_IGMP)
			logit(LOG_DEBUG, 0, "Remove source[%d] (%s,%s)", j,
			      inet_fmt(ina->s_addr, s2), inet_ntoa(rec_group));
		    accept_leave_message(src, ina->s_addr, rec_group.s_addr);
		    IF_DEBUG(DEBUG_IGMP)
			logit(LOG_DEBUG, 0, "Accepted");
		}
		break;

	    default:
		/* RFC3376: Unrecognized Record Type values MUST be silently ignored. */
		break;
	}

	record = (struct igmpv3_grec *)((uint8_t *)record + record_size);
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
 * Time out old version compatibility mode
 */
static void SwitchVersion(void *arg)
{
    cbk_t *cbk = (cbk_t *)arg;

    if (cbk->g->al_pv < 3)
	cbk->g->al_pv += 1;

    logit(LOG_INFO, 0, "Switch IGMP compatibility mode back to v%d for group %s",
	  cbk->g->al_pv, inet_fmt(cbk->g->al_addr, s1));

    free(cbk);
}


/*
 * Set a timer to switch version back on a vif.
 */
static int SetVerTimer(vifi_t vifi, struct listaddr *g)
{
    uint32_t timeout;
    cbk_t *cbk;

    cbk = calloc(1, sizeof(cbk_t));
    if (!cbk) {
	logit(LOG_ERR, 0, "Failed calloc() in SetVerTimer()\n");
	return -1;
    }

    timeout = IGMP_ROBUSTNESS_VARIABLE * IGMP_QUERY_INTERVAL + IGMP_QUERY_RESPONSE_INTERVAL;
    cbk->vifi = vifi;
    cbk->g = g;

    return timer_setTimer(timeout, SwitchVersion, cbk);
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
    
    cbk = calloc(1, sizeof(cbk_t));
    if (!cbk) {
	logit(LOG_ERR, 0, "%s(): out of memory", __func__);
	return -1;
    }

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
    struct uvif *v;

    v = &uvifs[cbk->vifi];

    if (v->uv_flags & VIFF_QUERIER) {
	uint32_t group = cbk->g->al_addr;

	IF_DEBUG(DEBUG_IGMP)
	    logit(LOG_DEBUG, 0, "SendQuery: Send IGMP v%s query", cbk->q_len == 4 ? "3" : "2");

	send_igmp(igmp_send_buf, v->uv_lcl_addr, group, IGMP_MEMBERSHIP_QUERY,
		  cbk->q_time, group != allhosts_group ? group : 0, cbk->q_len);
    }

    cbk->g->al_query = 0;
    free(cbk);
}


/*
 * Set a timer to send a group-specific query.
 */
static int
SetQueryTimer(g, vifi, to_expire, q_time, q_len)
    struct listaddr *g;
    vifi_t vifi;
    int to_expire;
    int q_time;
    int q_len;
{
    cbk_t *cbk;

    cbk = calloc(1, sizeof(cbk_t));
    if (!cbk) {
	    logit(LOG_ERR, 0, "%s(): out of memory", __func__);
	    return -1;
    }

    cbk->g = g;
    cbk->q_time = q_time;
    cbk->q_len = q_len;
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
    struct listaddr *g;

    /*
     * Look for the group in our group list;
     */
    for (g = v->uv_groups; g != NULL; g = g->al_next) {
        if (group == g->al_addr) 
        	return TRUE;
    }

    return FALSE;
}

/**
 * Local Variables:
 *  c-file-style: "cc-mode"
 * End:
 */
