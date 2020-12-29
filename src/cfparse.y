%{
/*
 * Configuration file parser for pimd-dense, based on the original from
 * mrouted, by Bill Fenner (1994)
 */
#include <stdio.h>
#include <stdarg.h>
#include <netdb.h>
#include <ifaddrs.h>

#include "defs.h"

/*
 * Local function declarations
 */
static void        fatal(const char *fmt, ...);
static void        warn(const char *fmt, ...);
static void        yyerror(char *s);
static char       *next_word(void);
static int         yylex(void);
int                yyparse(void);

static FILE *fp;

static int lineno;

static struct uvif *v;
static struct uvif scrap;

struct addrmask {
	uint32_t addr;
	int      mask;
};
%}

%union
{
	int              num;
	char            *ptr;
	struct addrmask  addrmask;
	uint32_t         addr;
};

%token DEFAULT_ROUTE_PREF DEFAULT_ROUTE_METRIC
%token NO
%token PHYINT
%token DISABLE ENABLE IGMPV1 IGMPV2 IGMPV3
%token DISTANCE METRIC THRESHOLD NETMASK
%token <num> BOOLEAN
%token <num> NUMBER
%token <ptr> STRING
%token <addrmask> ADDRMASK
%token <addr> ADDR

%type <addr> interface addrname

%start conf

%%

conf	: stmts
	;

stmts	: /* Empty */
	| stmts stmt
	;

stmt	: error
	| DEFAULT_ROUTE_PREF NUMBER
	{
	    if ($2 < 1 || $2 > 255)
		fatal("Invalid default-route-distance (1-255)");
	    default_route_distance = $2;
	}
	| DEFAULT_ROUTE_METRIC NUMBER
	{
	    if ($2 < 1 || $2 > 1024)
		fatal("Invalid default-route-metric (1-1024)");
	    default_route_metric = $2;
	}
	| NO PHYINT		{ config_set_ifflag(VIFF_DISABLED); }
	| PHYINT interface
	{
	    v = config_find_ifaddr($2);
	    if (!v) {
		warn("phyint %s not available, continuing ...", inet_fmt($2, s1));
		v = &scrap;
	    }
	}
	ifmods
	;

ifmods	: /* empty */
	| ifmods ifmod
	;

ifmod	: mod
	| DISABLE		{ v->uv_flags |= VIFF_DISABLED; }
	| ENABLE		{ v->uv_flags &= ~VIFF_DISABLED; }
	| IGMPV1		{ v->uv_flags &= ~VIFF_IGMPV2; v->uv_flags |= VIFF_IGMPV1; }
	| IGMPV2		{ v->uv_flags &= ~VIFF_IGMPV1; v->uv_flags |= VIFF_IGMPV2; }
	| IGMPV3		{ v->uv_flags &= ~VIFF_IGMPV1; v->uv_flags &= ~VIFF_IGMPV2; }
	| NETMASK addrname
	{
	    uint32_t subnet, mask;

	    mask = $2;
	    subnet = v->uv_lcl_addr & mask;
	    if (!inet_valid_subnet(subnet, mask))
		fatal("Invalid netmask");
	    v->uv_subnet = subnet;
	    v->uv_subnetmask = mask;
	    v->uv_subnetbcast = subnet | ~mask;
	}
	| NETMASK
	{
	    warn("Expected address after netmask keyword, ignored");
	}
	;

mod	: THRESHOLD NUMBER
	{
	    if ($2 < 1 || $2 > 255)
		fatal("Invalid threshold %d",$2);
	    v->uv_threshold = $2;
	}
	| THRESHOLD
	{
	    warn("Expected number after threshold keyword, ignored");
	}
	| DISTANCE NUMBER
	{
	    if ($2 < 1 || $2 > 255)
		fatal("Invalid distance %d",$2);
	    v->uv_local_pref = $2;
	}
	| DISTANCE
	{
	    warn("Expected number after distance keyword, ignored");
	}
	| METRIC NUMBER
	{
	    if ($2 < 1 || $2 > 1024)
		fatal("Invalid metric %d",$2);
	    v->uv_metric = $2;
	}
	| METRIC
	{
	    warn("Expected number after metric keyword, ignored");
	}
	;

interface: ADDR
	{
	    $$ = $1;
	}
	| STRING
	{
	    struct uvif *v;

	    /*
	     * Looks a little weird, but the orig. code was based around
	     * the addresses being used to identify interfaces.
	     */
	    v = config_find_ifname($1);
	    if (!v) {
		warn("phyint %s not available, continuing ...", $1);
		$$ = 0;
	    } else
		$$ = v->uv_lcl_addr;
	}
	;

