/* $Id: pim_var.h,v 1.1 1998/06/01 22:29:43 kurtw Exp $ */

#ifndef _NETINET_PIM_VAR_H_
#define _NETINET_PIM_VAR_H_

/*
 * Protocol Independent Multicast (PIM),
 * implementation-specific definitions.
 *
 * Written by George Edmond Eddy (Rusty), ISI, February 1998
 * Modified by Pavlin Ivanov Radoslavov, USC/ISI, May 1998
 */

struct pimstat {
    u_int	pim_rcv_total;		/* total PIM messages received	*/
    u_int	pim_rcv_tooshort;	/* received with too few bytes	*/
    u_int	pim_rcv_badsum;		/* received with bad checksum	*/
    u_int	pim_rcv_badversion;	/* received bad PIM version	*/
    u_int	pim_rcv_registers;	/* received registers		*/
    u_int	pim_rcv_badregisters;	/* received invalid registers	*/
    u_int	pim_snd_registers;	/* sent registers		*/
};

#if (defined(KERNEL)) || (defined(_KERNEL))
extern struct pimstat pimstat;
#ifndef __P
#ifdef __STDC__
#define __P(x)  x
#else
#define __P(x)  ()
#endif
#endif

#ifdef NetBSD
void pim_input __P((struct mbuf *, ...));
#else
void pim_input __P((struct mbuf *, int));
#endif
#endif /* KERNEL */

/*
 * Names for PIM sysctl objects
 */
#if (defined(__FreeBSD__)) || (defined(NetBSD))
#define PIMCTL_STATS		1	/* statistics (read-only) */
#define PIMCTL_MAXID		2

#define PIMCTL_NAMES { \
	{ 0, 0 }, \
	{ "stats", CTLTYPE_STRUCT }, \
}
#endif /* FreeBSD || NetBSD */

#endif /* _NETINET_PIM_VAR_H_ */
