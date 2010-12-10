/* -------------------------------------------------------------------------
 * common.h - htun common defs
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
/* $Id: common.h,v 2.24 2003/02/22 21:14:10 ola Exp $ */

#ifndef __COMMON_H
#define __COMMON_H

#include <netinet/in.h>
#include <time.h>
#include "queue.h"
#include "log.h"
#include "server.h"
#include "iprange.h"

#define HTUN_MAXPACKET 65536
#define HTUN_DEFAULT_CFGFILE "/etc/htund.conf"
#define HTUN_MAXCLIENTS 10
#define HTUN_MAXPENDING 5
#define HTUN_SOCKPENDING 10

#ifdef _DEBUG
#define dprintf lprintf
#else
#define dprintf(...)
#endif

#define iplen(pkt) \
    ( (unsigned short)( ((((pkt)[6]&0xFF)<<8) | ((pkt)[7]&0xFF)) + 4 ) )

#define ipdst(pkt) htonl(((pkt)[20]<<24 | (pkt)[21]<<16 | (pkt)[22]<<8 | (pkt)[23]))
#define ipsrc(pkt) htonl(((pkt)[16]<<24 | (pkt)[17]<<16 | (pkt)[18]<<8 | (pkt)[19]))
#define max(a,b) ((a)>(b)?(a):(b))
#define min(a,b) ((b)>(a)?(a):(b))

struct server_config {
    unsigned short max_clients;
    unsigned short max_pending;
    unsigned short idle_disconnect;
    unsigned short server_ports[2];
    unsigned short min_nack_delay;
    unsigned short packet_count_threshold;
    unsigned long  packet_max_interval;
    unsigned short max_response_delay;
    time_t clidata_timeout;
    iprange_t *ipr;
    char *redir_host;
    unsigned short redir_port;
};

struct client_config {
    unsigned short proxy_port;
    unsigned short server_ports[2];
    struct in_addr proxy_ip;
    struct in_addr server_ip;
    struct in_addr local_ip;
    struct in_addr peer_ip;
    unsigned short do_routing;
    unsigned short max_poll_interval;
    unsigned long  min_poll_interval_msec;
    unsigned short poll_backoff_rate;
    int channel_2_idle_allow;
    int connect_tries;
    int reconnect_tries;
    int reconnect_sleep_sec;
    int protocol;
    int ack_wait;
    iprange_t *ipr;
    /* Put the large data at the end to speed up access to smaller data */
    char proxy_ip_str[16];
    char server_ip_str[16];
    char local_ip_str[16]; /* virual tun if */
    char peer_ip_str[16]; /* server virtual tun if */
    char if_name[16]; /* eth0, eth1 etc.. */
    char proxy_user[41];
    char proxy_pass[81];
    char base64_user_pass[300];
};

typedef struct {
    char cfgfile[PATH_MAX];
    char tunfile[PATH_MAX];
    char logfile[PATH_MAX];
    int is_server;
    int demonize;
    int debug;

    union {
        struct server_config s;
        struct client_config c; 
    } u;
} config_data_t;

/* Reads the configuration and returns a dynamically allocated struct with the
 * data */
config_data_t *read_config( const char *configfile );

/* dumps the config struct to the screen */
void print_config( config_data_t *configfile );

/* Reads exactly one packet's worth of data from tunfd and returns it in a
 * dynamically allocated char buffer. */
char *get_packet( int tunfd );

/* Become a daemon: fork, die, setsid, fork, die, disconnect */
void daemonize( void );

/* Check the validity of the configuration data */
int config_check( config_data_t *c );

/* Drop privileges to nobody. Optional reason for log */
void dropprivs(char *reason);

/* Get root privileges back. Optional reason for log */
void getprivs(char *reason);

/* initialize signames[] array */
void init_signames(void);

/* Variables used by other modules */
extern log_t *log;
extern config_data_t *config;
extern char *signames[64]; /* 64 names of signals */

#endif /* _COMMON_DEFS_H_ */
