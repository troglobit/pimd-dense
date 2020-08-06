/*
 * Copyright (c) 2018-2020  Joachim Nilsson <troglobit@gmail.com>
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

#include "defs.h"

static struct sockaddr_un sun;
static int ipc_socket = -1;

static char *timetostr(time_t t, char *buf, size_t len)
{
	int sec, min, hour, day;
	static char tmp[20];

	if (!buf) {
		buf = tmp;
		len = sizeof(tmp);
	}

	day  = t / 86400;
	t    = t % 86400;
	hour = t / 3600;
	t    = t % 3600;
	min  = t / 60;
	t    = t % 60;
	sec  = t;

	if (day)
		snprintf(buf, len, "%dd%dh%dm%ds", day, hour, min, sec);
	else
		snprintf(buf, len, "%dh%dm%ds", hour, min, sec);

	return buf;
}

static void show_neighbor(FILE *fp, struct uvif *uv, pim_nbr_entry_t *n)
{
	char tmp[20], buf[42];

	snprintf(buf, sizeof(buf), "%s", timetostr(n->timer, NULL, 0));

	if (uv->uv_flags & VIFF_DR) {
		memset(tmp, 0, sizeof(tmp));
	} else {
		if (uv->uv_pim_neighbors == n)
			snprintf(tmp, sizeof(tmp), "DR");
	}

	fprintf(fp, "%-16s  %-15s  %-4s  %-28s\n",
		uv->uv_name,
		inet_fmt(n->address, s1),
		tmp, buf);
}

/* PIM Neighbor Table */
static void show_neighbors(FILE *fp, int detail)
{
	pim_nbr_entry_t *n;
	struct uvif *uv;
	vifi_t vifi;
	int first = 1;

	(void)detail;

	for (vifi = 0; vifi < numvifs; vifi++) {
		uv = &uvifs[vifi];

		for (n = uv->uv_pim_neighbors; n; n = n->next) {
			if (first) {
				fprintf(fp, "Interface         Address          Mode  Expires               =\n");
				first = 0;
			}
			show_neighbor(fp, uv, n);
		}
	}
}

static void show_interface(FILE *fp, struct uvif *uv)
{
	pim_nbr_entry_t *n;
	uint32_t addr = 0;
	size_t num  = 0;
	char *pri = "N/A";

	if (uv->uv_flags & VIFF_REGISTER)
		return;

	if (uv->uv_flags & VIFF_DR)
		addr = uv->uv_lcl_addr;
	else if (uv->uv_pim_neighbors)
		addr = uv->uv_pim_neighbors->address;

	for (n = uv->uv_pim_neighbors; n; n = n->next)
		num++;

	fprintf(fp, "%-16s  %-4s   %-15s  %3zu  %5d  %4s  %-15s\n",
		uv->uv_name,
		(uv->uv_flags & VIFF_DOWN) ? "DOWN" : "UP",
		inet_fmt(uv->uv_lcl_addr, s1),
		num, PIM_TIMER_HELLO_PERIOD,
		pri, inet_fmt(addr, s2));
}

/* PIM Interface Table */
static void show_interfaces(FILE *fp, int detail)
{
	vifi_t vifi;

	(void)detail;

	if (numvifs)
		fprintf(fp, "Interface         State  Address          Nbr  Hello  Prio  DR Address =\n");

	for (vifi = 0; vifi < numvifs; vifi++)
		show_interface(fp, &uvifs[vifi]);
}

