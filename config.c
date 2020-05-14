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

#include "defs.h"


/*
 * Forward declarations.
 */
static char *next_word  __P((char **));
static int parse_phyint __P((char *s));


/*
 * Query the kernel to find network interfaces that are multicast-capable
 * and install them in the uvifs array.
 */
void 
config_vifs_from_kernel()
{
    struct ifreq *ifrp, *ifend;
    register struct uvif *v;
    register vifi_t vifi;
    int n;
    u_int32 addr, mask, subnet;
    short flags;
    int num_ifreq = 32;
    struct ifconf ifc;

    total_interfaces = 0; /* The total number of physical interfaces */
    
    ifc.ifc_len = num_ifreq * sizeof(struct ifreq);
    ifc.ifc_buf = calloc(ifc.ifc_len, sizeof(char));
    while (ifc.ifc_buf) {
	if (ioctl(udp_socket, SIOCGIFCONF, (char *)&ifc) < 0)
	    log(LOG_ERR, errno, "ioctl SIOCGIFCONF");
	
	/*
	 * If the buffer was large enough to hold all the addresses
	 * then break out, otherwise increase the buffer size and
	 * try again.
	 *
	 * The only way to know that we definitely had enough space
	 * is to know that there was enough space for at least one
	 * more struct ifreq. ???
	 */
	if ((num_ifreq * sizeof(struct ifreq)) >=
	    ifc.ifc_len + sizeof(struct ifreq))
	    break;
	
	num_ifreq *= 2;
	ifc.ifc_len = num_ifreq * sizeof(struct ifreq);
	ifc.ifc_buf = realloc(ifc.ifc_buf, ifc.ifc_len);
    }
    if (ifc.ifc_buf == NULL)
	log(LOG_ERR, 0, "config_vifs_from_kernel: ran out of memory");
    
    ifrp = (struct ifreq *)ifc.ifc_buf;
    ifend = (struct ifreq *)(ifc.ifc_buf + ifc.ifc_len);
    /*
     * Loop through all of the interfaces.
     */
    for (; ifrp < ifend; ifrp = (struct ifreq *)((char *)ifrp + n)) {
	struct ifreq ifr;
#ifdef HAVE_SA_LEN
	n = ifrp->ifr_addr.sa_len + sizeof(ifrp->ifr_name);
	if (n < sizeof(*ifrp))
	    n = sizeof(*ifrp);
#else
	n = sizeof(*ifrp);
#endif /* HAVE_SA_LEN */
	
	/*
	 * Ignore any interface for an address family other than IP.
	 */
	if (ifrp->ifr_addr.sa_family != AF_INET) {
	    total_interfaces++;  /* Eventually may have IP address later */
	    continue;
	}
	
	addr = ((struct sockaddr_in *)&ifrp->ifr_addr)->sin_addr.s_addr;
	
	/*
	 * Need a template to preserve address info that is
	 * used below to locate the next entry.  (Otherwise,
	 * SIOCGIFFLAGS stomps over it because the requests
	 * are returned in a union.)
	 */
	bcopy(ifrp->ifr_name, ifr.ifr_name, sizeof(ifr.ifr_name));
	
	/*
	 * Ignore loopback interfaces and interfaces that do not
	 * support multicast.
	 */
	if (ioctl(udp_socket, SIOCGIFFLAGS, (char *)&ifr) < 0)
	    log(LOG_ERR, errno, "ioctl SIOCGIFFLAGS for %s", ifr.ifr_name);
	flags = ifr.ifr_flags;
	if ((flags & (IFF_LOOPBACK | IFF_MULTICAST)) != IFF_MULTICAST)
	    continue;
	
	/*
	 * Everyone below is a potential vif interface.
	 * We don't care if it has wrong configuration or not configured
	 * at all.
	 */	  
	total_interfaces++;

	/*
	 * Ignore any interface whose address and mask do not define a
	 * valid subnet number, or whose address is of the form
	 * {subnet,0} or {subnet,-1}.
	 */
	if (ioctl(udp_socket, SIOCGIFNETMASK, (char *)&ifr) < 0)
	    log(LOG_ERR, errno, "ioctl SIOCGIFNETMASK for %s",
		ifr.ifr_name);
	mask = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr;
	subnet = addr & mask;
	if ((!inet_valid_subnet(subnet, mask))
	    || (addr == subnet) || addr == (subnet | ~mask)) {
	    log(LOG_WARNING, 0,
		"ignoring %s, has invalid address (%s) and/or mask (%s)",
		ifr.ifr_name, inet_fmt(addr, s1), inet_fmt(mask, s2));
	    continue;
	}
	
	/*
	 * Ignore any interface that is connected to the same subnet as
	 * one already installed in the uvifs array.
	 */
	/*
	 * TODO: XXX: bug or "feature" is to allow only one interface per
	 * subnet?
	 */
	
	for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	    if (strcmp(v->uv_name, ifr.ifr_name) == 0) {
		log(LOG_DEBUG, 0,
		    "skipping %s (%s on subnet %s) (alias for vif#%u?)",
		    v->uv_name, inet_fmt(addr, s1),
		    netname(subnet, mask), vifi);
		break;
	    }
	    if ((addr & v->uv_subnetmask) == v->uv_subnet ||
		(v->uv_subnet & mask) == subnet) {
		log(LOG_WARNING, 0, "ignoring %s, same subnet as %s",
		    ifr.ifr_name, v->uv_name);
		break;
	    }
	}
	if (vifi != numvifs)
	    continue;
	
	/*
	 * If there is room in the uvifs array, install this interface.
	 */
	if (numvifs == MAXVIFS) {
	    log(LOG_WARNING, 0, "too many vifs, ignoring %s", ifr.ifr_name);
	    continue;
	}
	v = &uvifs[numvifs];
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
	strncpy(v->uv_name, ifr.ifr_name, IFNAMSIZ);
	v->uv_groups		= (struct listaddr *)NULL;
	v->uv_dvmrp_neighbors   = (struct listaddr *)NULL;
	NBRM_CLRALL(v->uv_nbrmap);
	v->uv_querier           = (struct listaddr *)NULL;
	v->uv_igmpv1_warn       = 0;
	v->uv_prune_lifetime    = 0;
	v->uv_acl               = (struct vif_acl *)NULL;
	RESET_TIMER(v->uv_leaf_timer);
	v->uv_addrs		= (struct phaddr *)NULL;
	v->uv_filter		= (struct vif_filter *)NULL;
	RESET_TIMER(v->uv_pim_hello_timer);
	RESET_TIMER(v->uv_gq_timer);
	v->uv_pim_neighbors	= (struct pim_nbr_entry *)NULL;
	v->uv_local_pref        = default_source_preference;
	v->uv_local_metric      = default_source_metric;
	
	if (flags & IFF_POINTOPOINT)
	    v->uv_flags |= (VIFF_REXMIT_PRUNES | VIFF_POINT_TO_POINT);
	log(LOG_INFO, 0,
	    "installing %s (%s on subnet %s) as vif #%u - rate=%d",
	    v->uv_name, inet_fmt(addr, s1), netname(subnet, mask),
	    numvifs, v->uv_rate_limit);
	++numvifs;
	
	/*
	 * If the interface is not yet up, set the vifs_down flag to
	 * remind us to check again later.
	 */
	if (!(flags & IFF_UP)) {
	    v->uv_flags |= VIFF_DOWN;
	    vifs_down = TRUE;
	}
    }
}


