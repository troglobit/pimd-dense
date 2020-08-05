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
 *  $Id: debug.c,v 1.9 1998/06/02 19:46:54 kurtw Exp $
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

#define SYSLOG_NAMES
#include "defs.h"


#ifdef __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif

static struct debugname {
    char   *name;
    int	    level;
    size_t  nchars;
} debugnames[] = {
    {   "igmp_proto",	    DEBUG_IGMP_PROTO,     6	    },
    {   "igmp_timers",	    DEBUG_IGMP_TIMER,     6	    },
    {   "igmp_members",	    DEBUG_IGMP_MEMBER,    6	    },
    {   "groups",	    DEBUG_MEMBER,         1	    },
    {   "membership",       DEBUG_MEMBER,         2	    },
    {   "igmp",	            DEBUG_IGMP, 	  1	    },
    {   "trace",	    DEBUG_TRACE,          2	    },
    {   "mtrace",	    DEBUG_TRACE,          2	    },
    {   "traceroute",       DEBUG_TRACE,          2	    },
    {   "timeout",	    DEBUG_TIMEOUT,        2	    },
    {   "callout",	    DEBUG_TIMEOUT,        3	    },
    {   "pkt",	            DEBUG_PKT,  	  2	    },
    {   "packets",	    DEBUG_PKT,  	  2	    },
    {   "interfaces",       DEBUG_IF,   	  2	    },
    {   "vif",	            DEBUG_IF,   	  1	    },
    {   "kernel",           DEBUG_KERN,           2	    },
    {   "cache",            DEBUG_MFC,   	  1	    },
    {   "mfc",              DEBUG_MFC,  	  2	    },
    {   "k_cache",          DEBUG_MFC,  	  2	    },
    {   "k_mfc",            DEBUG_MFC,  	  2	    },
    {   "rsrr",	            DEBUG_RSRR, 	  2	    },
    {   "pim_detail",       DEBUG_PIM_DETAIL,     5	    },
    {   "pim_hello",        DEBUG_PIM_HELLO,      5	    },
    {   "pim_neighbors",    DEBUG_PIM_HELLO,      5	    },
    {   "pim_register",     DEBUG_PIM_REGISTER,   5	    },
    {   "registers",        DEBUG_PIM_REGISTER,   2	    },
    {   "pim_join_prune",   DEBUG_PIM_JOIN_PRUNE, 5	    },
    {   "pim_j_p",          DEBUG_PIM_JOIN_PRUNE, 5	    },
    {   "pim_jp",           DEBUG_PIM_JOIN_PRUNE, 5	    },
    {   "pim_graft",        DEBUG_PIM_GRAFT,      5         },
    {   "pim_bootstrap",    DEBUG_PIM_BOOTSTRAP,  5	    },
    {   "pim_bsr",          DEBUG_PIM_BOOTSTRAP,  5	    },
    {   "bsr",	            DEBUG_PIM_BOOTSTRAP,  1	    },
    {   "bootstrap",        DEBUG_PIM_BOOTSTRAP,  1	    },
    {   "pim_asserts",      DEBUG_PIM_ASSERT,     5	    },
    {   "pim_routes",       DEBUG_PIM_MRT,        6	    },
    {   "pim_routing",      DEBUG_PIM_MRT,        6	    },
    {   "pim_mrt",          DEBUG_PIM_MRT,        5	    },
    {   "pim_timers",       DEBUG_PIM_TIMER,      5	    },
    {   "pim_rpf",          DEBUG_PIM_RPF,        6	    },
    {   "rpf",              DEBUG_RPF,            3	    },
    {   "pim",              DEBUG_PIM,  	  1	    },
    {   "routes",	    DEBUG_MRT,            1	    },
    {   "routing",	    DEBUG_MRT,            1	    },
    {   "mrt",  	    DEBUG_MRT,            1	    },
    {   "routers",          DEBUG_NEIGHBORS,      6	    },
    {   "mrouters",         DEBUG_NEIGHBORS,      7	    },
    {   "neighbors",        DEBUG_NEIGHBORS,      1	    },
    {   "timers",           DEBUG_TIMER,          1	    },
    {   "asserts",          DEBUG_ASSERT,         1	    },
    {   "all",              DEBUG_ALL,            2         },
    {   "3",	            0xffffffff,           1	    }    /* compat. */
};

