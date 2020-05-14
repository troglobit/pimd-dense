/* $Id: pim.h,v 1.1 1998/06/01 22:29:43 kurtw Exp $ */

#ifndef _NETINET_PIM_H_
#define _NETINET_PIM_H_

/*
 * Protocol Independent Multicast (PIM) definitions.
 *
 * Written by Ahmed Helmy, USC/SGI, July 1996
 * Modified by George Edmond Eddy (Rusty), ISI, February 1998
 * Modified by Pavlin Ivanov Radoslavov, USC/ISI, May 1998
 */

/*
 * PIM packet format.
 */
#define PIM_VERSION	2
struct pim {
#if BYTE_ORDER == LITTLE_ENDIAN
    u_char	pim_type:4,		/* type of PIM message            */
		pim_vers:4;		/* PIM version                    */
#else /* BYTE_ORDER == BIG_ENDIAN */
	u_char	pim_vers:4,		/* PIM version                    */
		pim_type:4;		/* type of PIM message            */
#endif /* BYTE_ORDER */
	u_char  reserved;		/* Reserved                       */
	u_short	pim_cksum;		/* IP-style checksum              */
};

#define PIM_MINLEN	8		/* The header min. length is 8    */
#define PIM_REG_MINLEN	(PIM_MINLEN+20)	/* Register message + inner IPheader */

/*
 * Message types
 */
#define PIM_REGISTER	0x01		/* PIM Register type is 1 */
#define PIM_NULL_REGISTER 0x40000000	/* second bit in reg_head is the Null-bit */

#endif /* _NETINET_PIM_H_ */
