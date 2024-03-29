/*
 * Copyright (c) 1993, 1998-2001.
 * The University of Southern California/Information Sciences Institute.
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

/* RSRR code written by Daniel Zappala, USC Information Sciences Institute,
 * April 1995.
 *
 * Modified by Kurtw Windisch (kurtw@antc.uoregon.edu) for use with 
 * Dense-mode pimd.
 */
#include <config.h>

/* May 1995 -- Added support for Route Change Notification */

#ifdef RSRR

#include "defs.h"
#include <sys/param.h>
#if (defined(BSD) && (BSD >= 199103))
#include <stddef.h>
#endif


/*
 * Exported variables.
 */
int rsrr_socket;		/* interface to reservation protocol */

/* 
 * Global RSRR variables.
 */
char *rsrr_recv_buf;    	/* RSRR receive buffer */
char *rsrr_send_buf;    	/* RSRR send buffer */

struct sockaddr_un client_addr;
socklen_t client_length = sizeof(client_addr);


/*
 * Local functions definition
 */
static void	rsrr_accept    (size_t recvlen);
static void	rsrr_accept_iq (void);
static int	rsrr_accept_rq (struct rsrr_rq *route_query, u_int8 flags, struct gtable *gt_notify);
static void	rsrr_read      (int, fd_set *);
static int	rsrr_send      (int sendlen);
static void	rsrr_cache     (struct gtable *gt, struct rsrr_rq *route_query);