int log_level  = LOG_NOTICE;
int log_syslog = 1;
int log_nmsgs  = 0;
unsigned long debug = 0x00000000;        /* If (long) is smaller than
					  * 4 bytes, then we are in
					  * trouble.
					  */

char *
packet_kind(proto, type, code)
    u_int proto, type, code;
{
    static char unknown[60];

    switch (proto) {
    case IPPROTO_IGMP:
	switch (type) {
	case IGMP_MEMBERSHIP_QUERY:    return "IGMP Membership Query    ";
	case IGMP_V1_MEMBERSHIP_REPORT:return "IGMP v1 Member Report    ";
	case IGMP_V2_MEMBERSHIP_REPORT:return "IGMP v2 Member Report    ";
	case IGMP_V2_LEAVE_GROUP:      return "IGMP Leave message       ";
	case IGMP_DVMRP:
	    switch (code) {
	    case DVMRP_PROBE:          return "DVMRP Neighbor Probe     ";
	    case DVMRP_REPORT:         return "DVMRP Route Report       ";
	    case DVMRP_ASK_NEIGHBORS:  return "DVMRP Neighbor Request   ";
	    case DVMRP_NEIGHBORS:      return "DVMRP Neighbor List      ";
	    case DVMRP_ASK_NEIGHBORS2: return "DVMRP Neighbor request 2 ";
	    case DVMRP_NEIGHBORS2:     return "DVMRP Neighbor list 2    ";
	    case DVMRP_PRUNE:          return "DVMRP Prune message      ";
	    case DVMRP_GRAFT:          return "DVMRP Graft message      ";
	    case DVMRP_GRAFT_ACK:      return "DVMRP Graft message ack  ";
	    case DVMRP_INFO_REQUEST:   return "DVMRP Info Request       ";
	    case DVMRP_INFO_REPLY:     return "DVMRP Info Reply         ";
	    default:
		sprintf(unknown,   "UNKNOWN DVMRP message code = %3d ", code);
		return unknown;
	    }
	case IGMP_PIM:
	    /* The old style (PIM v1) encapsulation of PIM messages
	     * inside IGMP messages.
	     */
	    /* PIM v1 is not implemented but we just inform that a message
	     *	has arrived.
	     */
	    switch (code) {
	    case PIM_V1_QUERY:	       return "PIM v1 Router-Query      ";
	    case PIM_V1_REGISTER:      return "PIM v1 Register          ";
	    case PIM_V1_REGISTER_STOP: return "PIM v1 Register-Stop     ";
	    case PIM_V1_JOIN_PRUNE:    return "PIM v1 Join/Prune        ";
	    case PIM_V1_RP_REACHABILITY:
		                       return "PIM v1 RP-Reachability   ";
	    case PIM_V1_ASSERT:        return "PIM v1 Assert            ";
	    case PIM_V1_GRAFT:         return "PIM v1 Graft             ";
	    case PIM_V1_GRAFT_ACK:     return "PIM v1 Graft_Ack         ";
	    default:
		sprintf(unknown,   "UNKNOWN PIM v1 message type =%3d ", code);
		return unknown;
	    }
	case IGMP_MTRACE:              return "IGMP trace query         ";
	case IGMP_MTRACE_RESP:         return "IGMP trace reply         ";
	default:
	    sprintf(unknown,
		    "UNKNOWN IGMP message: type = 0x%02x, code = 0x%02x ",
		    type, code);
	    return unknown;
	}
    case IPPROTO_PIM:    /* PIM v2 */
	switch (type) {
	case PIM_V2_HELLO:	       return "PIM v2 Hello             ";
	case PIM_V2_REGISTER:          return "PIM v2 Register          ";
	case PIM_V2_REGISTER_STOP:     return "PIM v2 Register_Stop     ";
	case PIM_V2_JOIN_PRUNE:        return "PIM v2 Join/Prune        ";
	case PIM_V2_BOOTSTRAP:         return "PIM v2 Bootstrap         ";
	case PIM_V2_ASSERT:            return "PIM v2 Assert            ";
	case PIM_V2_GRAFT:             return "PIM-DM v2 Graft          ";
	case PIM_V2_GRAFT_ACK:         return "PIM-DM v2 Graft_Ack      ";
	case PIM_V2_CAND_RP_ADV:       return "PIM v2 Cand. RP Adv.     ";
	default:
	    sprintf(unknown,      "UNKNOWN PIM v2 message type =%3d ", type);
	    return unknown;
	}
    default:
	sprintf(unknown,          "UNKNOWN proto =%3d               ", proto);
	return unknown;
    }
}


