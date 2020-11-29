/*
 * Copyright (c) 2018-2020  Joachim Wiberg <troglobit@gmail.com>
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
 * IPC API:
 *    - text based for compat with any PIM daemon
 *    - required commands: HELP, SHOW, VERSION
 *
 * Client asks daemon for available commands with HELP (help sep. w/ two spaces)
 * Client can also send VERSION to get daemon version
 * Client can send SHOW to get general status overview
 *
 * Daemon requires commands to be EXACT MATCH, so the client must
 * translate any short-commands to the full command before sending
 * it to the daemon.
 */

#include "defs.h"

#define ENABLED(v) (v ? "Enabled" : "Disabled")

static struct sockaddr_un sun;
static int ipc_socket = -1;
static int detail = 0;

enum {
	IPC_ERR = -1,
	IPC_OK  = 0,
	IPC_HELP,
	IPC_VERSION,
	IPC_STATUS,
	IPC_RESTART,
	IPC_DEBUG,
	IPC_LOGLEVEL,
	IPC_KILL,
	IPC_IGMP_GRP,
	IPC_IGMP_IFACE,
	IPC_PIM_IFACE,
	IPC_PIM_NEIGH,
	IPC_PIM_ROUTE,
	IPC_PIM_DUMP
};

struct ipcmd {
	int   op;
	char *cmd;
	char *arg;
	char *help;
} cmds[] = {
	{ IPC_DEBUG,      "debug", "[? | none | SYS]", "Debug subystem(s), separate multiple with comma"},
	{ IPC_HELP,       "help", NULL, "This help text" },
	{ IPC_KILL,       "kill", NULL, "Kill running pimd-dense daemon, like SIGTERM"},
	{ IPC_LOGLEVEL,   "log", "[? | none | LEVEL]" , "Set pimd-dense log level: none, err, notice*, info, debug"},
	{ IPC_RESTART,    "restart", NULL, "Restart pimd and reload the .conf file, like SIGHUP"},
	{ IPC_VERSION,    "version", NULL, "Show pimd-dense version" },
	{ IPC_STATUS,     "show status", NULL, "Show pimd-dense status, default" },
	{ IPC_IGMP_GRP,   "show igmp groups", NULL, "Show IGMP group memberships" },
	{ IPC_IGMP_IFACE, "show igmp interface", NULL, "Show IGMP interface status" },
	{ IPC_PIM_IFACE,  "show pim interface", NULL, "Show PIM interface table" },
	{ IPC_PIM_NEIGH,  "show pim neighbor", NULL, "Show PIM neighbor table" },
	{ IPC_PIM_ROUTE,  "show pim routes", "[detail]", "Show PIM routing table" },
	{ IPC_PIM_DUMP,   "show pim compat", "[detail]", "Show PIM status, compat mode" },
	{ IPC_PIM_DUMP,   "show", NULL, NULL }, /* hidden default */
};

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

static char *chomp(char *str)
{
	char *p;

	if (!str || strlen(str) < 1) {
		errno = EINVAL;
		return NULL;
	}

	p = str + strlen(str) - 1;
        while (*p == '\n')
		*p-- = 0;

	return str;
}

static void strip(char *cmd, size_t len)
{
	char *ptr;

	ptr = cmd + len;
	len = strspn(ptr, " \t\n");
	if (len > 0)
		ptr += len;

	memmove(cmd, ptr, strlen(ptr) + 1);
	chomp(cmd);
}

static void check_detail(char *cmd, size_t len)
{
	const char *det = "detail";
	char *ptr;

	strip(cmd, len);

	len = MIN(strlen(cmd), strlen(det));
	if (len > 0 && !strncasecmp(cmd, det, len)) {
		len = strcspn(cmd, " \t\n");
		strip(cmd, len);

		detail = 1;
	} else
		detail = 0;
}

static int ipc_read(int sd, char *cmd, ssize_t len)
{
	len = read(sd, cmd, len - 1);
	if (len < 0)
		return IPC_ERR;
	if (len == 0)
		return IPC_OK;

	cmd[len] = 0;
	logit(LOG_DEBUG, 0, "IPC cmd: '%s'", cmd);

	for (size_t i = 0; i < NELEMS(cmds); i++) {
		struct ipcmd *c = &cmds[i];
		size_t len = strlen(c->cmd);

		if (!strncasecmp(cmd, c->cmd, len)) {
			check_detail(cmd, len);
			return c->op;
		}
	}

	errno = EBADMSG;
	return IPC_ERR;
}

static int ipc_write(int sd, char *msg, size_t sz)
{
	ssize_t len;

	logit(LOG_DEBUG, 0, "IPC rpl: '%s'", msg);

	while ((len = write(sd, msg, sz))) {
		if (-1 == len && EINTR == errno)
			continue;
		break;
	}

	if (len != (ssize_t)sz)
		return IPC_ERR;

	return 0;
}