/* Initialize RSRR socket */
void
rsrr_init()
{
    int servlen;
    struct sockaddr_un serv_addr;

    rsrr_recv_buf = calloc(1, RSRR_MAX_LEN);
    if (!rsrr_recv_buf)
	logit(LOG_ERR, errno, "rsrr_init(): out of memory");
    rsrr_send_buf = calloc(1, RSRR_MAX_LEN);
    if (!rsrr_send_buf)
	logit(LOG_ERR, errno, "rsrr_init(): out of memory");

    if ((rsrr_socket = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
	logit(LOG_ERR, errno, "Can't create RSRR socket");

    unlink(RSRR_SERV_PATH);
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sun_family = AF_UNIX;
    strlcpy(serv_addr.sun_path, RSRR_SERV_PATH, sizeof(serv_addr.sun_path));
#if (defined(BSD) && (BSD >= 199103))
    servlen = offsetof(struct sockaddr_un, sun_path) +
		strlen(serv_addr.sun_path);
    serv_addr.sun_len = servlen;
#else
    servlen = sizeof(serv_addr.sun_family) + strlen(serv_addr.sun_path);
#endif
 
    if (bind(rsrr_socket, (struct sockaddr *) &serv_addr, servlen) < 0)
	logit(LOG_ERR, errno, "Can't bind RSRR socket");

    if (register_input_handler(rsrr_socket, rsrr_read) < 0)
	logit(LOG_ERR, 0, "Couldn't register RSRR as an input handler");
}

/* Read a message from the RSRR socket */
static void
rsrr_read(f, rfd)
	int f;
	fd_set *rfd;
{
    ssize_t len;
    
    memset(&client_addr, 0, sizeof(client_addr));
    while ((len = recvfrom(rsrr_socket, rsrr_recv_buf, RSRR_MAX_LEN, 0,
		(struct sockaddr *)&client_addr, &client_length)) < 0) {

	if (errno == EINTR)
	    continue;

	logit(LOG_ERR, errno, "RSRR recvfrom");
	return;
    }

    rsrr_accept(len);
}

/*
 * Accept a message from the reservation protocol and take
 * appropriate action.
 */
static void
rsrr_accept(recvlen)
    size_t recvlen;
{
    struct rsrr_header *rsrr;
    struct rsrr_rq *route_query;
    
    if (recvlen < RSRR_HEADER_LEN) {
	logit(LOG_WARNING, 0,
	    "Received RSRR packet of %zu bytes, which is less than min size",
	    recvlen);
	return;
    }
    
    rsrr = (struct rsrr_header *) rsrr_recv_buf;
    
    if (rsrr->version > RSRR_MAX_VERSION) {
	logit(LOG_WARNING, 0,
	    "Received RSRR packet version %d, which I don't understand",
	    rsrr->version);
	return;
    }
    
    switch (rsrr->version) {
      case 1:
	switch (rsrr->type) {
	  case RSRR_INITIAL_QUERY:
	    /* Send Initial Reply to client */
	    IF_DEBUG(DEBUG_RSRR)
		logit(LOG_DEBUG, 0, "Received Initial Query");
	    rsrr_accept_iq();
	    break;
	  case RSRR_ROUTE_QUERY:
	    /* Check size */
	    if (recvlen < RSRR_RQ_LEN) {
		logit(LOG_WARNING, 0,
		    "Received Route Query of %zu bytes, which is too small",
		    recvlen);
		break;
	    }
	    /* Get the query */
	    route_query =
		(struct rsrr_rq *) (rsrr_recv_buf + RSRR_HEADER_LEN);
	    IF_DEBUG(DEBUG_RSRR)
		logit(LOG_DEBUG, 0,
		    "Received Route Query for src %s grp %s notification %d",
		    inet_fmt(route_query->source_addr, s1),
		    inet_fmt(route_query->dest_addr, s2),
		    BIT_TST(rsrr->flags, RSRR_NOTIFICATION_BIT));
	    /* Send Route Reply to client */
	    rsrr_accept_rq(route_query, rsrr->flags, (struct gtable *)NULL);
	    break;
	  default:
	    logit(LOG_WARNING, 0,
		"Received RSRR packet type %d, which I don't handle",
		rsrr->type);
	    break;
	}
	break;
	
      default:
	logit(LOG_WARNING, 0,
	    "Received RSRR packet version %d, which I don't understand",
	    rsrr->version);
	break;
    }
}

/* Send an Initial Reply to the reservation protocol. */
/*
 * TODO: XXX: if a new interfaces come up and _IF_ the multicast routing
 * daemon automatically include it, have to inform the RSVP daemon.
 * However, this is not in the RSRRv1 draft (just expired and is not
 * available anymore from the internet-draft ftp sites). Probably has to
 * be included in RSRRv2.
 */
static void
rsrr_accept_iq()
{
    struct rsrr_header *rsrr;
    struct rsrr_vif *vif_list;
    struct uvif *v;
    vifi_t vifi;
    int sendlen;
    
    /* Check for space.  There should be room for plenty of vifs,
     * but we should check anyway.
     */
    if (numvifs > RSRR_MAX_VIFS) {
	logit(LOG_WARNING, 0,
	      "Can't send RSRR Route Reply because %d is too many vifs (MAX: %d)",
	      numvifs, RSRR_MAX_VIFS);
	return;
    }
    
    /* Set up message */
    rsrr = (struct rsrr_header *) rsrr_send_buf;
    rsrr->version = 1;
    rsrr->type = RSRR_INITIAL_REPLY;
    rsrr->flags = 0;
    rsrr->num = numvifs;
    
    vif_list = (struct rsrr_vif *) (rsrr_send_buf + RSRR_HEADER_LEN);
    
    /* Include the vif list. */
    for (vifi = 0, v = uvifs; vifi < numvifs; vifi++, v++) {
	vif_list[vifi].id = vifi;
	vif_list[vifi].status = 0;
	if (v->uv_flags & (VIFF_DISABLED))
	    BIT_SET(vif_list[vifi].status, RSRR_DISABLED_BIT);
	vif_list[vifi].threshold = v->uv_threshold;
	vif_list[vifi].local_addr = v->uv_lcl_addr;
    }
    
    /* Get the size. */
    sendlen = RSRR_HEADER_LEN + numvifs*RSRR_VIF_LEN;
    
    /* Send it. */
    IF_DEBUG(DEBUG_RSRR)
	logit(LOG_DEBUG, 0, "Send RSRR Initial Reply");
    rsrr_send(sendlen);
}

/* Send a Route Reply to the reservation protocol.  The Route Query
 * contains the query to which we are responding.  The flags contain
 * the incoming flags from the query or, for route change
 * notification, the flags that should be set for the reply.  The
 * kernel table entry contains the routing info to use for a route
 * change notification.
 */
/* XXX: must modify if your routing table structure/search is different */
static int
rsrr_accept_rq(route_query, flags, gt_notify)
    struct rsrr_rq *route_query;
    u_int8 flags;
    struct gtable *gt_notify;
{
    struct rsrr_header *rsrr;
    struct rsrr_rr *route_reply;
    int sendlen;
    struct gtable *gt;
    int status_ok;
    
    /* Set up message */
    rsrr = (struct rsrr_header *) rsrr_send_buf;
    rsrr->version = 1;
    rsrr->type = RSRR_ROUTE_REPLY;
    rsrr->flags = flags;
    rsrr->num = 0;
    
    route_reply = (struct rsrr_rr *) (rsrr_send_buf + RSRR_HEADER_LEN);
    route_reply->dest_addr = route_query->dest_addr;
    route_reply->source_addr = route_query->source_addr;
    route_reply->query_id = route_query->query_id;
    
    /* Blank routing entry for error. */
    route_reply->in_vif = 0;
    route_reply->reserved = 0;
    route_reply->out_vif_bm = 0;
    
    /* Get the size. */
    sendlen = RSRR_RR_LEN;
    
    /* If routing table entry is defined, then we are sending a Route Reply
     * due to a Route Change Notification event.  Use the routing table entry
     * to supply the routing info.
     */
    status_ok = FALSE;
    if (gt_notify) {
	/* Include the routing entry. */
	route_reply->in_vif = gt_notify->incoming;
	route_reply->out_vif_bm = gt_notify->oifs;
	gt = gt_notify;
	status_ok = TRUE;
    } else if ((gt = find_route(route_query->source_addr,
				route_query->dest_addr,
				MRTF_SG | MRTF_WC | MRTF_PMBR,
				DONT_CREATE)) != (struct gtable *)NULL) {
	status_ok = TRUE;
	route_reply->in_vif = gt->incoming;
	route_reply->out_vif_bm = gt->oifs;
    }
    if (status_ok != TRUE) {
	/* Set error bit. */
	rsrr->flags = 0;
	BIT_SET(rsrr->flags, RSRR_ERROR_BIT);
    }
    else {
	/* Cache reply if using route change notification. */
	if (BIT_TST(flags, RSRR_NOTIFICATION_BIT)) {
	    /* TODO: XXX: Originally the rsrr_cache() call was first, but
	     * I think this is incorrect, because rsrr_cache() checks the
	     * rsrr_send_buf "flag" first.
	     */
	    BIT_SET(rsrr->flags, RSRR_NOTIFICATION_BIT);
	    rsrr_cache(gt, route_query);
	}
    }
    
    IF_DEBUG(DEBUG_RSRR)
	logit(LOG_DEBUG, 0, "%sSend RSRR Route Reply for src %s dst %s in vif %d out vifs 0x%x",
	    gt_notify ? "Route Change: " : "",
	    inet_fmt(route_reply->source_addr,s1),
	    inet_fmt(route_reply->dest_addr,s2),
	    route_reply->in_vif, route_reply->out_vif_bm);
    
    /* Send it. */
    return rsrr_send(sendlen);
}

/* Send an RSRR message. */
static int
rsrr_send(sendlen)
    int sendlen;
{
    int error;
    
    /* Send it. */
    error = sendto(rsrr_socket, rsrr_send_buf, sendlen, 0,
		   (struct sockaddr *)&client_addr, client_length);
    
    /* Check for errors. */
    if (error < 0) {
	logit(LOG_WARNING, errno, "Failed send on RSRR socket");
    } else if (error != sendlen) {
	logit(LOG_WARNING, 0,
	    "Sent only %d out of %d bytes on RSRR socket", error, sendlen);
    }
    return error;
}

/* TODO: need to sort the rsrr_cache entries for faster access */
/* Cache a message being sent to a client.  Currently only used for
 * caching Route Reply messages for route change notification.
 */
static void
rsrr_cache(gt, route_query)
    struct gtable *gt;
    struct rsrr_rq *route_query;
{
    struct rsrr_cache *rc, **rcnp;
    struct rsrr_header *rsrr;

    rsrr = (struct rsrr_header *) rsrr_send_buf;

    rcnp = &gt->rsrr_cache;
    while ((rc = *rcnp) != NULL) {
	if ((rc->route_query.source_addr == 
	     route_query->source_addr) &&
	    (rc->route_query.dest_addr == 
	     route_query->dest_addr) &&
	    (!strcmp(rc->client_addr.sun_path, client_addr.sun_path))) {
	    /* Cache entry already exists.
	     * Check if route notification bit has been cleared.
	     */
	    if (!BIT_TST(rsrr->flags, RSRR_NOTIFICATION_BIT)) {
		/* Delete cache entry. */
		*rcnp = rc->next;
		free(rc);
	    } else {
		/* Update */
		/* TODO: XXX: No need to update iif, oifs, flags */
		rc->route_query.query_id = route_query->query_id;
		IF_DEBUG(DEBUG_RSRR)
		    logit(LOG_DEBUG, 0,
			  "Update cached query id %u from client %s",
			  rc->route_query.query_id, rc->client_addr.sun_path);
	    }
	    return;
	}
	rcnp = &rc->next;
    }
    
    /* Cache entry doesn't already exist.  Create one and insert at
     * front of list.
     */
    rc = calloc(1, sizeof(struct rsrr_cache));
    if (!rc)
	logit(LOG_ERR, 0, "ran out of memory");

    rc->route_query.source_addr = route_query->source_addr;
    rc->route_query.dest_addr = route_query->dest_addr;
    rc->route_query.query_id = route_query->query_id;
    strlcpy(rc->client_addr.sun_path, client_addr.sun_path, sizeof(rc->client_addr.sun_path));
    rc->client_length = client_length;
    rc->next = gt->rsrr_cache;
    gt->rsrr_cache = rc;
    IF_DEBUG(DEBUG_RSRR)
	logit(LOG_DEBUG, 0, "Cached query id %u from client %s",
	    rc->route_query.query_id, rc->client_addr.sun_path);
}

/* Send all the messages in the cache for particular routing entry.
 * Currently this is used to send all the cached Route Reply messages
 * for route change notification.
 */
void
rsrr_cache_send(gt, notify)
    struct gtable *gt;
    int notify;
{
    struct rsrr_cache *rc, **rcnp;
    u_int8 flags = 0;

    if (notify)
	BIT_SET(flags, RSRR_NOTIFICATION_BIT);

    rcnp = &gt->rsrr_cache;

    while ((rc = *rcnp) != NULL) {
	if (rsrr_accept_rq(&rc->route_query, flags, gt) < 0) {
	    IF_DEBUG(DEBUG_RSRR)
		logit(LOG_DEBUG, 0,
		      "Deleting cached query id %u from client %s",
		      rc->route_query.query_id, rc->client_addr.sun_path);
	    /* Delete cache entry. */
	    *rcnp = rc->next;
	    free(rc);
	} else {
	    rcnp = &rc->next;
	}
    }
}

/* Clean the cache by deleting or moving all entries. */
/* XXX: for PIM, if the routing entry is (S,G), will try first to
 * "transfer" the RSRR cache entry to the (*,G) or (*,*,RP) routing entry
 * (if any). If the current routing entry is (*,G), it will move the
 * cache entries to the (*,*,RP) routing entry (if existing).
 * If the old and the new (iif, oifs) are the same, then no need to send
 * route change message to the reservation daemon: just plug all entries at
 * the front of the rsrr_cache chain.
 */
void
rsrr_cache_clean(gt)
    struct gtable *gt;
{
    struct rsrr_cache *rc, *rc_next;

    IF_DEBUG(DEBUG_RSRR) {
	logit(LOG_DEBUG, 0, "cleaning cache for source %s and group %s",
	    inet_fmt(gt->source->address, s1),
	    inet_fmt(gt->group->group, s2));
    }
    rc = gt->rsrr_cache;
    if (rc == (struct rsrr_cache *)NULL)
	return;

    while(rc) {
        rc_next = rc->next;
        free(rc);
        rc = rc_next;
     }

    gt->rsrr_cache = (struct rsrr_cache *)NULL;
}

void
rsrr_clean()
{
    unlink(RSRR_SERV_PATH);
    if (rsrr_recv_buf)
	    free(rsrr_recv_buf);
    if (rsrr_send_buf)
	    free(rsrr_send_buf);

    rsrr_recv_buf = NULL;
    rsrr_send_buf = NULL;
}

#endif /* RSRR */

/**
 * Local Variables:
 *  c-file-style: "cc-mode"
 * End:
 */