/*
 * Used for debugging particular type of messages.
 */
int
debug_kind(proto, type, code)
    u_int proto, type, code;
{
    switch (proto) {
    case IPPROTO_IGMP:
	switch (type) {
	case IGMP_MEMBERSHIP_QUERY:        return DEBUG_IGMP;
	case IGMP_V1_MEMBERSHIP_REPORT:    return DEBUG_IGMP;
	case IGMP_V2_MEMBERSHIP_REPORT:    return DEBUG_IGMP;
	case IGMP_V2_LEAVE_GROUP:          return DEBUG_IGMP;
	case IGMP_DVMRP:
	    switch (code) {
	    case DVMRP_PROBE:              return DEBUG_DVMRP_PEER;
	    case DVMRP_REPORT:             return DEBUG_DVMRP_ROUTE;
	    case DVMRP_ASK_NEIGHBORS:      return 0;
	    case DVMRP_NEIGHBORS:          return 0;
	    case DVMRP_ASK_NEIGHBORS2:     return 0;
	    case DVMRP_NEIGHBORS2:         return 0;
	    case DVMRP_PRUNE:              return DEBUG_DVMRP_PRUNE;
	    case DVMRP_GRAFT:              return DEBUG_DVMRP_PRUNE;
	    case DVMRP_GRAFT_ACK:          return DEBUG_DVMRP_PRUNE;
	    case DVMRP_INFO_REQUEST:       return 0;
	    case DVMRP_INFO_REPLY:         return 0;
	    default:                       return 0;
	    }
	case IGMP_PIM:
	    /* PIM v1 is not implemented */
	    switch (code) {
	    case PIM_V1_QUERY:             return DEBUG_PIM;
	    case PIM_V1_REGISTER:          return DEBUG_PIM;
	    case PIM_V1_REGISTER_STOP:     return DEBUG_PIM;
	    case PIM_V1_JOIN_PRUNE:        return DEBUG_PIM;
	    case PIM_V1_RP_REACHABILITY:   return DEBUG_PIM;
	    case PIM_V1_ASSERT:            return DEBUG_PIM;
	    case PIM_V1_GRAFT:             return DEBUG_PIM;
	    case PIM_V1_GRAFT_ACK:         return DEBUG_PIM;
	    default:                       return DEBUG_PIM;
	    }
	case IGMP_MTRACE:                  return DEBUG_TRACE;
	case IGMP_MTRACE_RESP:             return DEBUG_TRACE;
	default:                           return DEBUG_IGMP;
	}
    case IPPROTO_PIM: 	/* PIM v2 */
	/* TODO: modify? */
	switch (type) {
	case PIM_V2_HELLO:             return DEBUG_PIM;
	case PIM_V2_REGISTER:          return DEBUG_PIM_REGISTER;
	case PIM_V2_REGISTER_STOP:     return DEBUG_PIM_REGISTER;
	case PIM_V2_JOIN_PRUNE:        return DEBUG_PIM;
	case PIM_V2_BOOTSTRAP:         return DEBUG_PIM_BOOTSTRAP;
	case PIM_V2_ASSERT:            return DEBUG_PIM;
	case PIM_V2_GRAFT:             return DEBUG_PIM;
	case PIM_V2_GRAFT_ACK:         return DEBUG_PIM;
	case PIM_V2_CAND_RP_ADV:       return DEBUG_PIM_CAND_RP;
	default:                       return DEBUG_PIM;
	}
    default:                               return 0;
    }
    return 0;
}


