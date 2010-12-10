/* -------------------------------------------------------------------------
 * srvproto1.c - htun protocol 1 functions for the server side
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
/* $Id: srvproto1.c,v 2.12 2002/08/16 01:49:47 jehsom Exp $ */


#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include "common.h"
#include "log.h"
#include "http.h"
#include "util.h"
#include "srvproto2.h"
#include "tun.h"


int handle_f_p1( clidata_t **clientp ) {
    /* for now we just use f_p2 since they're identical */
    return handle_f_p2(clientp);
}

/*
 * Waits for one of the following conditions to become true before returning:
 * 1. config->u.s.min_nack_delay time passes with no packets on sendq
 * 2. sendq nonempty and config->u.s.packet_max_interval elapses since last packet queued
 * 3. sendq contains at least config->u.s.packet_count_threshold packets.
 * 4. config->u.s.max_response_delay has elapsed since the start of the function.
 */
static inline size_t sendq_wait( queue_t *q ) {
    struct timespec ts;
    struct timeval t1, t2;
    unsigned int nr_pkts = 0;
    unsigned long long ns=0;

    dprintf(log, DEBUG, "starting");

    /* Get the start time */
    gettimeofday(&t1, NULL);

    /* Wait for at most config->u.s.min_nack_delay for data. Return if none */
    ts.tv_sec = config->u.s.min_nack_delay / 1000;
    ts.tv_nsec = (config->u.s.min_nack_delay % 1000) * 1000000;
    if( !q_timedwait(q, &ts) ) {
        lprintf(log, DEBUG, "No data in queue after min nack delay (%lums)",
                config->u.s.min_nack_delay);
        goto end;
    }

    while( 1 ) {
        /* If we have config->u.s.packet_count_threshold packets, return */
        if( (nr_pkts=q->nr_nodes) >= config->u.s.packet_count_threshold ) {
            lprintf(log, DEBUG, 
                    "pkt count threshold of %d reached w/%d pkts.",
                    config->u.s.packet_count_threshold, nr_pkts);
            goto end;
        }
        
        /* Put the current time in t2 */
        gettimeofday(&t2, NULL);

        /* Sleep till lastadd + config->u.s.packet_max_interval */
        ns = q->lastadd.tv_sec * 1000000000
           + q->lastadd.tv_usec * 1000
           + config->u.s.packet_max_interval * 1000000
           - t2.tv_sec * 1000000000
           - t2.tv_usec * 1000;

        if( ns <= config->u.s.packet_max_interval * 1000000 ) {
            ts.tv_sec = ns / 1000000000;
            ts.tv_nsec = ns % 1000000000;
                     
            lprintf(log, DEBUG, 
                    "waiting %lu.%09lu sec for another pkt...",
                    ts.tv_sec, ts.tv_nsec);
            nanosleep(&ts, NULL);
        }

        /* If q->nr_nodes still hasn't changed, we can return */
        if( nr_pkts == q->nr_nodes ) {
            lprintf(log, DEBUG, 
                    "no new packets since last check.");
            goto end;
        }
    }
end:

    gettimeofday(&t2, NULL);

    lprintf(log, DEBUG, 
            "slept %lu.%06lu sec. %d pkts (%lu bytes) ready.",
            t2.tv_sec - t1.tv_sec, t2.tv_usec - t1.tv_usec, q->nr_nodes,
            q->totsize);
    return q->totsize;
}

static inline int send_queue( queue_t *q, int fd, size_t amount ) {
    unsigned int sent=0;
    int cnt, totcnt=0;
    char *pkt;

    if( amount == 0 ) {
        dprintf(log, DEBUG, "no data to send to client");
        fdprintf(fd, RESPONSE_204);
    } else {
        dprintf(log, DEBUG, "data to send.");
        fdprintf(fd, RESPONSE_200_NOBODY, amount);

        while( sent < amount ) {
            if( (pkt=q_remove(q, 0, NULL)) == NULL ) {
                lprintf(log, WARN, "q_remove failed!");
                return -1;
            }
            if( (cnt=write(fd, pkt, iplen(pkt))) < 0 ) {
                lprintf(log, INFO, "send failed: %s",
                        strerror(errno));
                free(pkt);
                return -1;
            }
            dprintf(log, DEBUG, "wrote %d bytes to client", cnt);
            totcnt++;
            sent += iplen(pkt);
            free(pkt);
        }
        lprintf(log, INFO, "Sent %d bytes in %d pkts.", 
                sent, totcnt);
    }
    return 0;
}

int handle_p_p1( clidata_t *client, char *hdrs ) {
    char *pkt;
    queue_t *sendq = client->sendq;
    int chan1 = client->chan1;
    int tmp;

    pkt=getbody(chan1, hdrs, &tmp);
    free(pkt);
    
    send_queue(sendq, chan1, sendq->totsize);

    dprintf(log, DEBUG, "returning");
    return 0;
}


int handle_s_p1( clidata_t *client, char *hdrs ) {
    int gotten=0, cnt=0;
    int expected = get_content_length(hdrs);
    char *pkt;
    queue_t *recvq = client->recvq;
    queue_t *sendq = client->sendq;
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
            free(pkt);
            return -1;
        }
    }
    lprintf(log, INFO, "Got %d bytes in %d pkts.",
            gotten, cnt);

    send_queue(sendq, chan1, sendq_wait(client->sendq));

    dprintf(log, DEBUG, "returning");

    return 0;

}
