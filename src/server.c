/* -------------------------------------------------------------------------
 * server.c - htun server daemon functions
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
/* $Id: server.c,v 2.48 2002/08/16 01:49:47 jehsom Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"
#include "server.h"
#include "tpool.h"
#include "queue.h"
#include "log.h"
#include "http.h"
#include "tun.h"
#include "util.h"
#include "srvproto2.h"
#include "srvproto1.h"
#include "iprange.h"
#include "clidata.h"

tpool_t *tpool;
clidata_list_t *clients=NULL;

/* 
 * The threads in the threadpool that handle incoming clients run this as
 * their main function.
 */
void client_handler( void *clisock_in ) {
    int reqtype;
    char req[HTTP_REQUESTLINE_MAX];
    char hdrs[HTTP_HEADERS_MAX];
    clidata_t *client=NULL;
    int clisock;
    int rc=0;
    int chantype=0;
    
    if( !clisock_in ) {
        lprintf(log, ERROR, "Received null socket pointer!");
        return;
    }
    clisock = *((int*)clisock_in);

    while( 1 ) {
        if( (reqtype=parse_request(clisock, req)) == REQ_NONE ) {
            lprintf(log, INFO, "disconnect on socket #%d",
                    clisock);
            goto ch_error;
        }
        
        dprintf(log, DEBUG, "About to get headers from client");
        if( getheaders(clisock, hdrs, HTTP_HEADERS_MAX) == -1 ) {
            lprintf(log, WARN, 
                "Couldn't receive HTTP hdrs from client.");
            goto ch_error;
        }
        
        /* if chantype == 0, this is initial request. Should be CP or CR */
        if( chantype == 0 ) {
            switch( reqtype ) {
                case REQ_CP1:
                    lprintf(log, INFO, 
                            "Configuring protocol 1 channel");
                    client = handle_cp(clisock, hdrs, 1);
                    break;
                case REQ_CP2:
                    lprintf(log, INFO, 
                            "Configuring protocol 2 channel 1");
                    client = handle_cp(clisock, hdrs, 2);
                    break;
                case REQ_CR:
                    lprintf(log, INFO, 
                            "Configuring protocol 2 channel 2");
                    client = handle_cr(clisock, hdrs);
                    break;
                case REQ_GET:
                default:
                    lprintf(log, WARN, 
                            "Redirecting bad request: '%s'", 
                            chomp(req));
                    if( proxy_request(clisock, req, hdrs) == -1 ) {
                        fdprintf(clisock, RESPONSE_503);
                    }
                    goto ch_error;
            }
            /* If client is null, it means the handler failed */
            if( !client ) goto ch_error;
            /* 
             * Now that we've established a valid channel, set chantype so we
             * know to expect non-configuration messages in the future 
             */
            chantype = reqtype;
        } else if( chantype == REQ_CP1 ) {
            switch( reqtype ) {
                case REQ_S:
                    rc=handle_s_p1(client, hdrs);
                    break;
                case REQ_P:
                    rc=handle_p_p1(client, hdrs);
                    break;
                case REQ_F:
                    lprintf(log, INFO, "Client %s requested a close.",
                            client->macaddr);
                    rc=handle_f_p1(&client);
                    return;
                default:
                    lprintf(log, WARN, 
                        "Bad request on proto1 chan: %s.",
                        chomp(req)); 
                    handle_f_p1(&client);
                    return;
            }
        } else if( chantype == REQ_CP2 ) {
            switch( reqtype ) {
                case REQ_S:
                    rc=handle_s_p2(client, hdrs);
                    break;
                case REQ_F:
                    lprintf(log, INFO, "Client %s requested a close.",
                            client->macaddr);
                    rc=handle_f_p2(&client);
                    return;
                default:
                    lprintf(log, WARN, 
                        "Bad request on proto2 chan 1: %s.",
                        chomp(req)); 
                    handle_f_p1(&client);
                    return;
            }
        } else if( chantype == REQ_CR ) {
            switch( reqtype ) {
                case REQ_R:
                    rc=handle_r_p2(client, hdrs);
                    break;
                default:
                    lprintf(log, WARN, 
                        "Bad request on proto2 chan 2: %s.",
                        chomp(req)); 
                    handle_f_p1(&client);
                    return;
            }
        }
        if( rc == -1 ) {
            lprintf(log, INFO, 
                    "proto handler failed. returning.");
            goto ch_error;
        }
    }
    /* Should not get here, but just in case... */
    return;

ch_error:
    if( chantype == REQ_CP1 || chantype == REQ_CP2 ) {
        close(client->chan1);
        client->chan1 = -1;
        client->lastuse = time(NULL);
    } else if( chantype == REQ_CR ) {
        close(client->chan2);
        client->chan2 = -1;
        client->lastuse = time(NULL);
    } else {
        close(clisock);
    }
    return;
}