static void dump_route(FILE *fp, mrtentry_t *r, int detail)
{
	vifi_t vifi;
	char oifs[MAXVIFS+1];
	char pruned_oifs[MAXVIFS+1];
	char leaves_oifs[MAXVIFS+1];
	char incoming_iif[MAXVIFS+1];

	for (vifi = 0; vifi < numvifs; vifi++) {
		oifs[vifi] =
			VIFM_ISSET(vifi, r->oifs) ? 'o' : '.';
		pruned_oifs[vifi] =
			VIFM_ISSET(vifi, r->pruned_oifs) ? 'p' : '.';
		leaves_oifs[vifi] =
			VIFM_ISSET(vifi, r->leaves) ? 'l' : '.';
		incoming_iif[vifi] = '.';
	}
	oifs[vifi]		= 0x0;	/* End of string */
	pruned_oifs[vifi]	= 0x0;
	leaves_oifs[vifi]	= 0x0;
	incoming_iif[vifi]	= 0x0;
	incoming_iif[r->incoming] = 'I';

	/* TODO: don't need some of the flags */
	if (r->flags & MRTF_SPT)	  fprintf(fp, " SPT");
	if (r->flags & MRTF_WC)	          fprintf(fp, " WC");
	if (r->flags & MRTF_RP)	          fprintf(fp, " RP");
	if (r->flags & MRTF_REGISTER)     fprintf(fp, " REG");
	if (r->flags & MRTF_IIF_REGISTER) fprintf(fp, " IIF_REG");
	if (r->flags & MRTF_NULL_OIF)     fprintf(fp, " NULL_OIF");
	if (r->flags & MRTF_KERNEL_CACHE) fprintf(fp, " CACHE");
	if (r->flags & MRTF_ASSERTED)     fprintf(fp, " ASSERTED");
	if (r->flags & MRTF_REG_SUPP)     fprintf(fp, " REG_SUPP");
	if (r->flags & MRTF_SG)	          fprintf(fp, " SG");
	if (r->flags & MRTF_PMBR)	  fprintf(fp, " PMBR");
	fprintf(fp, "\n");

	if (!detail)
		return;

	fprintf(fp, "Pruned   oifs: %-20s\n", pruned_oifs);
	fprintf(fp, "Leaves   oifs: %-20s\n", leaves_oifs);
	fprintf(fp, "Outgoing oifs: %-20s\n", oifs);
	fprintf(fp, "Incoming     : %-20s\n", incoming_iif);

	fprintf(fp, "\nTIMERS       :  Entry    Prune VIFS:");
	for (vifi = 0; vifi < numvifs; vifi++)
		fprintf(fp, "  %d", vifi);
	fprintf(fp, "\n                %5d                 ", r->timer);
	for (vifi = 0; vifi < numvifs; vifi++)
		fprintf(fp, " %3d", r->prune_timers[vifi]);
	fprintf(fp, "\n");
}

/* PIM Multicast Routing Table */
static void show_pim_mrt(FILE *fp, int detail)
{
	u_int number_of_groups = 0;
	grpentry_t *g;
	mrtentry_t *r;

	/* TODO: remove the dummy 0.0.0.0 group (first in the chain) */
	for (g = grplist->next; g; g = g->next) {
		number_of_groups++;

		/* Print all (S,G) routing info */
		for (r = g->mrtlink; r; r = r->grpnext) {
			if (detail)
				fprintf(fp, "\nSource           Group            Flags =\n");
			fprintf(fp, "%-15s  %-15s  ",
				inet_fmt(r->source->address, s1),
				inet_fmt(g->group, s2));

			dump_route(fp, r, detail);
		}
	}

	fprintf(fp, "\nNumber of Groups        : %u\n", number_of_groups);
}

#define ENABLED(v) (v ? "Enabled" : "Disabled")

static void show_status(FILE *fp, int detail)
{
	char buf[10];
	int len;

	(void)detail;
/*
	snprintf(buf, sizeof(buf), "%d", curr_bsr_priority);
	MASK_TO_MASKLEN(curr_bsr_hash_mask, len);

	fprintf(fp, "Elected BSR\n");
	fprintf(fp, "    Address          : %s\n", inet_fmt(curr_bsr_address, s1));
	fprintf(fp, "    Expiry Time      : %s\n", !pim_bootstrap_timer ? "N/A" : timetostr(pim_bootstrap_timer, NULL, 0));
	fprintf(fp, "    Priority         : %s\n", !curr_bsr_priority ? "N/A" : buf);
	fprintf(fp, "    Hash Mask Length : %d\n", len);

	snprintf(buf, sizeof(buf), "%d", my_bsr_priority);
	MASK_TO_MASKLEN(my_bsr_hash_mask, len);

	fprintf(fp, "Candidate BSR\n");
	fprintf(fp, "    State            : %s\n", ENABLED(cand_bsr_flag));
	fprintf(fp, "    Address          : %s\n", inet_fmt(my_bsr_address, s1));
	fprintf(fp, "    Priority         : %s\n", !my_bsr_priority ? "N/A" : buf);

	fprintf(fp, "Candidate RP\n");
	fprintf(fp, "    State            : %s\n", ENABLED(cand_rp_flag));
	fprintf(fp, "    Address          : %s\n", inet_fmt(my_cand_rp_address, s1));
	fprintf(fp, "    Priority         : %d\n", my_cand_rp_priority);
	fprintf(fp, "    Holdtime         : %d sec\n", my_cand_rp_holdtime);

	fprintf(fp, "Join/Prune Interval  : %d sec\n", PIM_JOIN_PRUNE_PERIOD);
	fprintf(fp, "Hello Interval       : %d sec\n", pim_timer_hello_interval);
	fprintf(fp, "Hello Holdtime       : %d sec\n", pim_timer_hello_holdtime);
	fprintf(fp, "IGMP query interval  : %d sec\n", igmp_query_interval);
	fprintf(fp, "IGMP querier timeout : %d sec\n", igmp_querier_timeout);
	fprintf(fp, "SPT Threshold        : %s\n", spt_threshold.mode == SPT_INF ? "Disabled" : "Enabled");
	if (spt_threshold.mode != SPT_INF)
		fprintf(fp, "SPT Interval         : %d sec\n", spt_threshold.interval);
*/
}

