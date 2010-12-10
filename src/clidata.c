/* -------------------------------------------------------------------------
 * clidata.c - htun functions for manipulating server-side client data
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
/* $Id: clidata.c,v 2.19 2002/08/16 01:49:47 jehsom Exp $ */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#undef __EI
#include "clidata.h"
#include "util.h"
#include "common.h"
#include "iprange.h"

/*
 * Pass in a MAC address, and get_clidata() returns the data for the client
 * with that MAC address, or NULL if it does not exist.
 * clients is a list of clients (client_t structs)
 * *macaddr is the MAC Addr
 */
clidata_t *get_clidata( clidata_list_t *list, char *macaddr )
{
    clidata_t *c;

    if( !list ) {
        lprintf(log, ERROR, "passed null client list!");
        return NULL;
    }

    c = list->head;

    while( c ) {
        if( !xstrcasecmp(c->macaddr, macaddr) ) return c;
        c = c->next;
    }

    return NULL;
}

/*
 * Pass in a MAC address, and add_clidata() creates a new client data object
 * in the list of client data. On success, returns a pointer to the new client
 * data object. On failure, returns NULL
 * clients is a list of clients (client_t structs)
 * *macaddr is the MAC Addr
 */
clidata_t *add_clidata( clidata_list_t *list, char *macaddr )
{
    clidata_t *c;

    if( !list ) {
        lprintf(log, ERROR, "passed null client list!");
        return NULL;
    }

    if( (c=calloc(1,sizeof(clidata_t))) == NULL ) {
        lprintf(log, WARN, "malloc failure\n");
        return NULL;
    }

    strncpy(c->macaddr, macaddr, 13);
    c->tunfd = -1;
    c->chan1 = -1;
    c->chan2 = -1;

    pthread_mutex_lock(&list->lock);
    c->next = list->head;
    list->head = c;
    pthread_mutex_unlock(&list->lock);

    return c;
}

/*
 * Takes a pointer-pointer to a clidata_t and deletes the clidata_t from the
 * list it's in. free()s the clidata_t after it has been removed.
 * eg in list A B C, If B is to be deleted then pass in &A->next
 */
void remove_clidata( clidata_list_t *list, char *macaddr )
{
    clidata_t *tmp;
    clidata_t **cp;
    
    if( !list ) {
        lprintf(log, ERROR, "passed null client list!");
        return;
    }

    cp = &list->head;
    while( *cp ) {
        if( !xstrcasecmp(macaddr, (*cp)->macaddr) ) break;
        cp = &(*cp)->next;
    }

    if( !*cp ) return;

    lprintf(log, INFO, "Removing client data for MAC addr %s.", macaddr);

    pthread_mutex_lock(&list->lock);
    tmp = *cp;
    *cp = (*cp)->next;
    pthread_mutex_unlock(&list->lock);

    if( tmp->chan1 != -1 ) {
        dprintf(log, DEBUG, "closing chan1 (fd #%d)", 
                tmp->chan1);
        close(tmp->chan1); 
        tmp->chan1 = -1;
    }
    if( tmp->chan2 != -1 ) {
        dprintf(log, DEBUG, "closing chan2 (fd #%d)",
                tmp->chan2);
        close(tmp->chan2); 
        tmp->chan2 = -1;
    }
    if( tmp->tunfd != -1 ) {
        dprintf(log, DEBUG, "closing tunfd #%d", tmp->tunfd);
        close(tmp->tunfd);
        tmp->tunfd = -1;
    }
    dprintf(log, DEBUG, "destroying sendq");
    if( tmp->sendq ) q_destroy(&tmp->sendq);
    dprintf(log, DEBUG, "destroying recvq");
    if( tmp->recvq ) q_destroy(&tmp->recvq);
    dprintf(log, DEBUG, "freeing iprange list");
    free_iprange_list(&tmp->iprange);
    dprintf(log, DEBUG, "freeing clidata struct itself");
    free(tmp);
    dprintf(log, DEBUG, "returning");
    return;
}
    
clidata_list_t *new_clidata_list( void )
{
    clidata_list_t *rc = calloc(1, sizeof(clidata_list_t));

    if( !rc ) return NULL;

    pthread_mutex_init(&rc->lock, NULL);
    return rc;
}

/* 
 * Free()s a clidata_t list by calling remove_clidata() on the head node until
 * there is no more to free.
 */
void free_clidata_list( clidata_list_t **listp )
{
    clidata_t *c;

    if( !listp || !*listp ) {
        lprintf(log, ERROR, "passed null client list!");
        return;
    }

    c = (*listp)->head;

    while( (*listp)->head ) remove_clidata(*listp, (*listp)->head->macaddr);
    free(*listp);
    *listp = NULL;
    return;
}

/*
 * Takes a pointer to the head pointer of a clidata list, and prunes any
 * client objects whose recvfd and sendfd are both -1, and that have not been
 * accessed for a long time (configurable value).
 */
void prune_clidata_list( clidata_list_t *list )
{
    clidata_t *c;
    clidata_t *next;

    if( !list ) {
        lprintf(log, ERROR, "passed null client list!");
        return;
    }

    c = list->head;

    while( c ) {
        next = c->next;
        dprintf(log, DEBUG, "considering %s, next=%lu",
                c->macaddr, next);
        if( c->chan1 == -1 && c->chan2 == -1 && 
            c->lastuse < time(NULL) - config->u.s.clidata_timeout ) {
            remove_clidata(list, c->macaddr);
        }
        c = next;
    }
    dprintf(log, DEBUG, "returning");
    return;
}

