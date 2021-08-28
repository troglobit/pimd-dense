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
#include <getopt.h>

char versionstring[100];

char *ident       = PACKAGE_NAME;
char *prognm      = NULL;
char *pid_file    = NULL;
char *sock_file   = NULL;
char *config_file = NULL;

int   foreground = 0;

static int sighandled = 0;
#define GOT_SIGINT      0x01
#define GOT_SIGHUP      0x02
#define GOT_SIGUSR1     0x04
#define GOT_SIGUSR2     0x08
#define GOT_SIGALRM     0x10

#define NHANDLERS       4

static struct ihandler {
    int fd;			/* File descriptor               */
    ihfunc_t func;		/* Function to call with &fd_set */
} ihandlers[NHANDLERS];
static int nhandlers = 0;

/*
 * Forward declarations.
 */
static void handler (int);
static void timer (void *);
static void cleanup (void);
static void restart (int);
static void cleanup (void);
static void resetlogging (void *);

int
register_input_handler(fd, func)
    int fd;
ihfunc_t func;
{
    if (nhandlers >= NHANDLERS)
	return -1;
    
    ihandlers[nhandlers].fd = fd;
    ihandlers[nhandlers++].func = func;
    
    return 0;
}

static void pidfile_cleanup(void)
{
    if (pid_file) {
	if (remove(pid_file))
	    logit(LOG_INFO, errno, "Failed removing %s", pid_file);
	free(pid_file);
	pid_file = NULL;
    }
}

static void pidfile(void)
{
    FILE *fp;

    atexit(pidfile_cleanup);

    fp = fopen(pid_file, "w");
    if (fp) {
	fprintf(fp, "%d\n", getpid());
	fclose(fp);
    }
}

static int compose_paths(void)
{
    /* Default .conf file path: "/etc" + '/' + "pimd" + ".conf" */
    if (!config_file) {
	size_t len = strlen(SYSCONFDIR) + strlen(ident) + 7;

	config_file = calloc(1, len);
	if (!config_file) {
	    logit(LOG_ERR, errno, "Failed allocating memory, exiting.");
	    exit(1);
	}

	snprintf(config_file, len, _PATH_PIMD_CONF, ident);
    }

    if (!pid_file) {
	size_t len = strlen(_PATH_PIMD_RUNDIR) + strlen(ident) + 6;

	pid_file = calloc(1, len);
	if (!pid_file) {
	    logit(LOG_ERR, errno, "Failed allocating memory, exiting.");
	    exit(1);
	}

	snprintf(pid_file, len, _PATH_PIMD_PID, ident);
    }

    return 0;
}

int usage(int code)
{
    char buf[768];

    compose_paths();

    printf("Usage:\n"
	   "  %s [-hnsv] [-d SYS[,SYS]] [-f FILE] [-l LVL] [-p FILE] [-u FILE] [-w SEC]\n"
	   "\n"
	   "Options:\n"
	   "  -d SYS    Enable debug of subsystem(s)\n"
	   "  -f FILE   Configuration file, default: %s\n"
	   "  -h        This help text\n"
	   "  -i NAME   Identity for config + PID file, and syslog, default: %s\n"
	   "  -l LVL    Set log level: none, err, notice (default), info, debug\n"
	   "  -n        Run in foreground, do not detach from calling terminal\n"
	   "  -p FILE   Override PID file, default is based on identity, -i\n"
	   "  -s        Use syslog, default unless running in foreground, -n\n"
	   "  -u FILE   Override UNIX domain socket, default based on identity, -i\n"
	   "  -v        Show program version and support information\n"
	   "  -w SEC    Initial startup delay before probing interfaces\n",
	   prognm, config_file, prognm);

    fprintf(stderr, "\nAvailable subystems for debug:\n");
    if (!debug_list(DEBUG_ALL, buf, sizeof(buf))) {
	char line[82] = "  ";
	char *ptr;

	ptr = strtok(buf, " ");
	while (ptr) {
	    char *sys = ptr;

	    ptr = strtok(NULL, " ");

	    /* Flush line */
	    if (strlen(line) + strlen(sys) + 3 >= sizeof(line)) {
		puts(line);
		strlcpy(line, "  ", sizeof(line));
	    }

	    if (ptr)
		snprintf(buf, sizeof(buf), "%s ", sys);
	    else
		snprintf(buf, sizeof(buf), "%s", sys);

	    strlcat(line, buf, sizeof(line));
	}

	puts(line);
    }

    free(config_file);

    return code;
}