int
debug_list(mask, buf, len)
    int mask;
    char *buf;
    size_t len;
{
    struct debugname *d;
    size_t i;

    memset(buf, 0, len);
    for (i = 0, d = debugnames; i < NELEMS(debugnames); i++, d++) {
	if (!(mask & d->level))
	    continue;

	if (mask != (int)DEBUG_ALL)
	    mask &= ~d->level;

	strlcat(buf, d->name, len);

	if (mask && i + 1 < NELEMS(debugnames))
	    strlcat(buf, ", ", len);
    }

    return 0;
}


int
debug_parse(arg)
    char *arg;
{
    struct debugname *d;
    size_t i, len;
    char *next = NULL;
    int sys = 0;

    if (!arg || !strlen(arg) || strstr(arg, "none"))
	return sys;

    while (arg) {
	int no = 0;

	next = strchr(arg, ',');
	if (next)
	    *next++ = '\0';
	/* disable log level if flag preceded by '-' */
	if (arg[0] == '-') {
	    arg++;
	    no = 1;
	}

	len = strlen(arg);
	for (i = 0, d = debugnames; i < NELEMS(debugnames); i++, d++) {
	    if (len >= d->nchars && strncmp(d->name, arg, len) == 0)
		break;
	}

	if (i == NELEMS(debugnames))
	    return DEBUG_PARSE_FAIL;

	if (no)
	    sys &= ~d->level;
	else
	    sys |= d->level;
	arg = next;
    }

    return sys;
}


int
log_str2lvl(level)
    char *level;
{
    int i;

    for (i = 0; prioritynames[i].c_name; i++) {
	size_t len = MIN(strlen(prioritynames[i].c_name), strlen(level));

	if (!strncasecmp(prioritynames[i].c_name, level, len))
	    return prioritynames[i].c_val;
    }

    return atoi(level);
}


const char *
log_lvl2str(val)
    int val;
{
    int i;

    for (i = 0; prioritynames[i].c_name; i++) {
	if (prioritynames[i].c_val == val)
	    return prioritynames[i].c_name;
    }

    return "unknown";
}


int
log_list(buf, len)
    char *buf;
    size_t len;
{
    int i;

    memset(buf, 0, len);
    for (i = 0; prioritynames[i].c_name; i++) {
	if (i > 0)
	    strlcat(buf, ", ", len);
	strlcat(buf, prioritynames[i].c_name, len);
    }

    return 0;
}


/*
 * Some messages are more important than others.  This routine
 * determines the logging level at which to log a send error (often
 * "No route to host").  This is important when there is asymmetric
 * reachability and someone is trying to, i.e., mrinfo me periodically.
 */
int
log_severity(proto, type, code)
    u_int proto, type, code;
{
    switch (proto) {
    case IPPROTO_IGMP:
	switch (type) {
	case IGMP_MTRACE_RESP:
	    return LOG_INFO;
	    
	case IGMP_DVMRP:
	    switch (code) {
	    case DVMRP_NEIGHBORS:
	    case DVMRP_NEIGHBORS2:
		return LOG_INFO;
	    }
	    break;

	case IGMP_PIM:
	    /* PIM v1 */
	    switch (code) {
	    default:
		return LOG_INFO;
	    }

	default:
	    return LOG_WARNING;
	}
	break;
	
    case IPPROTO_PIM:
	/* PIM v2 */
	switch (type) {
	default:
	    return LOG_INFO;
	}
	break;

    default:
	return LOG_WARNING;
    }

    return LOG_WARNING;
}


