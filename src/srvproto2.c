/* -------------------------------------------------------------------------
 * srvproto2.c - htun protocol 2 functions for the server side
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
/* $Id: srvproto2.c,v 1.34 2002/10/31 03:30:58 jehsom Exp $ */


#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

#include "common.h"
#include "log.h"
#include "http.h"
#include "util.h"
#include "clidata.h"
#include "server.h"
#include "tpool.h"
#include "srvproto2.h"
#include "tun.h"
#include "queue.h"

clidata_t *handle_cp( int clisock, char *hdrs, int proto ) {
    char *macaddr;
    iprange_t *ranges=NULL;
    iprange_t **rangep=&ranges;
    char *body;
    char **lines;
    clidata_t *client;
    char buf[CP2_OK_MAXBODY];
    int i;
    int len;

    /* Get body of request. body gets malloc()d data */
    if( (body=getbody(clisock, hdrs, &len)) == NULL ) {
        lprintf(log, WARN, "Client did not send expected amount");
        goto cleanup1;
    }
    dprintf(log, DEBUG, "Got body: %s", body);

    /* Split lines of input. lines get malloc()d data */
    if( (lines=splitlines(body)) == NULL ) {
        lprintf(log, ERROR,
                "Problem splittling lines with splitlines()");
        fdprintf(clisock, RESPONSE_500_ERR);
        goto cleanup2;
    }
    dprintf(log, DEBUG, "split lines successfully.");

    /* Set macaddr based on the first line */
    if( (macaddr=lines[0]) == NULL ) {
        lprintf(log, WARN,
                "Client did not send MAC address line!");
        fdprintf(clisock, RESPONSE_400);
        goto cleanup3;
    }
    chomp(macaddr);
    dprintf(log, DEBUG, "Got macaddr %s.", macaddr);

    /* Interpret the ipranges. make_iprange() ranges gets malloc()d data */
    for( i=1; lines[i]; i++ ) {
        dprintf(log, DEBUG, "About to convert %s", lines[i]);
        if( (*rangep=make_iprange(lines[i])) == NULL ) {
            if( *lines[i] ) {
                lprintf(log, WARN,
                        "Client sent invalid ip range: %s", 
                        lines[i]);
            }
            continue;
        }
        rangep=&(*rangep)->next;
    }

    if( ranges == NULL ) {
        lprintf(log, WARN, "Client sent no ip ranges. Dropping.");
        fdprintf(clisock, RESPONSE_400);
        goto cleanup3;
    }
    dprintf(log, DEBUG,
            "About to get clidata for MAC addr %s.", macaddr);

    /* Get clidata for this client */
    if( (client=get_clidata(clients, macaddr)) == NULL ) {
        dprintf(log, DEBUG,
                "Need to make new clidata for %s.", macaddr);
        if( (client=add_clidata(clients, macaddr)) == NULL ) {
            lprintf(log, WARN,
                    "Could not create clidata! Dropping client.");
            fdprintf(clisock, RESPONSE_500_ERR);
            free_iprange_list(&ranges);
            goto cleanup3;
        }
        client->iprange = ranges;
        client->chan1 = clisock;

        dprintf(log, DEBUG, "About to call srv_tun_alloc()");
        if( srv_tun_alloc(client, clients) == -1 ) {
            fdprintf(clisock, RESPONSE_503);
            goto cleanup4;
        }

        if( proto == 1 ) {
            if( srv_start_tunfile_reader(client) == -1 ) goto cleanup4;
        }
        if( srv_start_tunfile_writer(client) == -1 ) goto cleanup4;

    } else {
        char ip1[16], ip2[16];
        strcpy(ip1, inet_ntoa(client->srvaddr));
        strcpy(ip2, inet_ntoa(client->cliaddr));
        lprintf(log, INFO,
                "Client %s found. localip=%s, peerip=%s.", macaddr, ip1, ip2);

        if( client->chan1 != -1 ) {
            lprintf(log, WARN, 
                "Client chan1 appears to be connected already. Dropping old.");
            close(client->chan1);
            client->chan1 = -1;
        }
        if( client->chan2 != -1 ) {
            lprintf(log, WARN, 
                "Client chan2 appears to be connected already. Dropping old.");
            close(client->chan2);
            client->chan2 = -1;
        }
        if( client->iprange ) free_iprange_list( &client->iprange );
        client->iprange = ranges;
        client->chan1 = clisock;
    }


    dprintf(log, DEBUG, "About to respond to client");

    {
        char ip1[16], ip2[16];

        strcpy(ip1,inet_ntoa(client->cliaddr));
        strcpy(ip2,inet_ntoa(client->srvaddr));
        sprintf(buf, "%s\n%s\n", ip1, ip2);
    }

    fdprintf(clisock, RESPONSE_200, strlen(buf), buf);

    dprintf(log, DEBUG, "Returning");
    return client;

cleanup4:
    remove_clidata(clients, client->macaddr);
cleanup3:
    free(lines);
cleanup2:
    free(body);
cleanup1:
    return NULL;
}