#define UNKNOWN        -1
#define EMPTY           1
#define PHYINT          2
#define DEFAULT_SOURCE_METRIC     3
#define DEFAULT_SOURCE_PREFERENCE 4

/*
 * function name: wordToOption
 * input: char *word, a pointer to the word
 * output: int; a number corresponding to the code of the word
 * operation: converts the result of the string comparisons into numerics.
 * comments: called by config_vifs_from_file()
 */
int 
wordToOption(word)
    char *word;
{
    if (EQUAL(word, ""))
	return EMPTY;
    if (EQUAL(word, "phyint"))
	return PHYINT;
    if (EQUAL(word, "default_source_metric"))
        return DEFAULT_SOURCE_METRIC;
    if (EQUAL(word, "default_source_preference"))
        return DEFAULT_SOURCE_PREFERENCE;

    return UNKNOWN;
}

/*
 * function name: parse_phyint
 * input: char *s, pointing to the parsing point of the file
 * output: int (TRUE if the parsing was successful, o.w. FALSE)
 * operation: parses the physical interface file configurations, if any.
 * The general form is:
 *     phyint <local-addr> [disable]
 */
static int
parse_phyint(s)
    char *s;
{
    char *w, c;
    u_int32 local;
    vifi_t vifi;
    struct uvif *v;
    u_int n;
    
    if (EQUAL((w = next_word(&s)), "")) {
	log(LOG_WARNING, 0, "Missing phyint address in %s", configfilename);
	return(FALSE);
    }		/* if empty */
    
    local = inet_parse(w, 4);
    if (!inet_valid_host(local)) {
	log(LOG_WARNING, 0, "Invalid phyint address '%s' in %s", w,
	    configfilename);
	return(FALSE);
    }		/* invalid address */
    
    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	if (vifi == numvifs) {
	    log(LOG_WARNING, 0,
		"phyint %s in %s is not a configured interface",
		inet_fmt(local, s1), configfilename);
	    return(FALSE);
	}	/* if vifi == numvifs */
	
	if (local != v->uv_lcl_addr)
	    continue;
	
	while (!EQUAL((w = next_word(&s)), "")) {
	    if (EQUAL(w, "disable"))
		v->uv_flags |= VIFF_DISABLED;
	    else if(EQUAL(w, "preference")) 
                if(EQUAL((w = next_word(&s)), "")) 
                    log(LOG_WARNING, 0,
                        "Missing preference for phyint %s in %s",
                        inet_fmt(local, s1), configfilename);
                else if (sscanf(w, "%u%c", &n, &c) != 1 ||
                         n < 1 || n > 255 )
                    log(LOG_WARNING, 0,
                        "Invalid preference '%s' for phyint %s in %s",
                        w, inet_fmt(local, s1),
                        configfilename);
		else {
		    IF_DEBUG(DEBUG_ASSERT)
			log(LOG_DEBUG, 0,
			    "Config setting default local preference on %s to %d.", 
			    inet_fmt(local, s1), n);
		    v->uv_local_pref = n;
		}
	    
	    else if(EQUAL(w, "metric")) 
                if(EQUAL((w = next_word(&s)), "")) 
                    log(LOG_WARNING, 0,
                        "Missing metric for phyint %s in %s",
                        inet_fmt(local, s1), configfilename);
                else if (sscanf(w, "%u%c", &n, &c) != 1 ||
                         n < 1 || n > 1024 )
                    log(LOG_WARNING, 0,
                        "Invalid metric '%s' for phyint %s in %s",
                        w, inet_fmt(local, s1),
                        configfilename);
		else {
		    IF_DEBUG(DEBUG_ASSERT)
			log(LOG_DEBUG, 0,
			    "Config setting default local metric on %s to %d.", 
			    inet_fmt(local, s1), n);
		    v->uv_local_metric = n;
		}
	    
	}		/* if not empty */
	break;
    }
    return(TRUE);
}


