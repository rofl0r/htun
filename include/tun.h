/*
 *  Universal TUN/TAP device driver.
 *  Copyright (C) 1999-2000 Maxim Krasnyansky <max_mk@yahoo.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 */

/*
 *  Modified for use with htun by Moshe Jacobson <moshe@runslinux.net>
 *  and Ola Nordström <ola@triblock.com>
 *
 *  $Id: tun.h,v 2.15 2002/04/28 05:29:07 jehsom Exp $
 */

#ifndef __IF_TUN_H
#define __IF_TUN_H

#include <net/if.h>
#include <netinet/in.h>
#include "clidata.h"

/* Read queue size */
#define TUN_READQ_SIZE	10

/* TUN device flags */
#define TUN_TUN_DEV 	0x0001	
#define TUN_TAP_DEV	0x0002
#define TUN_TYPE_MASK   0x000f

#define TUN_FASYNC	0x0010
#define TUN_NOCHECKSUM	0x0020
#define TUN_NO_PI	0x0040
#define TUN_ONE_QUEUE	0x0080
#define TUN_PERSIST 	0x0100	

/* Ioctl defines */
#define TUNSETNOCSUM  _IOW('T', 200, int) 
#define TUNSETDEBUG   _IOW('T', 201, int) 
#define TUNSETIFF     _IOW('T', 202, int) 
#define TUNSETPERSIST _IOW('T', 203, int) 
#define TUNSETOWNER   _IOW('T', 204, int)

/* TUNSETIFF ifr flags */
#define IFF_TUN		0x0001
#define IFF_TAP		0x0002
#define IFF_NO_PI	0x1000
#define IFF_ONE_QUEUE	0x2000

struct tun_pi {
	unsigned short flags;
	unsigned short proto;
};
#define TUN_PKT_STRIP	0x0001

/* 
 *  * Allocates a tun device based on the input set of acceptable ip ranges.
 * Simply attempts to bring up the interface with successive IPs until it
 * succeeds. Then attempts to assign peer addresses with successive IPs until
 * that succeeds.  On success, returns 0 and sets the following variables:
 *      clidata->tunfd
 *      clidata->cliaddr
 *      clidata->srvaddr
 * On failure, returns -1
 */
int srv_tun_alloc( clidata_t *clidata, clidata_list_t *clients ); 

/*
 * Stores the default gateway information for later undoing by
 * restore_default_gw().
 */
int store_default_gw(void);

/*
 * sets routing table's default gateway to the sock peer addrress
 */
int set_default_gw( int sockfd );

/*
 * Restores the default route to what it was before set_default_gw() was
 * called.
 */
int restore_default_gw(void);

/*
 * fetches the mac addr
 * returns a pointer to a static buffer of the mac addr
 */
char *get_mac( char *ifname );

/*
 * sets up the tun dev according to the specidied
 * local and peer ip
 */
int cli_tun_alloc(struct in_addr local, struct in_addr peer);


#endif /* __IF_TUN_H */
