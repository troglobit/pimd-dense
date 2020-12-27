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
 *  $Id: config.c,v 1.8 1998/12/22 21:50:16 kurtw Exp $
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

#include <ifaddrs.h>

#include "defs.h"
#include "queue.h"

struct iflist {
    struct uvif         ifl_uv;
    TAILQ_ENTRY(iflist) ifl_link;
};

static TAILQ_HEAD(ifi_head, iflist) ifl_kern = TAILQ_HEAD_INITIALIZER(ifl_kern);


void config_set_ifflag(uint32_t flag)
{
    struct iflist *ifl;

    TAILQ_FOREACH(ifl, &ifl_kern, ifl_link)
	ifl->ifl_uv.uv_flags |= flag;
}

struct uvif *config_find_ifname(char *nm)
{
    struct iflist *ifl;

    if (!nm) {
	errno = EINVAL;
	return NULL;
    }

    TAILQ_FOREACH(ifl, &ifl_kern, ifl_link) {
	struct uvif *uv = &ifl->ifl_uv;

        if (!strcmp(uv->uv_name, nm))
            return uv;
    }

    return NULL;
}

struct uvif *config_find_ifaddr(in_addr_t addr)
{
    struct iflist *ifl;

    TAILQ_FOREACH(ifl, &ifl_kern, ifl_link) {
	struct uvif *uv = &ifl->ifl_uv;

	if (addr == uv->uv_lcl_addr)
            return uv;
    }

    return NULL;
}

void config_vifs_correlate(void)
{
    struct iflist *ifl, *tmp;
    vifi_t vifi = 0;

    TAILQ_FOREACH(ifl, &ifl_kern, ifl_link) {
	struct uvif *uv = &ifl->ifl_uv;
	struct uvif *v;

	if (uv->uv_flags & VIFF_DISABLED) {
	    logit(LOG_DEBUG, 0, "skipping %s, interface disabled.", uv->uv_name);
	    continue;
	}

	/*
	 * Ignore any interface that is connected to the same subnet as
	 * one already installed in the uvifs array.
	 */
	for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	    if ((uv->uv_lcl_addr & v->uv_subnetmask) == v->uv_subnet ||
		(v->uv_subnet & uv->uv_subnetmask) == uv->uv_subnet) {
		logit(LOG_WARNING, 0, "ignoring %s, same subnet as %s.",
		      uv->uv_name, v->uv_name);
		break;
	    }

	    /*
	     * Same interface, but cannot have multiple VIFs on same
	     * interface so add as secondary IP address to RPF
	     */
	    if (strcmp(v->uv_name, uv->uv_name) == 0) {
		struct phaddr *ph;

		ph = calloc(1, sizeof(*ph));
		if (!ph) {
		    logit(LOG_ERR, errno, "failed allocating altnet on %s", v->uv_name);
		    break;
		}

		logit(LOG_INFO, 0, "Adding subnet %s as an altnet on %s",
		      netname(uv->uv_subnet, uv->uv_subnetmask), v->uv_name);

		ph->pa_subnet       = uv->uv_subnet;
		ph->pa_subnetmask  = uv->uv_subnetmask;
		ph->pa_subnetbcast = uv->uv_subnetbcast;

		ph->pa_next = v->uv_addrs;
		v->uv_addrs = ph;
		break;
	    }
	}

	if (vifi != numvifs)
	    continue;

	/*
	 * If there is room in the uvifs array, install this interface.
	 */
	if (numvifs == MAXVIFS) {
	    logit(LOG_WARNING, 0, "too many vifs, ignoring %s", uv->uv_name);
	    continue;
	}

	uvifs[numvifs++] = *uv;
    }

    /*
     * XXX: one future extension may be to keep this for adding/removing
     *      dynamic interfaces at runtime.  Then it should probably only
     *      be freed on SIGHUP/exit().  Now we free it and let SIGHUP
     *      rebuild it to recheck since we tear down all vifs anyway.
     */
    TAILQ_FOREACH_SAFE(ifl, &ifl_kern, ifl_link, tmp)
	free(ifl);
    TAILQ_INIT(&ifl_kern);
}

/*
 * Query the kernel to find network interfaces that are multicast-capable
 * and install them in the uvifs array.
 */
