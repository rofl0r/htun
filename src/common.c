/* -------------------------------------------------------------------------
 * common.c - htun common miscellaneous functions
 * Copyright (C) 2002 Moshe Jacobson <moshe@runslinux.net>,
 *                    Ola Nordstr÷m <ola@triblock.com>
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
/* $Id: common.c,v 2.23 2002/11/23 21:45:59 ola Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pwd.h>

#include "y.tab.h"
#include "log.h"
#include "common.h"

int tunfd;
char *signames[64];

/* Read exactly one packet from fd in a dynamic buffer */
char *get_packet( int fd ) {
    size_t len=0;
    char *pkt = malloc(HTUN_MAXPACKET);
    size_t cnt;
    struct stat st;
    int rc;

    dprintf( log, DEBUG, "Entering get_packet()" );

    if(!pkt) {
        lprintf( log, ERROR, "Unable to malloc() space for next packet!\n" );
        return NULL;
    }

    if( fstat(fd,&st) == -1 ) {
        lprintf( log, ERROR, "Unable to fstat() fd #%d: %s", fd,
                strerror(errno) );
        free(pkt);
        return NULL;
    }

    /* We have to treat the socket and the tunfile differently */
    if( S_ISSOCK(st.st_mode) ) {

        /* read 20 bytes, the size of an IP header */
        cnt=0;
        while( cnt < 20 ) {
            if( (rc=read(fd,pkt+cnt,20)) <= 0 ) {
                if( rc < 0 ) {
                    if( errno == EINTR ) continue;
                    lprintf( log, WARN, "Socket #%d: Reading IP hdr: %s.",
                            fd, strerror(errno) );
                } else if( rc == 0 ) {
                    lprintf( log, WARN, 
                            "Socket #%d: Read %d bytes, expected 20 (hdr).",
                            fd, cnt );
                }
                free(pkt);
                return NULL;
            }
            cnt += rc;
        }

        len=iplen(pkt);

        /* Read the rest of the packet */
        while( cnt < len ) {
            if( (rc=read(fd,pkt+cnt,len-cnt)) <= 0 ) {
                if( rc < 0 ) {
                    if( errno == EINTR ) continue;
                    lprintf( log, WARN, "Socket #%d: Reading IP pkt: %s.",
                            fd, strerror(errno) );
                } else if( rc == 0 ) {
                    lprintf( log, WARN, 
                            "Socket #%d: Read %d bytes, expected %lu.",
                            fd, cnt, len );
                }
                free(pkt);
                return NULL;
            }
            cnt += rc;
        }

    } else if( S_ISCHR(st.st_mode) ) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        do {
            if( (rc=read(fd,pkt,HTUN_MAXPACKET)) == -1 ) {
                lprintf( log, INFO, 
                        "Reading IP pkt from tun fd #%d: %s",
                        fd, strerror(errno) );
            }
        } while( rc == -1 && errno == EINTR );

        if( rc == -1 ) {
            free(pkt);
            pkt = NULL;
        } else {
            len=iplen(pkt);
        }
    } else {
        lprintf( log, ERROR, 
                "fd #%d neither socket nor char special.", fd);
        return NULL;
    }

    dprintf( log, DEBUG, "Got %lu-byte pkt from %s #%d.", 
            len, S_ISCHR(st.st_mode)?"tunfd":"socket", fd );

    return pkt;
}