/*
            1         2         3         4         5         6         7         8
  012345678901234567890123456789012345678901234567890123456789012345678901234567890
  Virtual Interface Table
  Vif  Local-Address    Subnet                Thresh  Flags          Neighbors
  0  10.0.3.1         10.0.3/24             1       DR NO-NBR
  1  172.16.12.254    172.16.12/24          1       DR PIM         172.16.12.2
                                                                   172.16.12.3
  2  192.168.122.147  register_vif0         1
*/
void dump_vifs(FILE *fp, int detail)
{
    vifi_t vifi;
    struct uvif *v;
    pim_nbr_entry_t *n;
    int width;
    int i;

    if (detail)
	fprintf(fp, "\nVirtual Interface Table\n");
    fprintf(fp, "VIF  Local Address    Subnet              Thresh  Flags      Neighbors=\n");

    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	int down = 0;

	fprintf(fp, "%3u  %-15s  ", vifi, inet_fmt(v->uv_lcl_addr, s1));

	if (v->uv_flags & VIFF_REGISTER)
	    fprintf(fp, "%-18s  ", v->uv_name);
	else
	    fprintf(fp,"%-18.18s  ", netname(v->uv_subnet, v->uv_subnetmask));

	fprintf(fp, "%6u ", v->uv_threshold);

	/* TODO: XXX: Print VIFF_TUNNEL? */
	width = 0;
	if (v->uv_flags & VIFF_DISABLED) {
	    fprintf(fp, " DISABLED");
	    down = 1;
	}
	if (v->uv_flags & VIFF_DOWN) {
	    fprintf(fp, " DOWN");
	    down = 1;
	}

	if (v->uv_flags & VIFF_DR) {
	    fprintf(fp, " DR");
	    width += 3;
	}
	if (v->uv_flags & VIFF_PIM_NBR) {
	    fprintf(fp, " PIM");
	    width += 4;
	}
	if (v->uv_flags & VIFF_DVMRP_NBR) {
	    fprintf(fp, " DVMRP");
	    width += 6;
	}
	if (v->uv_flags & VIFF_NONBRS) {
	    fprintf(fp, " NO-NBR");
	    width += 6;
	}

	n = v->uv_pim_neighbors;
	if (!down && n) {
	    for (i = width; i <= 11; i++)
		fprintf(fp, " ");
	    fprintf(fp, "%-15s\n", inet_fmt(n->address, s1));
	    for (n = n->next; n; n = n->next)
		fprintf(fp, "%61s%-15s\n", "", inet_fmt(n->address, s1));
	} else {
	    fprintf(fp, "\n");
	}
    }
}

/*
 * Log errors and other messages to the system log daemon and to stderr,
 * according to the severity of the message and the current debug level.
 * For errors of severity LOG_ERR or worse, terminate the program.
 */
void
logit(int severity, int syserr, char *format, ...)
{
    va_list ap;
    static char fmt[211] = "warning - ";
    char *msg;
    struct timeval now;
    time_t now_sec;
    struct tm *thyme;
    
    va_start(ap, format);
    vsprintf(&fmt[10], format, ap);
    va_end(ap);
    msg = (severity == LOG_WARNING) ? fmt : &fmt[10];
    
    /*
     * Log to stderr if we haven't forked yet and it's a warning or worse,
     * or if we're debugging.
     */
    if (!log_syslog) {
	if (severity > log_level)
	    goto done;

	gettimeofday(&now,NULL);
	now_sec = now.tv_sec;
	thyme = localtime(&now_sec);

	if (!debug)
	    fprintf(stderr, "%s: ", progname);

	fprintf(stderr, "%02d:%02d:%02d.%03ld %s", thyme->tm_hour,
		thyme->tm_min, thyme->tm_sec, now.tv_usec / 1000, msg);
	if (syserr )
	    fprintf(stderr, ": %s", strerror(syserr));

	fprintf(stderr, "\n");
	goto done;
    }
    
    /*
     * Always log things that are worse than warnings, no matter what
     * the log_nmsgs rate limiter says.
     *
     * Only count things worse than debugging in the rate limiter
     * (since if you put daemon.debug in syslog.conf you probably
     * actually want to log the debugging messages so they shouldn't
     * be rate-limited)
     */
    if ((severity < LOG_WARNING) || (log_nmsgs < LOG_MAX_MSGS)) {
	if (severity <= log_level && (severity != LOG_DEBUG))
	    log_nmsgs++;

	if (syserr)
	    syslog(severity, "%s: %s", msg, strerror(syserr));
	else
	    syslog(severity, "%s", msg);
    }

done:
    if (severity <= LOG_ERR)
	    exit(-1);
}