static void show_igmp_groups(FILE *fp, int detail)
{
	struct listaddr *group, *source;
	struct uvif *uv;
	vifi_t vifi;

	(void)detail;

	fprintf(fp, "Interface         Group            Source           Last Reported    Timeout=\n");
	for (vifi = 0, uv = uvifs; vifi < numvifs; vifi++, uv++) {
		for (group = uv->uv_groups; group; group = group->al_next) {
			char pre[40], post[40];

			snprintf(pre, sizeof(pre), "%-16s  %-15s  ",
				 uv->uv_name, inet_fmt(group->al_addr, s1));

			snprintf(post, sizeof(post), "%-15s  %7lu",
				 inet_fmt(group->al_reporter, s1),
				 group->al_timer);

			fprintf(fp, "%s%-15s  %s\n", pre, "ANY", post);
			continue;
		}
	}
}

static const char *ifstate(struct uvif *uv)
{
	if (uv->uv_flags & VIFF_DOWN)
		return "Down";

	if (uv->uv_flags & VIFF_DISABLED)
		return "Disabled";

	return "Up";
}

static void show_igmp_iface(FILE *fp, int detail)
{
	struct listaddr *group;
	struct uvif *uv;
	vifi_t vifi;

	(void)detail;
	fprintf(fp, "Interface         State     Querier          Timeout Version  Groups=\n");

	for (vifi = 0, uv = uvifs; vifi < numvifs; vifi++, uv++) {
		size_t num = 0;
		char timeout[10];
		int version;

		/* The register_vif is never used for IGMP messages */
		if (uv->uv_flags & VIFF_REGISTER)
			continue;

		if (!uv->uv_querier) {
			strlcpy(s1, "Local", sizeof(s1));
			snprintf(timeout, sizeof(timeout), "None");
		} else {
			inet_fmt(uv->uv_querier->al_addr, s1);
			snprintf(timeout, sizeof(timeout), "%lu", IGMP_OTHER_QUERIER_PRESENT_INTERVAL - uv->uv_querier->al_timer);
		}

		for (group = uv->uv_groups; group; group = group->al_next)
			num++;

		if (uv->uv_flags & VIFF_IGMPV1)
			version = 1;
/*		else if (uv->uv_flags & VIFF_IGMPV2)
			version = 2;
*/		else
			version = 3;

		fprintf(fp, "%-16s  %-8s  %-15s  %7s %7d  %6zd\n", uv->uv_name,
			ifstate(uv), s1, timeout, version, num);
	}
}

static void show_dump(FILE *fp, int detail)
{
	dump_vifs(fp, detail);
	dump_pim_mrt(fp, detail);
}

static int do_debug(void *arg)
{
	struct ipc *msg = (struct ipc *)arg;

	if (!strcmp(msg->buf, "?"))
		return debug_list(DEBUG_ALL, msg->buf, sizeof(msg->buf));

	if (strlen(msg->buf)) {
		int rc = debug_parse(msg->buf);

		if ((int)DEBUG_PARSE_FAIL == rc)
			return 1;

		/* Activate debugging of new subsystems */
		debug = rc;
	}

	/* Return list of activated subsystems */
	if (debug)
		debug_list(debug, msg->buf, sizeof(msg->buf));
	else
		snprintf(msg->buf, sizeof(msg->buf), "none");

	return 0;
}

static int do_loglevel(void *arg)
{
	struct ipc *msg = (struct ipc *)arg;
	int rc;

	if (!strcmp(msg->buf, "?"))
		return log_list(msg->buf, sizeof(msg->buf));

	if (!strlen(msg->buf)) {
		strlcpy(msg->buf, log_lvl2str(log_level), sizeof(msg->buf));
		return 0;
	}

	rc = log_str2lvl(msg->buf);
	if (-1 == rc)
		return 1;

	logit(LOG_NOTICE, 0, "Setting new log level %s", log_lvl2str(rc));
	log_level = rc;

	return 0;
}

static int ipc_write(int sd, struct ipc *msg)
{
	ssize_t len;

	while ((len = write(sd, msg, sizeof(*msg)))) {
		if (-1 == len && EINTR == errno)
			continue;
		break;
	}

	if (len != sizeof(*msg))
		return -1;

	return 0;
}