void print_client_config( struct client_config *c )
{
    iprange_t *ipr = c->ipr;
    if( c == NULL ) {
        fprintf(stderr, "Configfile is empty!\n");
        return;
    }
    lprintf( log, INFO, "protocol %d\n", c->protocol );
    lprintf( log, INFO, "connect tries: %d\n", c->connect_tries );
    lprintf( log, INFO, "reconnect tries: %d\n", c->reconnect_tries );
    lprintf( log, INFO, "reconnect sleep time: %d\n", c->reconnect_sleep_sec );
    lprintf( log, INFO, "channel 2 idle allowable time: %d\n", 
            c->channel_2_idle_allow);
    lprintf( log, INFO, "route table is %s\n", 
            c->do_routing ? "modified" : "not modified" );
    lprintf( log, INFO, "proxy ip: %s\n", c->proxy_ip_str );
    lprintf( log, INFO, "proxy port: %u\n", ntohs( c->proxy_port ));
    lprintf( log, INFO, "proxy user: '%s'\n", c->proxy_user );
    lprintf( log, INFO, "proxy pass: '%s'\n", c->proxy_pass );
    lprintf( log, INFO, "base64 user/pass: %s\n", c->base64_user_pass );
    lprintf( log, INFO, "server ip: %s\n", c->server_ip_str );
    lprintf( log, INFO, "server port: %u\n", ntohs( c->server_ports[0] ));
    lprintf( log, INFO, "secondary server port: %u\n", 
            ntohs( c->server_ports[1] ));
    lprintf( log, INFO, "local ip: %s\n", c->local_ip_str );
    lprintf( log, INFO, "peer ip: %s\n", c->peer_ip_str );

    lprintf( log, INFO, "max_poll_interval: %u\n", c->max_poll_interval);
    lprintf( log, INFO, "min_poll_interval_msec: %u\n", c->min_poll_interval_msec);
    lprintf( log, INFO, "poll_backoff_rate: %u\n", c->poll_backoff_rate);
    lprintf( log, INFO, "if_name: %s\n", c->if_name);
    lprintf( log, INFO, "ack_wait: %d\n", c->ack_wait);
    while( ipr != NULL ) {
        lprintf( log, INFO, "iprange: %s, bits: %d\n",
                inet_ntoa(ipr->net), ipr->maskbits);
        ipr = ipr->next;
    }
}

void print_server_config( struct server_config *s )
{
    iprange_t *ipr = s->ipr;

    lprintf( log, INFO, "max_clients: %u\n", s->max_clients);
    lprintf( log, INFO, "max_pending: %u\n", s->max_pending);
    lprintf( log, INFO, "server port: %u\n", ntohs( s->server_ports[0] ));
    lprintf( log, INFO, "secondary server port: %u\n", 
            ntohs( s->server_ports[1] ));
    lprintf( log, INFO, "idle_disconnect: %u\n", s->idle_disconnect);
    lprintf( log, INFO, "redirect_host: %s\n", s->redir_host);
    lprintf( log, INFO, "redirect_port: %u\n", ntohs(s->redir_port));
    lprintf( log, INFO, "min_nack_delay: %u\n", s->min_nack_delay);
    lprintf( log, INFO, "packet_count_threshold: %u\n",
            s->packet_count_threshold);
    lprintf( log, INFO, "packet_max_interval: %u\n",
            s->packet_max_interval);
    lprintf( log, INFO, "max_response_delay: %u\n",
            s->max_response_delay);
    while( ipr != NULL ) {
        lprintf( log, INFO, "iprange: %s, bits: %d\n",
                inet_ntoa(ipr->net), ipr->maskbits);
        ipr = ipr->next;
    }
}

/* dumps the config struct to the screen */
void print_config( config_data_t *configfile )
{
    if( configfile == NULL ) {
        fprintf(stderr, "Configfile is empty!\n");
        return;
    }

    lprintf( log, INFO, "--------- Begin Config Info -----------" );
    lprintf( log, INFO, "mode: %s\n", 
            configfile->is_server ? "server" : "client" );
    if( configfile->is_server )
        print_server_config( &configfile->u.s );
    else
        print_client_config( &configfile->u.c );
    lprintf( log, INFO, "config file: %s\n", configfile->cfgfile);
    lprintf( log, INFO, "tunfile: %s\n", configfile->tunfile);
    lprintf( log, INFO, "logfile: %s\n", configfile->logfile);
    lprintf( log, INFO, "debugging is: %s\n", 
            configfile->debug ? "on" : "off" );
    lprintf( log, INFO, "server is run in the %s\n", 
            configfile->demonize ? "background" : "foreground" );
    lprintf( log, INFO, "---------- End Config Info ------------" );
}