/*
 * function name: parse_default_source_metric
 * input: char *s
 * output: int
 * operation: reads and assigns the default source metric, if no reliable
 *            unicast routing information available.
 *            General form: 
 *              'default_source_metric <number>'.
 *            default pref and metric statements should precede all phyint
 *            statements in the config file.
 */
int
parse_default_source_metric(s)
    char *s;
{
    char *w;
    u_int value;
    vifi_t vifi;
    struct uvif *v;

    value = DEFAULT_LOCAL_METRIC;
    if (EQUAL((w = next_word(&s)), "")) {
        log(LOG_WARNING, 0,
            "Missing default source metric; set to default %u",
            DEFAULT_LOCAL_METRIC);
    } else if (sscanf(w, "%u", &value) != 1) {
        log(LOG_WARNING, 0,
            "Invalid default source metric; set to default %u",
            DEFAULT_LOCAL_METRIC);
        value = DEFAULT_LOCAL_METRIC;
    }
    default_source_metric = value;
    log(LOG_INFO, 0, "default_source_metric is %u", value);

    for (vifi = 0, v = uvifs; vifi < MAXVIFS; ++vifi, ++v) {
	v->uv_local_metric = default_source_metric;
    }
	
    return(TRUE);
}


/*
 * function name: parse_default_source_preference
 * input: char *s
 * output: int
 * operation: reads and assigns the default source preference, if no reliable
 *            unicast routing information available.
 *            General form: 
 *              'default_source_preference <number>'.
 *            default pref and metric statements should precede all phyint
 *            statements in the config file.
*/
int
parse_default_source_preference(s)
    char *s;
{
    char *w;
    u_int value;
    vifi_t vifi;
    struct uvif *v;

    value = DEFAULT_LOCAL_PREF;
    if (EQUAL((w = next_word(&s)), "")) {
        log(LOG_WARNING, 0,
            "Missing default source preference; set to default %u",
            DEFAULT_LOCAL_PREF);
    } else if (sscanf(w, "%u", &value) != 1) {
        log(LOG_WARNING, 0,
            "Invalid default source preference; set to default %u",
            DEFAULT_LOCAL_PREF);
        value = DEFAULT_LOCAL_PREF;
    }
    default_source_preference = value;
    log(LOG_INFO, 0, "default_source_preference is %u", value);

    for (vifi = 0, v = uvifs; vifi < MAXVIFS; ++vifi, ++v) {
	v->uv_local_pref = default_source_preference;
    }

    return(TRUE);
}