static int ipc_close(int sd, struct ipc *msg, int status)
{
	msg->cmd = status;
	if (ipc_write(sd, msg)) {
		logit(LOG_WARNING, errno, "Failed sending EOF/ACK to client");
		return -1;
	}

	return 0;
}

static int ipc_send(int sd, struct ipc *msg, FILE *fp)
{
	msg->cmd = IPC_OK_CMD;
	while (fgets(msg->buf, sizeof(msg->buf), fp)) {
		if (!ipc_write(sd, msg))
			continue;

		logit(LOG_WARNING, errno, "Failed communicating with client");
		return -1;
	}

	return ipc_close(sd, msg, IPC_EOF_CMD);
}

static void ipc_show(int sd, struct ipc *msg, void (*cb)(FILE *, int))
{
	FILE *fp;

	fp = tmpfile();
	if (!fp) {
		logit(LOG_WARNING, errno, "Failed opening temporary file");
		ipc_close(sd, msg, IPC_ERR_CMD);
		return;
	}

	cb(fp, msg->detail);
	rewind(fp);
	ipc_send(sd, msg, fp);
	fclose(fp);
}

static void ipc_generic(int sd, struct ipc *msg, int (*cb)(void *), void *arg)
{
	if (cb(arg))
		msg->cmd = IPC_ERR_CMD;
	else
		msg->cmd = IPC_OK_CMD;

	if (write(sd, msg, sizeof(*msg)) == -1)
		logit(LOG_WARNING, errno, "Failed sending IPC reply");
}

static void ipc_handle(int sd, fd_set *rfd)
{
	socklen_t socklen = 0;
	struct ipc msg;
	ssize_t len;
	int client;

	client = accept(sd, NULL, &socklen);
	if (client < 0)
		return;

	len = read(client, &msg, sizeof(msg));
	if (len < 0) {
		logit(LOG_WARNING, errno, "Failed reading IPC command");
		close(client);
		return;
	}

	switch (msg.cmd) {
	case IPC_DEBUG_CMD:
		ipc_generic(client, &msg, do_debug, &msg);
		break;

	case IPC_LOGLEVEL_CMD:
		ipc_generic(client, &msg, do_loglevel, &msg);
		break;

	case IPC_KILL_CMD:
		ipc_generic(client, &msg, daemon_kill, NULL);
		break;

	case IPC_RESTART_CMD:
		ipc_generic(client, &msg, daemon_restart, NULL);
		break;

	case IPC_SHOW_IGMP_GROUPS_CMD:
		ipc_show(client, &msg, show_igmp_groups);
		break;

	case IPC_SHOW_IGMP_IFACE_CMD:
		ipc_show(client, &msg, show_igmp_iface);
		break;

	case IPC_SHOW_PIM_IFACE_CMD:
		ipc_show(client, &msg, show_interfaces);
		break;

	case IPC_SHOW_PIM_NEIGH_CMD:
		ipc_show(client, &msg, show_neighbors);
		break;

	case IPC_SHOW_PIM_ROUTE_CMD:
		ipc_show(client, &msg, show_pim_mrt);
		break;

	case IPC_SHOW_STATUS_CMD:
		ipc_show(client, &msg, show_status);
		break;

	case IPC_SHOW_PIM_DUMP_CMD:
		ipc_show(client, &msg, show_dump);
		break;

	default:
		logit(LOG_WARNING, 0, "Invalid IPC command '0x%02x'", msg.cmd);
		break;
	}

	close(client);
}


void ipc_init(void)
{
	socklen_t len;
	int sd;

	sd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sd < 0) {
		logit(LOG_ERR, errno, "Failed creating IPC socket");
		return;
	}

#ifdef HAVE_SOCKADDR_UN_SUN_LEN
	sun.sun_len = 0;	/* <- correct length is set by the OS */
#endif
	sun.sun_family = AF_UNIX;
	snprintf(sun.sun_path, sizeof(sun.sun_path), _PATH_PIMD_SOCK, ident);

	unlink(sun.sun_path);
	logit(LOG_DEBUG, 0, "Binding IPC socket to %s", sun.sun_path);

	len = offsetof(struct sockaddr_un, sun_path) + strlen(sun.sun_path);
	if (bind(sd, (struct sockaddr *)&sun, len) < 0 || listen(sd, 1)) {
		logit(LOG_WARNING, errno, "Failed binding IPC socket, client disabled");
		close(sd);
		return;
	}

	if (register_input_handler(sd, ipc_handle) < 0)
		logit(LOG_ERR, 0, "Failed registering IPC handler");

	ipc_socket = sd;
}

void ipc_exit(void)
{
	if (ipc_socket > -1)
		close(ipc_socket);

	unlink(sun.sun_path);
	ipc_socket = -1;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