/* Become a daemon: fork, die, setsid, fork, die, disconnect */
void daemonize( void ) {
   pid_t pid ;

   fprintf( stderr, "HTun daemon backgrounding.\n" );
   pid = fork();
   if( pid < 0 ) lprintf( log, ERROR, "Unable to fork()!" );
   if( pid > 0 ) _exit(0) ;   /* parent exits */

   setsid();

   pid = fork() ;
   if( pid < 0 ) lprintf( log, ERROR, "Unable to fork()!" );
   if( pid > 0 ) _exit(0);   /* parent exits */

   chdir("/");
   freopen("/dev/null","r",stdin);
   freopen("/dev/null","w",stdout);
   freopen("/dev/null","w",stderr);
}

/*
 * checks the sanity of the config file struct
 * will printf to stderr if log is not functioning
 *  0  = success
 * -1  = if failed
 */
int config_check( config_data_t *c )
{
    /* check for must have common stuff here */
    if(strlen(c->tunfile) < 1) {
        lprintf(log, FATAL, "No valide tunfile specified\n");
    }

    if(c->is_server) {
        /* check for must have server stuff here */
        


        ;
    } else {

        ;
        /* check for must have client stuff */
    }

    return 0;
}

void dropprivs(char *str) {
    struct passwd *nonpriv = getpwnam("nobody");

    if( !nonpriv ) return;

    setreuid(-1,nonpriv->pw_uid);
    setregid(-1,nonpriv->pw_gid);
    if( str && *str ) 
        lprintf( log, INFO, "Dropped privs to 'nobody' (%s)", str );
    return;
}

void getprivs(char *str) {
    int rc = setreuid(0,0) | setregid(0,0);
    if( rc == -1 ) {
        lprintf( log, ERROR, "Unable to gain superuser privileges: %s",
                strerror(errno) );
    } else {
        if( str && *str ) 
            lprintf( log, INFO, "Got superuser privleges (%s)", str );
    }
    return;
}

void init_signames(void) {
    signames[SIGHUP] = "SIGHUP";
    signames[SIGINT] = "SIGINT";
    signames[SIGQUIT] = "SIGQUIT";
    signames[SIGILL] = "SIGILL";
    signames[SIGTRAP] = "SIGTRAP";
    signames[SIGABRT] = "SIGABRT";
    signames[SIGIOT] = "SIGIOT";
    signames[SIGBUS] = "SIGBUS";
    signames[SIGFPE] = "SIGFPE";
    signames[SIGKILL] = "SIGKILL";
    signames[SIGUSR1] = "SIGUSR1";
    signames[SIGSEGV] = "SIGSEGV";
    signames[SIGUSR2] = "SIGUSR2";
    signames[SIGPIPE] = "SIGPIPE";
    signames[SIGALRM] = "SIGALRM";
    signames[SIGTERM] = "SIGTERM";
    signames[SIGSTKFLT] = "SIGSTKFLT";
    signames[SIGCLD] = "SIGCLD";
    signames[SIGCHLD] = "SIGCHLD";
    signames[SIGCONT] = "SIGCONT";
    signames[SIGSTOP] = "SIGSTOP";
    signames[SIGTSTP] = "SIGTSTP";
    signames[SIGTTIN] = "SIGTTIN";
    signames[SIGTTOU] = "SIGTTOU";
    signames[SIGURG] = "SIGURG";
    signames[SIGXCPU] = "SIGXCPU";
    signames[SIGXFSZ] = "SIGXFSZ";
    signames[SIGVTALRM] = "SIGVTALRM";
    signames[SIGPROF] = "SIGPROF";
    signames[SIGWINCH] = "SIGWINCH";
    signames[SIGPOLL] = "SIGPOLL";
    signames[SIGIO] = "SIGIO";
    signames[SIGPWR] = "SIGPWR";
    signames[SIGSYS] = "SIGSYS";
    signames[SIGUNUSED] = "SIGUNUSED";

    return;
}