/* 
 * The thread that reads the tunfile and writes to the send queue runs this as
 * its main function, returning when the tunfd is closed.
 */
static void tunfile_reader( void *clidata_in ) 
{
    clidata_t *clidata = (clidata_t*)clidata_in;
    char *pkt;

    dprintf(log, DEBUG, "starting, tunfd #%d", clidata->tunfd);

    clidata->reader = pthread_self();

    while( 1 ) {
        if( (pkt=get_packet(clidata->tunfd)) == NULL ) break;
        if( q_add(clidata->sendq, pkt, Q_WAIT, iplen(pkt)) == -1 ) break;
    }

    lprintf(log, INFO, "Tunfile Reader exiting.");
    return;
}

/* 
 * The thread that reads the receive queue and writes to the tunfile runs this
 * as its main function returning when the recvq is destroyed.
 */
static void tunfile_writer( void *clidata_in ) 
{
    clidata_t *clidata = (clidata_t*)clidata_in;
    char *data;
    queue_t *recvq = clidata->recvq;
    int rc;

    dprintf(log, DEBUG, "starting, tunfd #%d", clidata->tunfd);

    clidata->writer = pthread_self();

    while(1) {
        if( (data=q_remove(recvq, Q_WAIT, NULL)) == NULL ) break;

        rc = write(clidata->tunfd, data, iplen(data));
        if( rc != -1 ) errno = 0;
        dprintf(log, DEBUG, 
                "writing %d byte pkt to tunfd: %s",
                iplen(data), strerror(errno));
        if( rc == -1 ) {
            free(data);
            break;
        }
        free(data);
    }    
    lprintf(log, INFO, "exiting.");
    return;
}