static char *progname(char *arg0)
{
       char *nm;

       nm = strrchr(arg0, '/');
       if (nm)
	       nm++;
       else
	       nm = arg0;

       return nm;
}

int
main(argc, argv)
    int argc;
    char *argv[];
{	
    struct timeval tv, difftime, curtime, lasttime, *timeout;
    int dummy, dummysigalrm;
    int startup_delay = 0;
    fd_set rfds, readers;
    int nfds, n, i, secs;
    struct sigaction sa;
    struct debugname *d;
    char ch;
    int rc;

    setlinebuf(stderr);
    snprintf(versionstring, sizeof(versionstring), "%s version %s", PACKAGE_NAME, PACKAGE_VERSION);

    prognm = ident = progname(argv[0]);
    while ((ch = getopt(argc, argv, "d:f:h?i:l:np:su:vw:")) != EOF) {
	switch (ch) {
	case 'd':
	    rc = debug_parse(optarg);
	    if (rc == (int)DEBUG_PARSE_FAIL)
		return usage(1);
	    debug = rc;
	    break;

	case 'f':
	    config_file = strdup(optarg);
	    break;

	case '?':
	case 'h':
	    return usage(0);

	case 'i':
	    ident = optarg;
	    break;

	case 'l':
	    rc = log_str2lvl(optarg);
	    if (rc == -1)
		return usage(1);
	    log_level = rc;
	    break;

	case 'n':
	    foreground = 1;
	    if (log_syslog > 0)
		log_syslog--;
	    break;

	case 'p':
		pid_file = strdup(optarg);
		break;

	case 's':
	    if (log_syslog == 0)
		log_syslog++;
	    break;

	case 'u':
		sock_file = strdup(optarg);
		break;

	case 'v':
	    printf("%s\n", versionstring);
	    printf("\n"
		   "Bug report address: %s\n", PACKAGE_BUGREPORT);
#ifdef PACKAGE_URL
	    printf("Project homepage:   %s\n", PACKAGE_URL);
#endif
	    return 0;

	case 'w':
	    startup_delay = atoi(optarg);
	    break;

	default:
	    return usage(1);
	}
    }

    argc -= optind;
    if (argc > 0)
	return usage(1);

    compose_paths();

    if (debug) {
	char buf[350];

	debug_list(debug, buf, sizeof(buf));
	printf("debug level 0x%lx (%s)\n", debug, buf);
    }

    if (geteuid() != 0) {
	fprintf(stderr, "%s: must be root\n", PACKAGE_NAME);
	exit(1);
    }

    if (!foreground) {
	/* Detach from the terminal */
#ifdef TIOCNOTTY
      int t;
#endif /* TIOCNOTTY */

	if (fork())
	    exit(0);
	close(0);
	close(1);
	close(2);
	(void)open("/", 0);
	dup2(0, 1);
	dup2(0, 2);
#if defined(SYSV) || defined(__linux__)
	setpgrp();
#else
#ifdef TIOCNOTTY
	t = open("/dev/tty", 2);
	if (t >= 0) {
	    (void)ioctl(t, TIOCNOTTY, NULL);
	    close(t);
	}
#else
	if (setsid() < 0)
	    perror("setsid");
#endif /* TIOCNOTTY */
#endif /* SYSV */
    } /* End of child process code */

    if (log_syslog) {
	openlog(prognm, LOG_PID, LOG_DAEMON);
	setlogmask(LOG_UPTO(log_level));
    }

    logit(LOG_NOTICE, 0, "%s starting", versionstring);
    
    /* TODO: XXX: use a combination of time and hostid to initialize the
     * random generator.
     */
    srand48(time(NULL));
    
    /* Start up the log rate-limiter */
    resetlogging(NULL);

    callout_init();
    init_igmp();
    init_pim();
    init_routesock(); /* Both for Linux netlink and BSD routing socket */
    init_pim_mrt();
    init_timers();
    ipc_init(sock_file);

    if (startup_delay) {
	logit(LOG_NOTICE, 0, "Delaying interface probe %d sec ...", startup_delay);
	sleep(startup_delay);
    }

    /* TODO: check the kernel DVMRP/MROUTED/PIM support version */
    
    init_vifs();
    
#ifdef RSRR
    rsrr_init();
#endif /* RSRR */

    sa.sa_handler = handler;
    sa.sa_flags = 0;	/* Interrupt system calls */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    
    FD_ZERO(&readers);
    FD_SET(igmp_socket, &readers);
    nfds = igmp_socket + 1;
    for (i = 0; i < nhandlers; i++) {
	FD_SET(ihandlers[i].fd, &readers);
	if (ihandlers[i].fd >= nfds)
	    nfds = ihandlers[i].fd + 1;
    }
    
    /* schedule first timer interrupt */
    timer_setTimer(TIMER_INTERVAL, timer, NULL);
    pidfile();

    /*
     * Main receive loop.
     */
    dummy = 0;
    dummysigalrm = SIGALRM;
    difftime.tv_usec = 0;
    gettimeofday(&curtime, NULL);
    lasttime = curtime;
    for(;;) {
	bcopy((char *)&readers, (char *)&rfds, sizeof(rfds));
	secs = timer_nextTimer();
	if (secs == -1)
	    timeout = NULL;
	else {
	   timeout = &tv;
	   timeout->tv_sec = secs;
	   timeout->tv_usec = 0;
        }
	
	if (sighandled) {
	    if (sighandled & GOT_SIGINT) {
		sighandled &= ~GOT_SIGINT;
		break;
	    }
	    if (sighandled & GOT_SIGHUP) {
		sighandled &= ~GOT_SIGHUP;
		restart(SIGHUP);
	    }
	    if (sighandled & GOT_SIGALRM) {
		sighandled &= ~GOT_SIGALRM;
		timer(&dummysigalrm);
	    }
	}
	if ((n = select(nfds, &rfds, NULL, NULL, timeout)) < 0) {
	    if (errno != EINTR) /* SIGALRM is expected */
		logit(LOG_WARNING, errno, "select failed");
	    continue;
	}

	/*
	 * Handle timeout queue.
	 *
	 * If select + packet processing took more than 1 second,
	 * or if there is a timeout pending, age the timeout queue.
	 *
	 * If not, collect usec in difftime to make sure that the
	 * time doesn't drift too badly.
	 *
	 * If the timeout handlers took more than 1 second,
	 * age the timeout queue again.  XXX This introduces the
	 * potential for infinite loops!
	 */
	do {
	    /*
	     * If the select timed out, then there's no other
	     * activity to account for and we don't need to
	     * call gettimeofday.
	     */
	    if (n == 0) {
		curtime.tv_sec = lasttime.tv_sec + secs;
		curtime.tv_usec = lasttime.tv_usec;
		n = -1;	/* don't do this next time through the loop */
	    } else
		gettimeofday(&curtime, NULL);
	    difftime.tv_sec = curtime.tv_sec - lasttime.tv_sec;
	    difftime.tv_usec += curtime.tv_usec - lasttime.tv_usec;
#ifdef TIMERDEBUG
	    IF_DEBUG(DEBUG_TIMEOUT)
		logit(LOG_DEBUG, 0, "TIMEOUT: secs %d, diff secs %d, diff usecs %d", secs, difftime.tv_sec, difftime.tv_usec );
#endif
	    while (difftime.tv_usec > 1000000) {
		difftime.tv_sec++;
		difftime.tv_usec -= 1000000;
	    }
	    if (difftime.tv_usec < 0) {
		difftime.tv_sec--;
		difftime.tv_usec += 1000000;
	    }
	    lasttime = curtime;
	    if (secs == 0 || difftime.tv_sec > 0) {
#ifdef TIMERDEBUG
		IF_DEBUG(DEBUG_TIMEOUT)
		    logit(LOG_DEBUG, 0, "\taging callouts: secs %d, diff secs %d, diff usecs %d", secs, difftime.tv_sec, difftime.tv_usec );
#endif
		age_callout_queue(difftime.tv_sec);
	    }
	    secs = -1;
	} while (difftime.tv_sec > 0);

	/* Handle sockets */
	if (n > 0) {
	    /* TODO: shall check first igmp_socket for better performance? */
	    for (i = 0; i < nhandlers; i++) {
		if (FD_ISSET(ihandlers[i].fd, &rfds)) {
		    (*ihandlers[i].func)(ihandlers[i].fd, &rfds);
		}
	    }
	}
    
    } /* Main loop */

    logit(LOG_NOTICE, 0, "%s exiting", versionstring);
    cleanup();
    free(config_file);

    return 0;
}

