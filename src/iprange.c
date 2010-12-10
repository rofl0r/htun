/* -------------------------------------------------------------------------
 * iprange.c - htun functions for manipulating IP range strings & lists
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
/* $Id: iprange.c,v 2.10 2002/08/16 01:49:47 jehsom Exp $ */

#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#undef __EI
#include "iprange.h"
#include "common.h"
#include "log.h"

iprange_t *make_iprange( char *str ) {
    iprange_t *ret;
    char *cp = strchr(str,'/');

    if( (ret=malloc(sizeof(iprange_t))) == NULL ) {
        lprintf(log, ERROR, "unable to malloc!");
        return NULL;
    }

    if( !cp ) goto cleanup;

    *cp++ = '\0';

    ret->net.s_addr = inet_addr(str);
    if( ret->net.s_addr == (unsigned long) -1 ) goto cleanup;

    ret->maskbits = strtoul(cp, NULL, 0);
    if( ret->maskbits == 0 && strcmp(cp,"0") != 0 ) goto cleanup;
    if( ret->maskbits > 32 ) goto cleanup;

    ret->net.s_addr = htonl( ntohl(ret->net.s_addr) & 
                             (0xFFFFFFFF << (32 - ret->maskbits)) );
    
    ret->next = NULL;

    dprintf(log, DEBUG, "Successful: '%s' -> '%s/%d'", str,
            inet_ntoa(ret->net), ret->maskbits);
    return ret;

cleanup:
    dprintf(log, DEBUG, "cleaning up after error");
    free(ret);
    return NULL;
}

/*
 * Adds the passed-in ip range string to the list whose head pointer is
 * pointed to by listp. Uses make_iprange() to convert str to an iprange_t.
 * Returns NULL on error or a pointer to the new iprange_t on success.
 */
iprange_t *add_iprange( iprange_t **listp, char *str ) {
    iprange_t *new;

    if( (new=make_iprange(str)) == NULL ) return NULL;

    while( *listp ) listp = &(*listp)->next;

    *listp = new;
    return new;
}

/*
 * Removes the iprange_t pointed to by *listp from the list it's in.
 */
void remove_iprange( iprange_t **listp ) {
    iprange_t *tmp;

    if( !listp || !(*listp) ) {
        lprintf(log, WARN,
                "attempt to remove null iprange list item");
        return;
    }

    tmp = *listp;
    *listp = tmp->next;
    free(tmp);
    return;
}

/*
 * Frees an iprange_t list by repeatedly calling remove_iprange() on the head.
 */
void free_iprange_list( iprange_t **listp ) {
    while( *listp ) remove_iprange(listp);
    return;
}