void 
config_vifs_from_kernel(void)
{
    in_addr_t addr, mask, subnet;
    struct ifaddrs *ifa, *ifap;
    struct iflist *ifl;
    struct uvif *v;
    int n;
    short flags;

    total_interfaces = 0; /* The total number of physical interfaces */
    
    if (getifaddrs(&ifap) < 0)
	logit(LOG_ERR, errno, "getifaddrs");

    /*
     * Loop through all of the interfaces.
     */
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
	/*
	 * Ignore any interface for an address family other than IP.
	 */
	if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
	    continue;
	
	/*
	 * Ignore loopback interfaces and interfaces that do not support
	 * multicast.
	 */
	flags = ifa->ifa_flags;
	if ((flags & (IFF_LOOPBACK|IFF_MULTICAST)) != IFF_MULTICAST)
	    continue;

	/*
	 * Everyone below is a potential vif interface.
	 * We don't care if it has wrong configuration or not configured
	 * at all.
	 */	  
	total_interfaces++;

	/*
	 * Perform some sanity checks on the address and subnet, ignore any
	 * interface whose address and netmask do not define a valid subnet.
	 */
	addr = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
	mask = ((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr.s_addr;
	subnet = addr & mask;
	if (!inet_valid_subnet(subnet, mask) || (addr != subnet && addr == (subnet & ~mask))) {
	    logit(LOG_WARNING, 0, "ignoring %s, has invalid address (%s) and/or mask (%s)",
		  ifa->ifa_name, inet_fmt(addr, s1), inet_fmt(mask, s2));
	    continue;
	}

	logit(LOG_DEBUG, 0, "Found %s, address %s mask %s ...",
	      ifa->ifa_name, inet_fmt(addr, s1), inet_fmt(mask, s2));

	ifl = calloc(1, sizeof(struct iflist));
        if (!ifl) {
            logit(LOG_ERR, errno, "failed allocating memory for iflist");
            return;
        }

	v = &ifl->ifl_uv;
	v->uv_flags		= 0;
	v->uv_metric		= DEFAULT_METRIC;
	v->uv_admetric		= 0;
	v->uv_threshold		= DEFAULT_THRESHOLD;
	v->uv_rate_limit	= DEFAULT_PHY_RATE_LIMIT;
	v->uv_lcl_addr		= addr;
	v->uv_rmt_addr		= INADDR_ANY_N;
	v->uv_dst_addr		= allpimrouters_group;
	v->uv_subnet		= subnet;
	v->uv_subnetmask	= mask;
	v->uv_subnetbcast	= subnet | ~mask;
	strlcpy(v->uv_name, ifa->ifa_name, sizeof(v->uv_name));
	v->uv_groups		= NULL;
	v->uv_dvmrp_neighbors   = NULL;
	NBRM_CLRALL(v->uv_nbrmap);
	v->uv_querier           = NULL;
	v->uv_igmpv1_warn       = 0;
	v->uv_prune_lifetime    = 0;
	v->uv_acl               = NULL;
	RESET_TIMER(v->uv_leaf_timer);
	v->uv_addrs		= NULL;
	v->uv_filter		= NULL;
	RESET_TIMER(v->uv_pim_hello_timer);
	RESET_TIMER(v->uv_gq_timer);
	v->uv_pim_neighbors	= NULL;
	v->uv_local_pref        = default_route_distance;
	v->uv_local_metric      = default_route_metric;
	
#ifdef __linux__
	/* On Linux we can enumerate using ifindex, no need for an IP address */
	v->uv_ifindex = if_nametoindex(v->uv_name);
	if (!v->uv_ifindex)
	    logit(LOG_ERR, errno, "failed reading interface index for %s", v->uv_name);
#endif

	if (flags & IFF_POINTOPOINT) {
	    v->uv_flags |= (VIFF_REXMIT_PRUNES | VIFF_POINT_TO_POINT);
	    v->uv_rmt_addr = ((struct sockaddr_in *)(ifa->ifa_dstaddr))->sin_addr.s_addr;
	} else if (mask == htonl(0xfffffffe)) {
	    /*
	     * Handle RFC 3021 /31 netmasks as point-to-point links
	     */
	    v->uv_flags |= (VIFF_REXMIT_PRUNES | VIFF_POINT_TO_POINT);
	    if (addr == subnet)
		v->uv_rmt_addr = addr + htonl(1);
	    else
		v->uv_rmt_addr = subnet;
	}

	/*
	 * If the interface is not yet up, set the vifs_down flag to
	 * remind us to check again later.
	 */
	if (!(flags & IFF_UP)) {
	    v->uv_flags |= VIFF_DOWN;
	    vifs_down = TRUE;
	}

	TAILQ_INSERT_TAIL(&ifl_kern, ifl, ifl_link);
    }

    freeifaddrs(ifap);
}

/**
 * Local Variables:
 *  c-file-style: "cc-mode"
 * End:
 */
