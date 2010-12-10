/* -------------------------------------------------------------------------
 * client.c - htun client daemon functions
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
/* $Id: client.c,v 2.49 2002/11/23 21:45:59 ola Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <limits.h> /* path max */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <semaphore.h> /* posix semaphores */

#include "client.h"
#include "common.h"
#include "http.h"
#include "queue.h"
#include "tun.h"
#include "util.h"

#define SERVER_ACK_WAIT 1
#define SERVER_MAX_RETRIES 4

static queue_t *sendq;
static queue_t *recvq;
static pthread_t main_th_id;

static int restart_connection = 0;
static pthread_mutex_t restart_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t restart_cond = PTHREAD_COND_INITIALIZER;

/********************************************************************
 *** General IO Utility routines
 ********************************************************************/

/*
 * Sends a request of the passed-in type, which can be one of P2_CS P2_CR P2_R
 * P2_S P1_S P1_P P2_F P1_F P1_CS. Writes to tye passed-in filedes fd.
 * Takes a variable number of arguments. You need to pass in all arguments
 * that the function cannot figure out on its own (such as config struct
 * values), in the order they appear in the message. Usually this will only
 * be the content length.
 */
static inline int send_req( int fd, int type, ... ) {
    va_list ap;
    char msg[3] = {0}, *reqname;
    int contentlen = 2;
    int rc = 0;
    short port = ntohs(config->u.c.server_ports[0]);
    int error = 0;

    va_start(ap, type);

    switch(type) {
        case P1_CS:
            contentlen = va_arg(ap, int);
            reqname = "CP1";
            break;
        case P1_S:
        case P2_S:
            contentlen = va_arg(ap, int);
            reqname = "S";
            break;
        case P1_P:
            strcpy(msg, ":)");
            reqname = "P";
            break;
        case P1_F:
        case P2_F:
            strcpy(msg, ":(");
            reqname = "F";
            break;
        case P2_CS:
            contentlen = va_arg(ap, int);
            reqname = "CP2";
            break;
        case P2_CR:
            port = ntohs(config->u.c.server_ports[1]);
            contentlen = va_arg(ap, int);
            reqname = "CR";
            break;
        case P2_R:
            port = ntohs(config->u.c.server_ports[1]);
            contentlen = va_arg(ap, int);
            reqname = "R";
            break;
        default:
            lprintf(log, ERROR, "send_req() passed invalid message type.");
            return -1;
    }

    dprintf(log, DEBUG, "Sending: "
          "POST http://%s:%d/%s HTTP/1.0\r\n", config->u.c.server_ip_str, port, reqname);
    rc = fdprintf(fd, 
          "POST http://%s:%d/%s HTTP/1.0\r\n", config->u.c.server_ip_str, port, reqname);
    if ( rc < 0 ) error = -1;


    if( *config->u.c.base64_user_pass ) {
        dprintf(log, DEBUG, "Sending: "
              "%s%s\r\n", HDR_PROXY_AUTH, config->u.c.base64_user_pass);
        rc = fdprintf(fd, 
              "%s%s\r\n", HDR_PROXY_AUTH, config->u.c.base64_user_pass);
        if ( rc < 0 ) error = -1;
    }

    dprintf(log, DEBUG, "Sending: "
        HDR_PROXY_CONNECTION "%s\r\n" HDR_CONTENT_LENGTH "%d\r\n" "\r\n" "%s",
        ((type == P1_F || type == P2_F) ? "Close" : "Keep-Alive"), contentlen, msg);
    rc = fdprintf(fd, 
        HDR_PROXY_CONNECTION "%s\r\n" HDR_CONTENT_LENGTH "%d\r\n" "\r\n" "%s",
        ((type == P1_F || type == P2_F) ? "Close" : "Keep-Alive"), contentlen, msg);
    if ( rc < 0 ) error = -1;

    va_end(ap);
    return error;
}
    


/*
 * creates a socket 
 *
 * returns sock 
 * returns -1 on failure
 */
