/* -------------------------------------------------------------------------
 * clidata.h - htun functions for manipulating server-side client data
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
/* $Id: clidata.h,v 2.7 2002/04/28 05:29:07 jehsom Exp $ */

#ifndef __CLIDATA_H
#define __CLIDATA_H

#include "iprange.h"
#include "queue.h"

#ifdef __EI
#undef __EI
#endif
#define __EI extern __inline__

#define CLIDATA_STALE_SECS 600

typedef struct _clidata {
    char macaddr[13];
    struct in_addr cliaddr;
    struct in_addr srvaddr;
    int tunfd;
    pthread_t writer;
    pthread_t reader;
    int chan1;
    int chan2;
    time_t lastuse;
    queue_t *sendq;
    queue_t *recvq;
    iprange_t *iprange;
    struct _clidata *next;
    struct _clidata *prev;
} clidata_t;

typedef struct _clidata_list_t {
    clidata_t *head;
    pthread_mutex_t lock;
} clidata_list_t;

/*
 * Determines whether an IP address has been used yet
 */
__EI
int ip_used( clidata_list_t *list, struct in_addr ip ) {
    clidata_t *c;
    for( c = list->head; c; c = c->next ) {
        if( ip.s_addr == c->cliaddr.s_addr ) return 1;
        if( ip.s_addr == c->srvaddr.s_addr ) return 1;
    }
    return 0;
}


/*
 * Pass in a MAC address, and get_clidata() returns the data for the client
 * with that MAC address, or NULL if it does not exist.
 */
clidata_t *get_clidata( clidata_list_t *list, char *macaddr );

/*
 * Pass in a MAC address, and add_clidata() creates a new client data object
 * in the list of client data. On success, returns a pointer to the new client
 * data object. On failure, returns NULL
 */
clidata_t *add_clidata( clidata_list_t *list, char *macaddr );

/*
 * Takes a pointer-pointer to a clidata_t and deletes the clidata_t from the
 * list it's in. free()s the clidata_t after it has been removed.
 */
void remove_clidata( clidata_list_t *list, char *macaddr );

/*
 * Malloc()s a new clidata_list_t and returns it
 */
clidata_list_t *new_clidata_list( void );

/* 
 * Free()s a clidata_t list by calling remove_clidata() on the head node until
 * there is no more to free.
 */
void free_clidata_list( clidata_list_t **listp );

/*
 * Takes a pointer to the head pointer of a clidata list, and prunes any
 * client objects whose recvfd and sendfd are both -1, and that have not been
 * accessed for a long time (configurable value).
 */
void prune_clidata_list( clidata_list_t *list );

#endif