addrname: ADDR
	{
	    $$ = $1;
	}
	| STRING
	{
	    struct sockaddr_in *sin;
	    struct addrinfo *result;
	    struct addrinfo hints;
	    int rc;

	    memset(&hints, 0, sizeof(struct addrinfo));
	    hints.ai_family = AF_INET;
	    hints.ai_socktype = SOCK_DGRAM;

	    rc = getaddrinfo($1, NULL, &hints, &result);
	    if (rc) {
		fatal("No such host %s", $1);
		return 0;	/* Never reached */
	    }

	    if (result->ai_next)
		fatal("Hostname %s does not %s", $1, "map to a unique address");

	    sin = (struct sockaddr_in *)result->ai_addr;
	    $$  = sin->sin_addr.s_addr;

	    freeaddrinfo(result);
	}
	;
%%

static void fatal(const char *fmt, ...)
{
    va_list ap;
    char buf[MAXHOSTNAMELEN + 100];

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    logit(LOG_ERR, 0, "%s:%d: %s", config_file, lineno, buf);
}

static void warn(const char *fmt, ...)
{
    va_list ap;
    char buf[200];

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    logit(LOG_WARNING, 0, "%s:%d: %s", config_file, lineno, buf);
}

static void yyerror(char *msg)
{
    logit(LOG_ERR, 0, "%s:%d: %s", config_file, lineno, msg);
}

static char *next_word(void)
{
    static char buf[1024];
    static char *p = NULL;
    char *q;

    while (1) {
	if (!p || !*p) {
	    lineno++;
	    if (fgets(buf, sizeof(buf), fp) == NULL)
		return NULL;
	    p = buf;
	}

	/* skip whitespace */
	while (*p && (*p == ' ' || *p == '\t'))
	    p++;

	if (*p == '#') {
	    p = NULL;		/* skip comments */
	    continue;
	}

	q = p;
	while (*p && *p != ' ' && *p != '\t' && *p != '\n')
	    p++;		/* find next whitespace */
	*p++ = '\0';		/* null-terminate string */

	if (!*q) {
	    p = NULL;
	    continue;		/* if 0-length string, read another line */
	}

	return q;
    }
}

/*
 * List of keywords.  Must have an empty record at the end to terminate
 * list.  If a second value is specified, the first is used at the beginning
 * of the file and the second is used while parsing interfaces (e.g. after
 * the first "phyint" or "tunnel" keyword).
 */
static struct keyword {
    char  *word;
    int    val;
} words[] = {
    { "default-route-distance", DEFAULT_ROUTE_PREF   },
    { "default-route-metric",   DEFAULT_ROUTE_METRIC },
    { "no",                     NO },
    { "phyint",                 PHYINT },
    { "disable",                DISABLE },
    { "enable",                 ENABLE },
    { "distance",               DISTANCE },
    { "metric",                 METRIC },
    { "threshold",              THRESHOLD },
    { "netmask",                NETMASK },
    { "igmpv1",                 IGMPV1 },
    { "igmpv2",                 IGMPV2 },
    { "igmpv2",                 IGMPV3 },
    { NULL,                     0 }
};


static int yylex(void)
{
    struct keyword *w;
    uint32_t addr, n;
    char *q;

    q = next_word();
    if (!q)
	return 0;

    for (w = words; w->word; w++) {
	if (!strcmp(q, w->word))
	    return w->val;
    }

    if (!strcmp(q, "on") || !strcmp(q, "yes")) {
	yylval.num = 1;
	return BOOLEAN;
    }

    if (!strcmp(q, "off") || !strcmp(q, "no")) {
	yylval.num = 0;
	return BOOLEAN;
    }

    if (!strcmp(q, "default")) {
	yylval.addrmask.mask = 0;
	yylval.addrmask.addr = 0;
	return ADDRMASK;
    }

    if (sscanf(q,"%[.0-9]/%u%c", s1, &n, s2) == 2) {
	addr = inet_parse(s1, 1);
	if (addr != 0xffffffff) {
	    yylval.addrmask.mask = n;
	    yylval.addrmask.addr = addr;
	    return ADDRMASK;
	}
	/* fall through to returning STRING */
    }

    if (sscanf(q,"%[.0-9]%c", s1, s2) == 1) {
	addr = inet_parse(s1, 4);
	if (addr != 0xffffffff && inet_valid_host(addr)) {
	    yylval.addr = addr;
	    return ADDR;
	}
    }

    if (sscanf(q,"0x%8x%c", &n, s1) == 1) {
	yylval.addr = n;
	return ADDR;
    }

    if (sscanf(q,"%u%c", &n, s1) == 1) {
	yylval.num = n;
	return NUMBER;
    }

    yylval.ptr = q;

    return STRING;
}

void config_vifs_from_file(void)
{
    lineno = 0;

    fp = fopen(config_file, "r");
    if (!fp) {
	if (errno != ENOENT)
	    logit(LOG_ERR, errno, "Cannot open %s", config_file);
	return;
    }

    yyparse();

    fclose(fp);
}

/**
 * Local Variables:
 *  c-file-style: "cc-mode"
 * End:
 */