static inline int create_socket( void )
{
    int sock;
    if( (sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0 )
        return -1;

    return sock;
}

/* 
 * open proxy connection connection
 *
 * returns socket success
 * returns -1     failture
 */
static inline int open_connection( struct sockaddr_in *srvaddr, int sock )
{
    int rv;

    rv = connect(sock, (struct sockaddr *)srvaddr, sizeof(struct sockaddr));

    if( rv == -1 ){
        lprintf(log, ERROR, "connect() failed: %s",
                strerror(errno));
        return -1;
    }

    return sock;
}

/*
 * sends shutdown headers to server
 * returns status of socket write
 */
#define send_shutdown(sock) send_req((sock), P2_F)

/*
 * recieves incoming data on proxy socket, places it on the recv queue
 *
 * returns  0 success
 * returns -1 failture
 */
static inline int recv_data( int p_sock )
{
    int data_len, c;
    int num;
    char *pkt;
    char buf[HTTP_HEADERS_MAX];

    memset(buf, '\0', HTTP_HEADERS_MAX);

    if( getheaders(p_sock, buf, HTTP_HEADERS_MAX-1) == -1 ) {
        lprintf(log, WARN, "failed to read response headers\n");
        dprintf(log, DEBUG, "string: %s", buf);
        return -1;
    }

    if( !strncmp(buf, MATCH_204_HTTP10, strlen(MATCH_204_HTTP10)) || 
        !strncmp(buf, MATCH_204_HTTP11, strlen(MATCH_204_HTTP11)) ) { 
        dprintf(log, DEBUG, "Nack returned\n");
        return 0;
    }

    /* If we have a 200 response, we've got data */
    if( !strncmp(buf, MATCH_200_HTTP10, strlen(MATCH_200_HTTP10)) ||
        !strncmp(buf, MATCH_200_HTTP11, strlen(MATCH_200_HTTP11)) ) { 
        dprintf(log, DEBUG, "Incoming data\n");

        /* Get the length of the payload */
        data_len = get_content_length(buf);
        if( data_len == 0 ) {
            dprintf(log, DEBUG, "Unable to get Content-Length header value.");
            return -1;
        }

        /* Keep getting data until we've reached the expected data_len */
        num = 0;
        c = 0;
        while( c < data_len ) {
            pkt = get_packet(p_sock);
            if( pkt == NULL ) {
                lprintf(log, WARN, "premature end of data stream\n");
                return -1;
            }
            dprintf(log, DEBUG, "pkt len: %d", iplen(pkt));
            c += iplen(pkt); /* dec data len, prevent race condition
                              * which could occur after packet is in recvq */
            if( q_add(recvq, pkt, Q_WAIT, iplen(pkt)) == -1 ) {
                lprintf(log, WARN, "insert packet, discarding\n");
            } else {
                num++;
            }
        }
    } else { /* The response is not a 200 */
        lprintf(log, WARN, "Bad or Error HTTP response received from server");
        dprintf(log, DEBUG, "Error response is: %s", buf);
        return -1;
    }

    lprintf(log, INFO, "rcvd %d packets, %d bytes\n", num, c);
    return 0;
}

/* 
 * checks to see if there is incoming data from the server (proxy)
 *
 * returns  0 there is data
 * returns -1 if no data
 * returns -2 if connection died (EPIPE)
 */
static inline int server_ack( int p_sock, int wait )
{
    fd_set rfds;
    struct timeval tv;
    int retval;
    char buf;

    /* Watch proxy socket to see when it has input
     * Wait up to five seconds.
     */
    FD_ZERO(&rfds);
    FD_SET(p_sock, &rfds);
    tv.tv_sec = wait;
    tv.tv_usec = 0;

    dprintf(log, DEBUG, "doing select");
    retval = select(p_sock + 1, &rfds, NULL, NULL, &tv);
    dprintf(log, DEBUG, "returned from select");
    if( retval ) { /* possibly data */
        if(recv(p_sock, &buf, 1, MSG_PEEK) == -1) {
            if( errno == EPIPE ) {
                lprintf(log, WARN, "Proxy connection down");
                return -2;
            }
            
            lprintf(log, WARN, "ack failure: %s", strerror(errno));
            return -1;
        }
    }

    return 0;
}

/*
 * dequeues all current data from the sendq send to proxy
 *
 * returns  0 success
 * returns -1 failure
 */
static inline int send_data( int p_sock )
{
    char *pkt;
    int total_len = 0, len;
    int rv, c;

    /* we know there is data on the queue, send it */
    total_len = sendq->totsize;

    dprintf(log, DEBUG, "sending HTTP headers, content len: %d",
            total_len);

    /* write headers */
    if( config->u.c.protocol == 1 ) {
        rv = send_req(p_sock, P1_S, total_len);
        /* rv = fdprintf(p_sock, REQ_P1_S, config->u.c.server_ip_str,
                ntohs(config->u.c.server_ports[0]), total_len); */
    } else {
        rv = send_req(p_sock, P2_S, total_len);
        /* rv = fdprintf(p_sock, REQ_P2_S, config->u.c.server_ip_str,
                ntohs(config->u.c.server_ports[0]), total_len); */
    }

    if( rv < 0 ) {
        dprintf(log, DEBUG, "sending HTTP headers failed");
        return -1;
    }

    dprintf(log, DEBUG, "sent headers");

    c = 0;
    len = total_len;
    while( total_len > 0 ) {
        dprintf(log, DEBUG, "sending HTTP data");

        if( (pkt = q_remove(sendq, 0, NULL) ) == NULL) {
            lprintf(log, WARN, "premature end of sendq");
            return -1;
        }
        c++;
        total_len -= iplen(pkt);
        if( send(p_sock, pkt, iplen(pkt), 0 ) != iplen(pkt) ) {
            if( errno == ENOTCONN ) {
                lprintf(log, WARN,
                        "#%d: Client disconnected prematurely!", p_sock);
                q_add(sendq, pkt, Q_PUSH, iplen(pkt));
                return -1;
            }
        }
        free(pkt);
    }
    lprintf(log, INFO, "sent %d packets, %d bytes\n",
        c, len);
    return 0;
}

/* 
 * negotiates the desired protocol connection with the server
 * saves the peer and local ip in the config
 * returns the socket on success
 * returns -1 on error
 */
static inline int do_negotiate_protocol(void)
{
    struct sockaddr_in proxy_addr;
    iprange_t *ipr = config->u.c.ipr;
    int p_sock, rv;
    char hdr[HTTP_HEADERS_MAX];
    char buf[1024];
    char *body, **content;
    int i, len;

    /* construct the addr */
    memset(&proxy_addr, 0, sizeof(proxy_addr));
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = config->u.c.proxy_ip.s_addr;
    proxy_addr.sin_port = config->u.c.proxy_port;

    /* open the proxy connection */
    if(( p_sock = open_connection(&proxy_addr, create_socket())) < 0 ) {
        return -1;
    }

    /* create the POST body, MAC followed by ipranges */
    i = snprintf( buf, 1024,  "%s\n", get_mac(config->u.c.if_name));
    dprintf(log, DEBUG, "mac: \"%s\"\n",get_mac(config->u.c.if_name));
    while( i < 1023 && ipr != NULL ) {
        i += snprintf(buf+i, 1023 - i,  "%s/%d\n",
                inet_ntoa(ipr->net), ipr->maskbits);
        ipr = ipr->next;
    }

    i = strlen(buf);
    dprintf(log, DEBUG, "buf len: %d i: %d str: \"%s\"\n", strlen(buf), i, buf);

    /* send the header & body */
    if( config->u.c.protocol == 1 )
        rv = send_req(p_sock, P1_CS, i);
        /* rv = fdprintf(p_sock, REQ_P1_CS, config->u.c.server_ip_str,
            ntohs(config->u.c.server_ports[0]), i);  */
            
    else if ( config->u.c.protocol == 2 )
        rv = send_req(p_sock, P2_CS, i);
        /* rv = fdprintf(p_sock, REQ_P2_CS, config->u.c.server_ip_str,
            ntohs(config->u.c.server_ports[0]), i);  */

    if( send(p_sock, buf, i, 0) != i ) {
        lprintf(log, WARN, "failed to send post\n" );
    }

    /* await the response */
    memset(hdr, '\0', HTTP_HEADERS_MAX);

    dprintf(log, DEBUG, "waiting for response\n");

    if( getheaders(p_sock, hdr, HTTP_HEADERS_MAX-1) == -1 ) {
        lprintf(log, WARN, "failed to read response headers\n");
        dprintf(log, DEBUG, "string: %s", hdr);
        return -1;
    }

    dprintf(log, DEBUG, "got response headers");

    if( !strncmp(hdr, RESPONSE_204, strlen(RESPONSE_204)) ) { 
        dprintf(log, DEBUG, "Nack returned\n");
        return -1;
    }

    if( strncmp(hdr, MATCH_200_HTTP10, strlen(MATCH_200_HTTP10)) != 0 &&
        strncmp(hdr, MATCH_200_HTTP11, strlen(MATCH_200_HTTP11)) != 0 ) {
        char *cp = strchr(hdr, '\n');

        if( cp ) *cp = '\0';

        lprintf(log, WARN, "Received unknown error response from proxy or server:");
        lprintf(log, WARN, "  %s", hdr);
        return -1;
    }

    if( (body = getbody(p_sock, hdr, &len)) == NULL ) {
        dprintf(log, DEBUG, "reading body failed\n");
        return -1;
    }

    /* now get the ips */
    content = splitlines(body);
     
    snprintf(config->u.c.local_ip_str, 16, "%s", content[0]);
    snprintf(config->u.c.peer_ip_str, 16, "%s", content[1]);

    dprintf(log, DEBUG, "got ips, local: %s, peer: %s", content[0], content[1]);

    config->u.c.local_ip.s_addr = inet_addr(config->u.c.local_ip_str);
    config->u.c.peer_ip.s_addr = inet_addr(config->u.c.peer_ip_str);

    /* clean up */
    free(content);
    free(body);

    return p_sock;
}

/*
 * restablishes a connection and verifies ips,
 * 
 * if they are the same, returns new sock
 * if they differ, returns -1,
 * if unable to re-establish connection returns -2
 */
static inline int restablish_connection( int sock )
{
    struct in_addr old_local_ip;
    struct in_addr old_peer_ip;

    old_local_ip.s_addr  = config->u.c.local_ip.s_addr;
    old_peer_ip.s_addr  = config->u.c.peer_ip.s_addr;

    close(sock);
    sock = do_negotiate_protocol();
    if( sock < 0 ) {
        lprintf(log, FATAL, "Unable to reopen send channel " 
                "with server %s\n", config->u.c.server_ip_str);

        return -1;
    }

    if( config->u.c.local_ip.s_addr != old_local_ip.s_addr ||
            config->u.c.peer_ip.s_addr != old_peer_ip.s_addr ) {

        lprintf(log, INFO, "channel disconnected\n"
                "upon reconnection recieved new ips from server\n"
                "reconfiguring\n");
        lprintf(log, INFO, "old peer: %s",
                inet_ntoa(old_peer_ip));
        lprintf(log, INFO, "new peer: %s", 
                inet_ntoa(config->u.c.peer_ip));
        lprintf(log, INFO, "old local: %s ",
                inet_ntoa(old_local_ip));
        lprintf(log, INFO, "new local: %s ", 
                inet_ntoa(config->u.c.local_ip));

        return -2;
    }
    return sock;
}

/********************************************************************
 *** Tunfile reader and writer, service the send and recv queues
 ********************************************************************/

/* 
 * thread
 *
 * who's sole purpose is to read from the tunfile write to the sendq
 */
static void *tunfile_reader( void *tunfd )
{
    int fd = *((int*)tunfd);
    char *pkt;

    dprintf(log, DEBUG, "starting...");

    while( 1 ) {
        dprintf(log, DEBUG, "READING");
        if( (pkt=get_packet(fd)) == NULL ) {
            lprintf(log, INFO, "get_packet failed, quitting");
            return NULL;
        }

        dprintf(log, DEBUG, "got packet: %d",iplen(pkt));

        if( q_add(sendq, pkt, Q_WAIT, iplen(pkt)) != 0 ) {
            lprintf(log, INFO, "q_add failed, quitting");
            return NULL;
        }

        dprintf(log, DEBUG, "inserted packed into queue");
    }
}

/* 
 * thread
 *
 * who's sole purpose is to read from the recvq write to the tunfile
 */
static void *tunfile_writer( void *tunfd )
{
    int fd = *((int*)tunfd);
    char *data;

    dprintf(log, DEBUG, "starting...");

    while( 1 ) {

        if(( data = q_remove(recvq, Q_WAIT, NULL)) == NULL)
            return NULL;

        if(write(fd, data, iplen(data)) < 0) {
            lprintf(log, WARN, "write failed: %s",
                    strerror(errno));
        }

        dprintf(log, DEBUG, "wrote %d", iplen(data));
        free(data);
    }
}

/********************************************************************
 *** Protocol 1 - Half Duplex
 ********************************************************************/

/*
 * polls the server for data
 * sock - proxy socket
 *
 * returns  0 success
 * returns -1 failure
 */

static inline int poll_server_p1( int sock )
{
    dprintf(log, DEBUG, "attempting to poll server");
    return send_req(sock, P1_P) > 0 ? -1 : 0;
    /* rv = fdprintf(sock, REQ_P1_P, config->u.c.server_ip_str,
            ntohs(config->u.c.server_ports[0]),
            config->u.c.local_ip_str, 0); */
}


/*
 * thread
 *
 * proxy channel performs all network io in protocol 1,
 * it talks to the proxy, sends and recieves data, polls
 * server when idle (using exponential backoff rate)
 */
static void *proxy_channel( void *sock )
{
    int need_reestablish = 0;
    int psock = *(int *)sock;
    struct timespec wait;
    int state_counter = 0;

    /* set wait time the the min poll interval milisec time */
    wait.tv_sec = (config->u.c.min_poll_interval_msec / 1000);
    wait.tv_nsec = (config->u.c.min_poll_interval_msec % 1000) * 1000000;

    if( psock < 0 ) {
        lprintf( log, ERROR, "invalid socket");
        return NULL;
    }

    while( 1 ) {

        /* client main has destroyed the queues, time to exit */
        if( sendq == NULL && recvq == NULL ) {
            send_shutdown(psock);
            break;
        }

        if( need_reestablish ) {
           lprintf(log, INFO, "connection closed, attempting reopen");

           psock = restablish_connection(psock);
           switch( psock ) {
               case -1:
                   /* signal the parent thread to shutdown */
                   pthread_kill(main_th_id, SIGTERM);
                   return NULL;
               case -2:
                   send_shutdown(psock);
                   close(psock);
                   /* signal parent to restart threads */
                   pthread_kill(main_th_id, SIGCHLD);
                   return NULL;
               default:
                   break;
           }
           need_reestablish = 0;
        }

        if( !q_isempty(sendq) ) {
            dprintf(log, DEBUG, "q is not empty!\n");

            if( send_data(psock) == -1 ) {
                lprintf(log, WARN, "client send failed");
                need_reestablish = 1;
                continue;
            }

            dprintf(log, DEBUG, "attempting to get server ack\n");
            if( server_ack(psock, 10) == 0) {
                dprintf(log, DEBUG, "got server ack - recving data!\n");

                if( recv_data(psock) == -1 ) {
                    dprintf(log, DEBUG, "recv_data failed - reopening conn\n");
                    need_reestablish = 1;
                    continue;
                }

                dprintf(log, DEBUG, "finished recving\n");
            } else {

                dprintf(log, DEBUG, "Reopening connection!\n");
                need_reestablish = 1;
                continue;
            }
        } else { /* queue is empty */

            if( q_timedwait(sendq, &wait) == 0 ) {

                if( poll_server_p1(psock) == -1) {
                    dprintf(log, DEBUG, "polling server failed");
                    need_reestablish = 1;
                    continue;
                }

                /* expect ack from server */
                if( recv_data(psock) == -1 ) {
                    dprintf(log, DEBUG, "Poll ack recv failure");
                    need_reestablish = 1;
                    continue;
                }

                /* increment the wait time */

                state_counter++;

                if(state_counter >= config->u.c.poll_backoff_rate) {
                    wait.tv_sec *= 2;
                    wait.tv_sec += (wait.tv_nsec * 2) / 1000000000;
                    wait.tv_nsec = (wait.tv_nsec*2) % 1000000000;

                    if( wait.tv_sec > config->u.c.max_poll_interval )
                        wait.tv_sec = config->u.c.max_poll_interval;
                    state_counter = 0;
                }

            } else {
                dprintf(log, DEBUG, "Back from sleep - with data");
                /* there is data, reset sleep to 0.2 secs */
                wait.tv_sec = (config->u.c.min_poll_interval_msec / 1000);
                wait.tv_nsec =
                    (config->u.c.min_poll_interval_msec % 1000) * 1000000;
                state_counter = 0;
            }
        }
    }
    return NULL;
}


/********************************************************************
 *** Protocol 2 - Full duplex
 ********************************************************************/


/*
 * does not poll but tells the server how long to wait before
 * returning data
 * sock - proxy socket
 * wait - wait time (in sec) before timeout
 *
 * returns  0 success
 * returns -1 failure
 */
static inline int poll_server_p2( int sock, int wait )
{
    int rv;
    char buf[8];

    snprintf(buf, 8, "%d", wait);

    rv = send_req(sock, P2_R, strlen(buf));
    /* rv = fdprintf(sock, REQ_P2_R, config->u.c.server_ip_str,
            ntohs(config->u.c.server_ports[1]), strlen(buf)); */
 
    if( rv < 0 ) return -1;

    if( send(sock, buf, strlen(buf), 0) != (int)strlen(buf) ) return -1; 

    return 0;
}

/* 
 * opens the recieiver channel
 * returns a newly recieve channel socket
 */
static inline int open_recieve_channel( void )
{
    struct sockaddr_in proxy_addr;
    int p_sock, rv;
    char hdr[HTTP_HEADERS_MAX];
    char buf[1024];
    char *body;
    int i, len, port;

    /* construct the addr */
    memset(&proxy_addr, 0, sizeof(proxy_addr));
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = config->u.c.proxy_ip.s_addr;
    proxy_addr.sin_port = config->u.c.proxy_port;

    /* open the proxy connection */
    if(( p_sock = open_connection(&proxy_addr, create_socket())) < 0 )
        return -1;

    /* create the POST body, MAC addr */
    i = snprintf( buf, 1023 ,  "%s", get_mac(config->u.c.if_name));

    /* send the header */
    port = ntohs(config->u.c.server_ports[1]);
    dprintf( log, DEBUG, "port: %d", port);
    rv = send_req(p_sock, P2_CR, i);
    /* rv = fdprintf(p_sock, REQ_P2_CR, config->u.c.server_ip_str, port, i); */

    /* send the body (MAC) */
    if( send(p_sock, buf, i, 0) != i ) {
        lprintf(log, WARN, "failed to send hdr\n" );
    }

    /* await the response */
    memset(hdr, '\0', HTTP_HEADERS_MAX);

    if( getheaders(p_sock, hdr, HTTP_HEADERS_MAX-1) == -1 ) {
        lprintf(log,WARN,"failed to read response headers");
        dprintf(log,DEBUG,"reason: %s", hdr);
        return -1;
    }

    if( !strncmp(hdr, MATCH_204_HTTP10, strlen(MATCH_204_HTTP10)) ||
        !strncmp(hdr, MATCH_204_HTTP10, strlen(MATCH_204_HTTP10)) ) { 
        lprintf(log, INFO, "channel opened\n");
        return p_sock;
    }

    /* this only gets executed if the server returned an error */

    if( (body = getbody(p_sock, hdr, &len)) == NULL ) {
        lprintf(log, WARN, "reading body failed\n");
        return -1;
    }

    lprintf(log, WARN, "failed to open channel\n");
    lprintf(log, WARN, "reason: %s", body);

    free(body);

    return -1;
}

/* 
 * thread
 *
 * continually waits for data from the server and adds
 * it to the recv queue
 */
static void *reciever( void *unused )
{
    int sock = -1;
    int wait = config->u.c.channel_2_idle_allow;
    int reconnect = 0;
    int retry = config->u.c.reconnect_tries;

    unused = unused; /* :) */

    reconnect = 1;

    for(;;) {
        if( reconnect ) {
            if( sock > 0 ) 
                close(sock);

            while( retry != 0 || config->u.c.reconnect_tries == -1 ) {
                sock = open_recieve_channel();
                if(sock < 0) {
                    lprintf(log, WARN,
                    "Recive Channel Connect failed, Sleeping before retry...");
                    --retry;
                    sleep(config->u.c.reconnect_sleep_sec);
                } else {
                    /* resent the retries so that if we disconnect
                     * again we can re connect */
                    retry = config->u.c.reconnect_tries;
                    break;
                }
            }
            reconnect = 0;
        }

        if(sock < 0) {
            lprintf(log, FATAL, "Recive Channel Connect failed, quitting...");
            /* signal the parent signal handler to shut down */
            pthread_kill(main_th_id, SIGTERM);
            return NULL;
        }

        if( poll_server_p2(sock, wait) != 0 ) {
            reconnect = 1;
            continue;
        }

        if( server_ack(sock, wait) != 0 ) {
            reconnect = 1;
            continue;
        }
       
        if( recv_data(sock) != 0 ) {
            reconnect = 1;
            continue;
        }
    }

    return NULL;
}

/* 
 * thread
 *
 * send data to server over the established socket
 */
static void *sender( void *socket )
{
    int sock = *(int *)socket;
    struct sockaddr_in proxy_addr;
    struct timespec wait = {10, 500000};
    int need_reestablish = 0;

    /* construct the addr */
    memset(&proxy_addr, 0, sizeof(proxy_addr));
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = config->u.c.proxy_ip.s_addr;
    proxy_addr.sin_port = config->u.c.proxy_port;

    for(;;) {
        if( q_timedwait(sendq, &wait) ) {

            if( send_data(sock) != 0 )
                need_reestablish = 1;

            /* recvieve the 204 No Data (ack) */
            if( recv_data(sock) != 0  && !need_reestablish )
                need_reestablish = 1;
        } else {

            /* sendq is destroyed, we are exiting, signal the server
             * and clean up */
            if(sendq == NULL) {
                lprintf(log, INFO, "sendq is NULL, exiting");
                send_shutdown(sock);
                close(sock);
                return NULL;
            }

            /* we timed out, possibly send NO data to server
             * to keep connection alive */
        }

        if( need_reestablish ) {

            sock = restablish_connection(sock);
            switch( sock ) {
                case -1:
                    /* signal the parent thread to shutdown */
                    pthread_kill(main_th_id, SIGTERM);
                    return NULL;
                case -2:
                    send_shutdown(sock);
                    close(sock);
                    /* signal parent to restart threads */
                    pthread_kill(main_th_id, SIGCHLD);
                    return NULL;
                default:
                    break;
            }

            need_reestablish = 0;
        }
    }

    return NULL;
}

/********************************************************************
 *** starup functions
 ********************************************************************/

static inline int do_shutdown(pthread_t *tids, int tunfd)
{
    /* Kill queues */
    q_destroy(&sendq);
    q_destroy(&recvq);
    lprintf(log, INFO, "send and recv queues destroyed");

    /*close(p_sock);*/
    close(tunfd);

    lprintf(log, INFO, "shutting down tunfile reader and writer");
    pthread_kill(tids[0], SIGCHLD);
    pthread_kill(tids[1], SIGCHLD);
    pthread_join(tids[0], (void **)NULL);
    pthread_join(tids[1], (void **)NULL);
    lprintf(log, INFO, "tunfile reader and writer exited");
    
    /* restore default route */
    if( config->u.c.do_routing ) {
        if( restore_default_gw() != 0 ) {
            lprintf(log, FATAL, "Unable to restore default route");
            return -1;
        }
        lprintf( log, INFO, "restored default route" );
    }

    if( config->u.c.protocol == 1 ) {
        lprintf(log, INFO, "shutting down proxy channel thread");
        pthread_kill(tids[2], SIGCHLD);
        /*pthread_join(tids[2], (void **)NULL);*/
        pthread_cancel(tids[2]);
        lprintf(log, INFO, "proxy channel thread exited");
    } else {
        lprintf(log, INFO, "Cancelling Sender and Reciever" );
        pthread_cancel(tids[2]);
        pthread_cancel(tids[3]);
        lprintf(log, INFO, "Sender and Reciever threads killed");
    }

    return 0;
}

/*
 * main thread that starts up and shutsdown all worker threads
 */
static void *starter(void *unused)
{
    extern int tunfd; /* from common.c */
    int sock;
    int run, reconnect = config->u.c.connect_tries, quit = 0;
    pthread_t tids[4];
    config_data_t *tmp;

    unused = unused;

    run = 1;
    while( run ) {
        
        lprintf(log, INFO, "Initiating server connection");

        /* reconnect configfile specified number of times OR
         * try forever if "connect_tries" == -1 */
        while( reconnect != 0 || config->u.c.connect_tries == -1 ) {
            /* establish a channel to the server */
            sock = do_negotiate_protocol();
            if( sock < 0 ) {
                lprintf(log, WARN,
                        "Connect failed, Sleeping before retry...");
                --reconnect;
                sleep(config->u.c.reconnect_sleep_sec);
            } else {
                break;
            }

        }

        lprintf(log, INFO, "Server Channel Established");

        if( sock < 0 ) {
            lprintf(log, FATAL, "Unable to negotiate desired protocol %d " 
                    "with server %s:%d via proxy %s:%d", config->u.c.protocol,
                    config->u.c.server_ip_str, ntohs(config->u.c.server_ports[0]),
                    config->u.c.proxy_ip_str, ntohs(config->u.c.proxy_port));
            break;
        }

        /* create the packet queues */
        sendq = q_init();
        recvq = q_init();
        if( sendq == NULL || recvq == NULL ) {
            lprintf(log, FATAL, 
                    "unable to create client queues, quitting...");
            break;
        }

        /* configure the tun dev */
        getprivs("setting up the tundev");

        tunfd = cli_tun_alloc(config->u.c.local_ip, config->u.c.peer_ip);
        dprintf(log, DEBUG, "tunfd: %d\n", tunfd);
        if( tunfd < 0 ) {
            lprintf(log, FATAL, "Unable configure the tun device");
            break;
        }

        /* setup new default route */
        if( config->u.c.do_routing ) {
            int tsock = create_socket();    
            if( store_default_gw() != 0 ) {
                lprintf(log, FATAL, "Unable to store default route");
                break;
            }
            if( set_default_gw( tsock ) != 0 ) {
                lprintf(log, FATAL, "Unable to set default route");
                break;
            }
        }

        dropprivs("tundev up");

        /* create the tun reader and writer */
        pthread_create( &tids[0], NULL, tunfile_reader, &tunfd );
        pthread_create( &tids[1], NULL, tunfile_writer, &tunfd );

        if( config->u.c.protocol == 1 ) {
            pthread_create( &tids[2], NULL, proxy_channel, (void*)&sock);
        } else if ( config->u.c.protocol == 2 ) {
            pthread_create( &tids[2], NULL, reciever, NULL );
            pthread_create( &tids[3], NULL, sender, (void*)&sock);
        }

        pthread_mutex_lock(&restart_mutex);
        while(restart_connection == 0) {
            pthread_cond_wait(&restart_cond, &restart_mutex);
        }
        if( restart_connection == -1 )
            quit = 1;
        restart_connection = 0;
        pthread_mutex_unlock(&restart_mutex);

        if(quit) break;

        if( do_shutdown(tids, tunfd) < 0 ) {
            lprintf( log, FATAL, "Unable to shutdown cleanly, quitting");
            pthread_kill(main_th_id, SIGTERM);
            return NULL;
        }

        tmp=config;
        config=read_config(config->cfgfile);
        free(tmp);
    }
    
    /* just shutdown and exit */
    do_shutdown(tids, tunfd);
    /* make sure main thread (sig handler) knows to exit too */
    pthread_kill(main_th_id, SIGTERM);
    return NULL;
}

/*
 * client main
 *
 * starts threads that begin client mode execution
 * this is esentially just a signal handler
 * returns EXIT_SUCCESS     if all goes well
 * returns EXIT_FAILURE     if bad things happen
 */
int client_main(void)
{
    int signum;
    sigset_t newmask;
    pthread_t starter_tid;

    lprintf(log, INFO, "HTUN Client starting");

    /* setup main thread id so worker threads can signal us */
    main_th_id = pthread_self();

    /* start the starter thread */
    pthread_create(&starter_tid, NULL, starter, NULL);

    /* Catch signals synchronously */
    sigfillset(&newmask);
    while( 1 ) {
        sigwait(&newmask,&signum);
        lprintf(log, INFO, "Program recieved %s", signames[signum]);
        switch(signum) {
            case SIGHUP:
                lprintf(log, INFO, "re-reading config file");
            case SIGCHLD:
                goto restart;
            case SIGTSTP:
                kill(getpid(), SIGSTOP);
                break;
            case SIGINT:
            case SIGTERM:
                goto cleanup;
                break;
            default:
                lprintf(log, WARN,
                        "Unknown signal %d caught.",
                        signum );
                break;
        }
        continue;

restart:
        pthread_mutex_lock(&restart_mutex);
        restart_connection = 1;
        /* wake up the starter thread */
        pthread_mutex_unlock(&restart_mutex);
        pthread_cond_signal(&restart_cond);
    }

cleanup:
    pthread_mutex_lock(&restart_mutex);
    restart_connection = -1;
    /* wake up the starter thread */
    pthread_mutex_unlock(&restart_mutex);
    pthread_cond_signal(&restart_cond);

    /*pthread_cancel(starter_tid);*/
    pthread_join(starter_tid, (void **)NULL);

    lprintf( log, INFO, "HTun client daemon exiting." );

    log_close(log);

    return EXIT_SUCCESS;
}