void 
config_vifs_from_file()
{
    FILE *f;
    char linebuf[100];
    char *w, *s;
    struct ifconf ifc;
    int option;
    char ifbuf[BUFSIZ];
    
    if ((f = fopen(configfilename, "r")) == NULL) {
	if (errno != ENOENT) log(LOG_WARNING, errno, "can't open %s", 
				 configfilename);
	return;
    }

    ifc.ifc_buf = ifbuf;
    ifc.ifc_len = sizeof(ifbuf);
    if (ioctl(udp_socket, SIOCGIFCONF, (char *)&ifc) < 0) 
	log(LOG_ERR, errno, "ioctl SIOCGIFCONF");
    
    while (fgets(linebuf, sizeof(linebuf), f) != NULL) {
	s = linebuf;
	w = next_word(&s);
	option = wordToOption(w);
	switch(option) {
	case EMPTY:
	    continue;
	    break;
	case PHYINT:
	    parse_phyint(s);
	    break;
	default:
	    log(LOG_WARNING, 0, "unknown command '%s' in %s",
		w, configfilename);
	}
    }
    fclose(f);
}


static char *
next_word(s)
    char **s;
{
    char *w;
    
    w = *s;
    while (*w == ' ' || *w == '\t')
	w++;
    
    *s = w;
    for(;;) {
	switch (**s) {
	case ' '  :
	case '\t' :
	    **s = '\0';
	    (*s)++;
	    return(w);
	case '\n' :
	case '#'  :
	    **s = '\0';
	    return(w);
	case '\0' :
	    return(w);
	default   :
	    if (isascii(**s) && isupper(**s))
		**s = tolower(**s);
	    (*s)++;
	}
    }
}