/* TODO: format the output for better readability */
void 
dump_pim_mrt(fp, detail)
    FILE *fp;
    int detail;
{
    grpentry_t *g;
    mrtentry_t *r;
    vifi_t vifi;
    u_int number_of_groups = 0;
    char oifs[(sizeof(vifbitmap_t)<<3)+1];
    char pruned_oifs[(sizeof(vifbitmap_t)<<3)+1];
    char leaves_oifs[(sizeof(vifbitmap_t)<<3)+1];
    char incoming_iif[(sizeof(vifbitmap_t)<<3)+1];
    
    fprintf(fp, "Multicast Routing Table\n%s", 
	    " Source          Group           Flags\n");

    /* TODO: remove the dummy 0.0.0.0 group (first in the chain) */
    for (g = grplist->next; g != (grpentry_t *)NULL; g = g->next) {
	number_of_groups++;
	/* Print all (S,G) routing info */
	for (r = g->mrtlink; r != (mrtentry_t *)NULL; r = r->grpnext) {
	    fprintf(fp, "---------------------------(S,G)----------------------------\n");
	    /* Print the routing info */
	    fprintf(fp, " %-15s", inet_fmt(r->source->address, s1));
	    fprintf(fp, " %-15s", inet_fmt(g->group, s2));

	    for (vifi = 0; vifi < numvifs; vifi++) {
		oifs[vifi] =
		    VIFM_ISSET(vifi, r->oifs)          ? 'o' : '.';
		pruned_oifs[vifi] =
		    VIFM_ISSET(vifi, r->pruned_oifs)   ? 'p' : '.';
		leaves_oifs[vifi] =
		    VIFM_ISSET(vifi, r->leaves)        ? 'l' : '.';
		incoming_iif[vifi] = '.';
	    }
	    oifs[vifi]          = 0x0;  /* End of string */
	    pruned_oifs[vifi]   = 0x0;
	    leaves_oifs[vifi]   = 0x0;
	    incoming_iif[vifi]  = 0x0;
	    incoming_iif[r->incoming] = 'I';

	    /* TODO: don't need some of the flags */
	    if (r->flags & MRTF_SPT)           fprintf(fp, " SPT");
	    if (r->flags & MRTF_WC)            fprintf(fp, " WC");
	    if (r->flags & MRTF_RP)            fprintf(fp, " RP");
	    if (r->flags & MRTF_REGISTER)      fprintf(fp, " REG");
	    if (r->flags & MRTF_IIF_REGISTER)  fprintf(fp, " IIF_REG");
	    if (r->flags & MRTF_NULL_OIF)      fprintf(fp, " NULL_OIF");
	    if (r->flags & MRTF_KERNEL_CACHE)  fprintf(fp, " CACHE");
	    if (r->flags & MRTF_ASSERTED)      fprintf(fp, " ASSERTED");
	    if (r->flags & MRTF_REG_SUPP)      fprintf(fp, " REG_SUPP");
	    if (r->flags & MRTF_SG)            fprintf(fp, " SG");
	    if (r->flags & MRTF_PMBR)          fprintf(fp, " PMBR");
	    fprintf(fp, "\n");
	    
	    fprintf(fp, "Pruned   oifs: %-20s\n", pruned_oifs);
	    fprintf(fp, "Leaves   oifs: %-20s\n", leaves_oifs);
	    fprintf(fp, "Outgoing oifs: %-20s\n", oifs);
	    fprintf(fp, "Incoming     : %-20s\n", incoming_iif);

	    fprintf(fp, "Upstream nbr: %s\n", 
		    r->upstream ? inet_fmt(r->upstream->address, s1) : "NONE");
	    fprintf(fp, "\nTIMERS:  Entry   Prune VIFS:");
	    for (vifi = 0; vifi < numvifs; vifi++)
		fprintf(fp, "  %2d", vifi);
	    fprintf(fp, "\n           %3d              ",
		    r->timer);
	    for (vifi = 0; vifi < numvifs; vifi++)
		fprintf(fp, " %3d", r->prune_timers[vifi]);
	    fprintf(fp, "\n");
	}
    }/* for all groups */

    fprintf(fp, "Number of Groups: %u\n\n", number_of_groups);
}