/* Bind to the port and listen. Return the socket's fd or -1 on error */
static int create_srvsock( unsigned short port )
{
    int sock, sockopt;
    struct sockaddr_in addr;
    struct sockaddr *myaddr = (struct sockaddr *)&addr;

    if( port == 0 ) {
        lprintf(log, ERROR, 
                "Cannot bind to port 0. Perhaps you forgot a config entry?");
        return -1;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if( (sock=socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1 ){
        lprintf(log, FATAL, 
                "Creating server socket: %s\n", strerror(errno));
        return -1;
    }

    sockopt=1;
    if( setsockopt(sock, 
        SOL_SOCKET, SO_REUSEADDR, (void*)&sockopt, sizeof(sockopt)) == -1 ) {
        lprintf( log, WARN, 
                "Setting socket options: %s.\n", strerror(errno) );
    }

    if( port < 1024 ) getprivs("Binding to port");
    if( bind(sock, myaddr, sizeof(struct sockaddr)) == -1 ){
        lprintf( log, ERROR, 
                "Binding to socket: %s\n", strerror(errno) );
        dprintf( log, DEBUG, "   port: %d  addr: %s",
                ntohs(addr.sin_port), inet_ntoa(addr.sin_addr) );
        close(sock);
        return -1;
    }
    if( port < 1024 ) dropprivs("Bound to port");

    if( listen(sock, HTUN_SOCKPENDING) == -1 ){
        lprintf( log, FATAL, "Listening on socket: %s\n", strerror(errno) );
        close(sock);
        return -1;
    }

    lprintf( log, INFO, "HTun daemon bound to port %d and listening.",
            ntohs(addr.sin_port) );

    return sock;
}

/* Wait for a request */
static int request_wait( int srvsock )
{
    int clisock;
    struct sockaddr_in cliaddr;
    socklen_t cliaddr_len = (socklen_t)sizeof(cliaddr);

    clisock=accept(srvsock, (struct sockaddr *) &cliaddr, &cliaddr_len);

    if( clisock == -1 ) {
        lprintf(log, WARN, "accept() failed: %s.\n", strerror(errno));
    } else {
        lprintf(log, INFO, "Accepted connection from %s, fd #%d.\n",
                inet_ntoa(cliaddr.sin_addr), clisock);
    }

    return clisock;
}

/* Listens for incoming connections and dispatches the clients to the tpool */
static void *dispatcher( void *srvsock_in ) {
    int srvsock = *((int*)srvsock_in);
    int clisock;

    while(1){
        if( (clisock=request_wait(srvsock)) == -1 ){
            lprintf( log, WARN, "dispatcher: request_wait() failed.\n" );
            continue;
        }
        
        if( tpool_add_work(tpool,client_handler,&clisock) == -1 ) {
            lprintf( log, INFO, 
                    "Request queue full. Dumping client.\n" );
            close(clisock);
            continue;
        }
    }
}

/* For export. Duty Free */
int srv_start_tunfile_reader( clidata_t *client ) 
{
    int clisock;
    
    if( !client ) {
        lprintf(log, ERROR, "passed NULL client!");
        return -1;
    }

    clisock = (client->chan2 == -1) ? client->chan1 : client->chan2;

    /* Create sendq for client */
    dprintf(log, DEBUG, 
            "Creating sendq for new client");
    if( (client->sendq=q_init()) == NULL ) {
        lprintf(log, ERROR,
                "Unable to create sendq for new client!");
        goto cleanup1;
    }

    /* Start tunfile reader */
    dprintf(log, DEBUG, "About to start tunfile reader");
    if( tpool_add_work(tpool, tunfile_reader, client) == -1 ) {
        dprintf(log, DEBUG,
                "starting tunfile reader: Too busy");
        fdprintf(clisock, RESPONSE_500_BUSY);
        goto cleanup2;
    }
    return 0;

cleanup2:
    q_destroy(&client->sendq);
cleanup1:
    return -1;
}

/* For export. Duty Free. */
int srv_start_tunfile_writer( clidata_t *client ) 
{
    int clisock;

    if( !client ) {
        lprintf(log, ERROR, "passed NULL client!");
        return -1;
    }

    clisock = client->chan1;

    /* Create recvq for client */
    dprintf(log, DEBUG, 
            "Creating recvq for new client");
    if( (client->recvq=q_init()) == NULL ) {
        lprintf(log, ERROR,
                "Unable to create recvq for new client!");
        goto cleanup1;
    }

    /* Start tunfile writer */
    dprintf(log, DEBUG, 
            "About to start tunfile writer");
    if( tpool_add_work(tpool, tunfile_writer, client) == -1 ) {
        dprintf(log, DEBUG,
                "starting tunfile writer: Too busy");
        fdprintf(clisock, RESPONSE_500_BUSY);
        goto cleanup2;
    }
    return 0;

cleanup2:
    q_destroy(&client->recvq);
cleanup1:
    return -1;
}

static void dump_stats( void ) {
    clidata_t *c = clients->head;
    time_t ago;
    
    lprintf(log, INFO, "Known clients:\n" );
    while( c ) {
        lprintf(log, INFO, "Client %s:", c->macaddr);
        lprintf(log, INFO, "\tClient IP : %s", inet_ntoa(c->cliaddr));
        lprintf(log, INFO, "\tServer IP : %s", inet_ntoa(c->srvaddr));
        lprintf(log, INFO, "\tTUN fd    : %d", c->tunfd);
        lprintf(log, INFO, "\tWriter TID: %lu", c->writer);
        lprintf(log, INFO, "\tReader TID: %lu", c->reader);
        lprintf(log, INFO, "\tChan1 sock: %d", c->chan1);
        lprintf(log, INFO, "\tChan2 sock: %d", c->chan2);
        ago = time(NULL) - c->lastuse;
        if( c->chan1 == -1 && c->chan2 == -1 ) {
            lprintf(log, INFO, "\tLast use  : %lu seconds ago", ago);
        }
        if( c->sendq ) {
            lprintf(log, INFO, "\tSend Queue: head=%lu, len=%lu, size=%lu, "
                "readers=%d, writers=%d, shutdown=%d, lastadd=%lu.%09lu",
                c->sendq->head, c->sendq->nr_nodes, c->sendq->totsize,
                c->sendq->readers, c->sendq->writers, c->sendq->shutdown,
                c->sendq->lastadd.tv_sec, c->sendq->lastadd.tv_usec);
        } else {
            lprintf(log, INFO, "\tSend Queue: NULL");
        }
        if( c->recvq ) {
            lprintf(log, INFO, "\tRecv Queue: head=%lu, len=%lu, size=%lu, "
                "readers=%d, writers=%d, shutdown=%d, lastadd=%lu.%09lu.",
                c->recvq->head, c->recvq->nr_nodes, c->recvq->totsize,
                c->recvq->readers, c->recvq->writers, c->recvq->shutdown,
                c->recvq->lastadd.tv_sec, c->recvq->lastadd.tv_usec);
        } else {
            lprintf(log, INFO, "\tRecv Queue: NULL");
        }
        c = c->next;
    }

    
    return;
}

int server_main( void ) {
    sigset_t newmask;
    pthread_t dispatchers[2];
    int socks[2];
    int signum;
    config_data_t *tmp;
    int i=0;

    /* Create thread pool */
    tpool = tpool_init( config->u.s.max_clients, config->u.s.max_pending, 1 );
    if( !tpool ) {
        lprintf( log, FATAL, "tpool_init() failed." );
        goto cleanup1;
    }

    if( (clients=new_clidata_list()) == NULL ) {
        lprintf(log, FATAL, "Could not create client list.");
        goto cleanup2;
    }

    /* Get our server socket */
    if( (socks[0]=create_srvsock(ntohs(config->u.s.server_ports[0]))) == -1 ) {
        lprintf( log, FATAL, "Fatal: Could not create server socket." );
        goto cleanup3;
    }

    /* Spawn the dispacher */
    if( pthread_create(&dispatchers[0], NULL, dispatcher, &socks[0]) ) {
        lprintf(log, FATAL, "Could not create dispatcher 1");
        goto cleanup4;
    }
    
    /* Get our second server socket */
    if( (socks[1]=create_srvsock(ntohs(config->u.s.server_ports[1]))) == -1 ) {
        lprintf( log, FATAL, "Fatal: Could not create server socket." );
        goto cleanup5;
    }
    /* Spawn the dispacher */
    if( pthread_create(&dispatchers[1], NULL, dispatcher, &socks[1]) ) {
        lprintf(log, FATAL, "Could not create dispatcher 2");
        goto cleanup6;
    }

    
    lprintf( log, INFO, "HTun server daemon started successfully." );
    
    /* Set up our SIGALRM system for clidata_list cleanup */
    alarm(60);

    sigfillset(&newmask);
    /* Catch signals synchronously */
    while(1) {
        lprintf(log, DEBUG, "Waiting on signal...");
        sigwait(&newmask,&signum);
        lprintf(log, INFO, "Program received %s.", signames[signum]);
        switch(signum) {
            case SIGHUP:
                tmp=config;
                config=read_config(config->cfgfile);
                free(tmp);
                break;
            case SIGINT:
            case SIGTERM:
                goto cleanup7;
            case SIGUSR1:
                dump_stats();
                break;
            case SIGTSTP:
                kill(getpid(),SIGSTOP);
                break;
            case SIGALRM:
                prune_clidata_list(clients);
                alarm(60);
                break;
            default:
                lprintf( log, WARN, "Unknown signal %d caught.",
                        signum );
                break;
        }
    }

cleanup7:
    i=0;
    lprintf( log, INFO, "Killing request dispatcher thread #%d...", 
            dispatchers[i] );
    if( pthread_cancel(dispatchers[i]) != 0 ) {
        lprintf(log, ERROR, "Could not cancel dispatcher thread %d",
            dispatchers[i] );
    }
    pthread_join(dispatchers[i], NULL);

cleanup6:
    close(socks[1]);

cleanup5:
    i=1;
    lprintf( log, INFO, "Killing request dispatcher thread #%d...", 
            dispatchers[i] );
    if( pthread_cancel(dispatchers[i]) != 0 ) {
        lprintf(log, ERROR, "Could not cancel dispatcher thread %d",
            dispatchers[i] );
    }
    pthread_join(dispatchers[i], NULL);

cleanup4:
    close(socks[0]);

cleanup3:
    lprintf(log, INFO, "Freeing client data list...");
    free_clidata_list(&clients);

cleanup2:
    /* Kill the threads in the thread pool */
    lprintf( log, INFO, "Killing thread pool..." );
    if( tpool_destroy(tpool, 1) == -1 ) {
        lprintf(log, ERROR, "Could not destroy thread pool!");
    }

cleanup1:
    lprintf( log, INFO, "HTun server daemon exiting." );
    log_close(log);

    return EXIT_SUCCESS;
}