clidata_t *handle_cr( int clisock, char *hdrs ) {
    char *macaddr;
    char *body;
    char **lines;
    clidata_t *client;
    int len;

    if( (body=getbody(clisock, hdrs, &len)) == NULL ) {
        lprintf(log, WARN, 
                "Client did not send the expected amount");
        goto cleanup1;
    }
    dprintf(log, DEBUG, "Got body: %s", body);

    if( (lines=splitlines(body)) == NULL ) {
        lprintf(log, ERROR, 
                "Problem splittling lines with splitlines()");
        fdprintf(clisock, RESPONSE_500_ERR);
        goto cleanup2;
    }
    dprintf(log, DEBUG, "split lines successfully.");

    if( (macaddr=lines[0]) == NULL ) {
        lprintf(log, WARN, 
                "Client did not send MAC address line!");
        fdprintf(clisock, RESPONSE_400);
        goto cleanup3;
    }
    chomp(macaddr);
    dprintf(log, DEBUG, "Got macaddr %s.", macaddr);

    dprintf(log, DEBUG, 
            "About to get clidata for MAC addr %s.", macaddr);

    if( (client=get_clidata(clients, macaddr)) == NULL ) {
        lprintf(log, INFO, 
                "Client tried to connect chan2 before chan1");
        fdprintf(clisock, RESPONSE_412);
        goto cleanup3;
    }

    dprintf(log, DEBUG, 
            "Clidata found for MAC addr %s.", macaddr);

    client->chan2 = clisock;

    dprintf(log, DEBUG, "Creating recvq for new send channel");
    if( (client->sendq=q_init()) == NULL ) {
        lprintf(log, ERROR, 
                "Unable to create recvq for new client!");
        goto cleanup3;
    }

    if( srv_start_tunfile_reader(client) == -1 ) {
        dprintf(log, DEBUG, "About to start tunfile reader");
        fdprintf(clisock, RESPONSE_500_BUSY);
        goto cleanup3;
    }

    dprintf(log, DEBUG, "About to respond to client");
    fdprintf(clisock, RESPONSE_204);

    dprintf(log, DEBUG, "Returning");
    return client;
        
cleanup3:
    free(lines);
cleanup2:
    free(body);
cleanup1:
    return NULL;
}

int handle_f_p2( clidata_t **client ) {
    pthread_t reader = (*client)->reader;
    pthread_t writer = (*client)->writer;

    fdprintf((*client)->chan1, RESPONSE_204);
    
    remove_clidata(clients, (*client)->macaddr);

    dprintf(log, DEBUG, "waking up reader thread (%lu).", reader);
    pthread_kill(reader, SIGCHLD);
    dprintf(log, DEBUG, "waking up writer thread (%lu).", writer);
    pthread_kill(writer, SIGCHLD);
    
    return 0;
}

int handle_s_p2( clidata_t *client, char *hdrs ) {
    int gotten=0;
    int expected = get_content_length(hdrs);
    char *pkt;
    int cnt=0;
    queue_t *recvq = client->recvq;
    int chan1 = client->chan1;

    if( !expected ) {
        lprintf(log, WARN, 
                "Client sent no Content-Length. Dropping.");
        return -1;
    }

    while( gotten < expected ) {
        if( (pkt=get_packet(chan1)) == NULL ) {
            lprintf(log, WARN, 
                    "get_packet() failed. Dropping client.");
            fdprintf(chan1, RESPONSE_500_ERR);
            return -1;
        }
        cnt++;
        gotten += iplen(pkt);
        dprintf(log, DEBUG, "got %d of %d bytes from client",
                gotten, expected);
        if( (q_add(recvq, pkt, Q_WAIT, iplen(pkt))) == -1 ) {
            lprintf(log, WARN, "q_add() failed. Dropping client.");
            fdprintf(chan1, RESPONSE_500_ERR);
            return -1;
        }
    }
    lprintf(log, INFO, "Got  %d bytes in %d pkts",
            gotten, cnt);

    fdprintf(chan1, RESPONSE_204);
    return 0;

}

int handle_r_p2( clidata_t *client, char *hdrs ) {
    int expected = get_content_length(hdrs);
    queue_t *sendq = client->sendq;
    int chan2 = client->chan2;
    struct timespec ts;
    char *body;
    int sex;
    char *pkt;

    if( !expected ) {
        lprintf(log, WARN, 
                "Client sent no Content-Length. Dropping.");
        goto cleanup1;
    }

    if( (body=getbody(chan2, hdrs, &expected)) == NULL ) {
        lprintf(log, WARN, "getbody() failed. Dropping client.");
        goto cleanup1;
    }

    if( (sex=strtol(body, NULL, 0)) == 0 ) {
        lprintf(log, WARN, "Client sent invalid seconds spec.");
        fdprintf(chan2, RESPONSE_400);
        goto cleanup2;
    }

    ts.tv_nsec = 0;
    ts.tv_sec = sex;

    dprintf(log, DEBUG, "waiting up to %d seconds.", sex);

    if( q_timedwait(sendq, &ts) ) {
        int sent=0, total, cnt=0;

        dprintf(log, DEBUG, "returned from wait, with data");
        total = sendq->totsize;
        fdprintf(chan2, RESPONSE_200_NOBODY, sendq->totsize);

        while( sent < total ) {
            if( (pkt=q_remove(sendq, 0, NULL)) == NULL ) {
                lprintf(log, WARN, "q_remove failed!");
                goto cleanup2;
            }
            if( write(chan2, pkt, iplen(pkt)) < 0 ) {
                lprintf(log, INFO, "send failed: %s",
                        strerror(errno));
                free(pkt);
                goto cleanup2;
            }
            cnt++;
            sent += iplen(pkt);
            free(pkt);
        }
        lprintf(log, INFO, "Sent %d bytes in %d pkts",
                sent, cnt);
    } else {
        dprintf(log, DEBUG, "returned from wait, with NO data");
        if( client->chan2 != -1 ) fdprintf(chan2, RESPONSE_204);
    }
    
    return 0;


cleanup2:
    free(body);
cleanup1:
    return -1;

}