/*
 * The 'virtual_time' variable is initialized to a value that will cause the
 * first invocation of timer() to send a probe or route report to all vifs
 * and send group membership queries to all subnets for which this router is
 * querier.  This first invocation occurs approximately TIMER_INTERVAL seconds
 * after the router starts up.   Note that probes for neighbors and queries
 * for group memberships are also sent at start-up time, as part of initial-
 * ization.  This repetition after a short interval is desirable for quickly
 * building up topology and membership information in the presence of possible
 * packet loss.
 *
 * 'virtual_time' advances at a rate that is only a crude approximation of
 * real time, because it does not take into account any time spent processing,
 * and because the timer intervals are sometimes shrunk by a random amount to
 * avoid unwanted synchronization with other routers.
 */

u_long virtual_time = 0;

/*
 * Timer routine. Performs all perodic functions:
 * aging interfaces, quering neighbors and members, etc... The granularity
 * is equal to TIMER_INTERVAL.
 */
static void 
timer(i)
    void *i;
{
    age_vifs();	        /* Timeout neighbors and groups         */
    age_routes();  	/* Timeout routing entries              */
    
    virtual_time += TIMER_INTERVAL;
    timer_setTimer(TIMER_INTERVAL, timer, NULL);
}	

/*
 * Performs all necessary functions to quit gracefully
 */