static int ipc_close(int sd)
{
	return shutdown(sd, SHUT_RDWR) ||
		close(sd);
}

static int ipc_send(int sd, char *buf, size_t len, FILE *fp)
{
	while (fgets(buf, len, fp)) {
		if (!ipc_write(sd, buf, strlen(buf)))
			continue;

		logit(LOG_WARNING, errno, "Failed communicating with client");
		return IPC_ERR;
	}

	return ipc_close(sd);
}

static void ipc_show(int sd, int (*cb)(FILE *), char *buf, size_t len)
{
	FILE *fp;

	fp = tempfile();
	if (!fp) {
		logit(LOG_WARNING, errno, "Failed opening temporary file");
		return;
	}

	if (cb(fp))
		return;

	rewind(fp);
	ipc_send(sd, buf, len, fp);
	fclose(fp);
}

static int ipc_err(int sd, char *buf, size_t len)
{
	switch (errno) {
	case EBADMSG:
		snprintf(buf, len, "No such command, see 'help' for available commands.");
		break;

	case EINVAL:
		snprintf(buf, len, "Invalid argument.");
		break;

	default:
		snprintf(buf, len, "Unknown error: %s", strerror(errno));
		break;
	}

	return ipc_write(sd, buf, strlen(buf));
}

/* wrap simple functions that don't use >768 bytes for I/O */
static int ipc_wrap(int sd, int (*cb)(char *, size_t), char *buf, size_t len)
{
	if (cb(buf, len))
		return IPC_ERR;

	return ipc_write(sd, buf, strlen(buf));
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

static const char *ifstate(struct uvif *uv)
{
	if (uv->uv_flags & VIFF_DOWN)
		return "Down";

	if (uv->uv_flags & VIFF_DISABLED)
		return "Disabled";

	return "Up";
}

/* PIM Neighbor Table */
static int show_neighbors(FILE *fp)
{
	pim_nbr_entry_t *n;
	struct uvif *uv;
	vifi_t vifi;
	int first = 1;

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

	return 0;
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

	fprintf(fp, "%-16s  %-8s  %-15s  %3zu  %5d  %4s  %-15s\n",
		uv->uv_name,
		ifstate(uv),
		inet_fmt(uv->uv_lcl_addr, s1),
		num, PIM_TIMER_HELLO_PERIOD,
		pri, inet_fmt(addr, s2));
}

/* PIM Interface Table */
static int show_interfaces(FILE *fp)
{
	vifi_t vifi;

	if (numvifs)
		fprintf(fp, "Interface         State     Address          Nbr  Hello  Prio  DR Address =\n");

	for (vifi = 0; vifi < numvifs; vifi++)
		show_interface(fp, &uvifs[vifi]);

	return 0;
}

static void dump_route(FILE *fp, mrtentry_t *r)
{
	char incoming_iif[MAXVIFS+1];
	char pruned_oifs[MAXVIFS+1];
	char leaves_oifs[MAXVIFS+1];
	char oifs[MAXVIFS+1];
	vifi_t vifi;

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
static int show_pim_mrt(FILE *fp)
{
	u_int number_of_groups = 0;
	grpentry_t *g;
	mrtentry_t *r;

	if (!detail)
		fprintf(fp, "Source           Group             Flags =\n");

	/* TODO: remove the dummy 0.0.0.0 group (first in the chain) */
	for (g = grplist->next; g; g = g->next) {
		number_of_groups++;

		/* Print all (S,G) routing info */
		for (r = g->mrtlink; r; r = r->grpnext) {
			if (detail)
				fprintf(fp, "\nSource           Group             Flags =\n");
			fprintf(fp, "%-15s  %-15s  ",
				inet_fmt(r->source->address, s1),
				inet_fmt(g->group, s2));

			dump_route(fp, r);
		}
	}

	fprintf(fp, "\nNumber of Groups        : %u\n", number_of_groups);

	return 0;
}

static int show_status(FILE *fp)
{
	char buf[10];
	int len;

/* TODO: What's relevant daemon status for pimd-dense?
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

	return 0;
}

static int show_igmp_groups(FILE *fp)
{
	struct listaddr *group, *source;
	struct uvif *uv;
	vifi_t vifi;

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

	return 0;
}

static int show_igmp_iface(FILE *fp)
{
	struct listaddr *group;
	struct uvif *uv;
	vifi_t vifi;

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
		else if (uv->uv_flags & VIFF_IGMPV2)
			version = 2;
		else
			version = 3;

		fprintf(fp, "%-16s  %-8s  %-15s  %7s %7d  %6zd\n", uv->uv_name,
			ifstate(uv), s1, timeout, version, num);
	}

	return 0;
}

static int show_dump(FILE *fp)
{
	dump_vifs(fp, detail);
	dump_pim_mrt(fp, detail);

	return 0;
}

static int show_version(FILE *fp)
{
	fputs(versionstring, fp);
	return 0;
}

static int ipc_debug(char *buf, size_t len)
{
	if (!strcmp(buf, "?"))
		return debug_list(DEBUG_ALL, buf, len);

	if (strlen(buf)) {
		int rc = debug_parse(buf);

		if ((int)DEBUG_PARSE_FAIL == rc) {
			errno = EINVAL;
			return 1;
		}

		/* Activate debugging of new subsystems */
		debug = rc;
	}

	/* Return list of activated subsystems */
	if (debug)
		debug_list(debug, buf, len);
	else
		snprintf(buf, len, "none");

	return 0;
}

static int ipc_loglevel(char *buf, size_t len)
{
	int rc;

	if (!strcmp(buf, "?"))
		return log_list(buf, len);

	if (!strlen(buf)) {
		strlcpy(buf, log_lvl2str(log_level), len);
		return 0;
	}

	rc = log_str2lvl(buf);
	if (-1 == rc) {
		errno = EINVAL;
		return 1;
	}

	logit(LOG_NOTICE, 0, "Setting new log level %s", log_lvl2str(rc));
	log_level = rc;

	return 0;
}

static void ipc_help(int sd, char *buf, size_t len)
{
	FILE *fp;

	fp = tempfile();
	if (!fp) {
		int sz;

		sz = snprintf(buf, len, "Cannot create tempfile: %s", strerror(errno));
		if (write(sd, buf, sz) != sz)
			logit(LOG_INFO, errno, "Client closed connection");
		return;
	}

	for (size_t i = 0; i < NELEMS(cmds); i++) {
		struct ipcmd *c = &cmds[i];
		char tmp[50];

		if (!c->help)
			continue; /* skip hidden commands */

		snprintf(tmp, sizeof(tmp), "%s%s%s", c->cmd, c->arg ? " " : "", c->arg ?: "");
		fprintf(fp, "%s\t%s\n", tmp, c->help);
	}
	rewind(fp);

	while (fgets(buf, len, fp)) {
		if (!ipc_write(sd, buf, strlen(buf)))
			continue;

		logit(LOG_WARNING, errno, "Failed communicating with client");
	}

	fclose(fp);
}

static void ipc_handle(int sd, fd_set *rfd)
{
	char cmd[768] = { 0 };
	ssize_t len;
	int client;
	int rc = 0;

	client = accept(sd, NULL, NULL);
	if (client < 0)
		return;

	switch (ipc_read(client, cmd, sizeof(cmd))) {
	case IPC_HELP:
		ipc_help(client, cmd, sizeof(cmd));
		break;

	case IPC_DEBUG:
		rc = ipc_wrap(client, ipc_debug, cmd, sizeof(cmd));
		break;

	case IPC_LOGLEVEL:
		rc = ipc_wrap(client, ipc_loglevel, cmd, sizeof(cmd));
		break;

	case IPC_KILL:
		rc = ipc_wrap(client, daemon_kill, cmd, sizeof(cmd));
		break;

	case IPC_RESTART:
		rc = ipc_wrap(client, daemon_restart, cmd, sizeof(cmd));
		break;

	case IPC_VERSION:
		ipc_show(client, show_version, cmd, sizeof(cmd));
		break;

	case IPC_IGMP_GRP:
		ipc_show(client, show_igmp_groups, cmd, sizeof(cmd));
		break;

	case IPC_IGMP_IFACE:
		ipc_show(client, show_igmp_iface, cmd, sizeof(cmd));
		break;

	case IPC_PIM_IFACE:
		ipc_show(client, show_interfaces, cmd, sizeof(cmd));
		break;

	case IPC_PIM_NEIGH:
		ipc_show(client, show_neighbors, cmd, sizeof(cmd));
		break;

	case IPC_PIM_ROUTE:
		ipc_show(client, show_pim_mrt, cmd, sizeof(cmd));
		break;

	case IPC_STATUS:
		ipc_show(client, show_status, cmd, sizeof(cmd));
		break;

	case IPC_PIM_DUMP:
		ipc_show(client, show_dump, cmd, sizeof(cmd));
		break;

	case IPC_OK:
		/* client ping, ignore */
		break;

	case IPC_ERR:
		logit(LOG_WARNING, errno, "Failed reading command from client");
		rc = IPC_ERR;
		break;

	default:
		logit(LOG_WARNING, 0, "Invalid IPC command: %s", cmd);
		break;
	}

	if (rc == IPC_ERR)
		ipc_err(sd, cmd, sizeof(cmd));

	ipc_close(client);
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
