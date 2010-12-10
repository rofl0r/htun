/* -------------------------------------------------------------------------
 * iprange.h - htun functions for manipulating IP range strings & lists
 * Copyright (C) 2002 Moshe Jacobson <moshe@runslinux.net>, 
 *                    Ola Nordström <ola@triblock.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * -------------------------------------------------------------------------
 */
/* $Id: iprange.h,v 2.3 2002/04/28 05:29:07 jehsom Exp $ */

#ifndef __IPRANGE_H
#define __IPRANGE_H

#include <arpa/inet.h>

#ifdef __EI
#undef __EI
#endif
#define __EI extern __inline__

typedef struct _iprange {
    struct in_addr net;
    unsigned maskbits : 6;
    struct _iprange *next;
} iprange_t;

/*
 * Returns an iprange_t dynamically allocated when passed in an IP range of
 * the form xxx.xxx.xxx.xxx/yy (ip network and netmask bits)
 */
iprange_t *make_iprange( char *str );

/* 
 * Returns 1 if the given IP is within the range given by ranges,
 * 0 otherwise. DOES NOT RECURSE THROUGH THE LINKED LIST!!
 */
__EI
int ip_ok( iprange_t *range, struct in_addr *ip ) {
    if( (ntohl(ip->s_addr) & (0xFFFFFFFF<<(32-range->maskbits))) ==
        (ntohl(range->net.s_addr) & (0xFFFFFFFF<<(32-range->maskbits))) )
        return 1;
    else return 0;
}
   
/*
 * Takes in a struct in_addr representing the currently tried IP address, and
 * sets it to the next acceptable IP address based on the input list of
 * acceptable ranges. Returns 0 if the input ip address was out of range to
 * begin with, or if there were no more IP addresses to try, 1 on success.
 * The first time this is called, ip->s_addr should be set to 0.
 */
int next_ip( iprange_t *range, struct in_addr *ip );

/*
 * Adds the passed-in ip range string to the list whose head pointer is
 * pointed to by listp. Uses make_iprange() to convert str to an iprange_t.
 * Returns NULL on error or a pointer to the new iprange_t on success.
 */
iprange_t *add_iprange( iprange_t **listp, char *str );

/*
 * Removes the iprange_t pointed to by *listp from the list it's in.
 */
void remove_iprange( iprange_t **listp );

/*
 * Frees an iprange_t list by repeatedly calling remove_iprange() on the head.
 */
void free_iprange_list( iprange_t **listp );


#endif