/* TODO: implement all necessary stuff */
static void
cleanup()
{
#ifdef RSRR
    rsrr_clean();
#endif

    free_all_callouts();
    stop_all_vifs();
    k_stop_pim(igmp_socket);

    igmp_clean();
    pim_clean();
    mrt_clean();
    ipc_exit();

    /*
     * When IOCTL_OK_ON_RAW_SOCKET is defined, 'udp_socket' is equal
     * 'to igmp_socket'. Therefore, 'udp_socket' should be closed only
     * if they are different.
     */
#ifndef IOCTL_OK_ON_RAW_SOCKET
    if (udp_socket > 0)
	close(udp_socket);
    udp_socket = 0;
#endif

    /* Both for Linux netlink and BSD routing socket */
    routesock_clean();

    /* No more socket callbacks */
    nhandlers = 0;
}


/*
 * Signal handler.  Take note of the fact that the signal arrived
 * so that the main loop can take care of it.
 */
static void
handler(sig)
    int sig;
{
    switch (sig) {
    case SIGALRM:
	sighandled |= GOT_SIGALRM;
	break;

    case SIGINT:
    case SIGTERM:
	sighandled |= GOT_SIGINT;
	break;
	
    case SIGHUP:
	sighandled |= GOT_SIGHUP;
	break;
    }
}


/* TODO: not verified */
/* PIMDM TODO */
/*
 * Restart the daemon
 */
static void
restart(i)
    int i;
{
    logit(LOG_NOTICE, 0, "%s restart", versionstring);
    
    /*
     * reset all the entries
     */
    cleanup();

    /* TODO: delete?
       free_all_routes();
       */

    /*
     * start processing again
     */
    init_igmp();
    init_pim();
    init_routesock();
    init_pim_mrt();
    init_vifs();
    ipc_init(sock_file);

#ifdef RSRR
    rsrr_init();
#endif /* RSRR */

    /* schedule timer interrupts */
    timer_setTimer(TIMER_INTERVAL, timer, NULL);
}


int
daemon_restart(char *buf, size_t len)
{
    (void)buf;
    (void)len;
    restart(1);

    return 0;
}

int
daemon_kill(char *buf, size_t len)
{
    (void)buf;
    (void)len;
    handler(SIGTERM);

    return 0;
}


static void
resetlogging(arg)
    void *arg;
{
    int nxttime = 60;
    void *narg = NULL;
    
    if (arg == NULL && log_nmsgs > LOG_MAX_MSGS) {
	nxttime = LOG_SHUT_UP;
	narg = (void *)&log_nmsgs;	/* just need some valid void * */
	syslog(LOG_WARNING, "logging too fast, shutting up for %d minutes",
	       LOG_SHUT_UP / 60);
    } else {
	log_nmsgs = 0;
    }
    
    timer_setTimer(nxttime, resetlogging, narg);
}
