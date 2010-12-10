/* -------------------------------------------------------------------------
 * gram.y - htun config grammar
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
/* $Id: gram.y,v 2.29 2003/01/24 01:20:50 jehsom Exp $ */

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "common.h"
#include "iprange.h"
#include "util.h"

extern int lineno;
extern int yylineno;
config_data_t *config; /* the config struct, created in the lexer */
char *linehead, *textpoint;
%}

%union {
    char* name; /* ip, mode, tun file, etc.. everything in here goes */
}


/* global option tokens */
%token DEMONIZE TEST TUN_FILE LOG_FILE ANSWER FNAME 

/* grammar related tokens */
%token SPACE NEWLINE LEFT_BRACE RIGHT_BRACE CLIENT SERVER OPTION

/* client option tokens */
%token DO_ROUTING SERVER_IP PROXY_IP IP SERVER_PORT PROXY_PORT PORT 
%token NUM IP_RANGE RANGE MAX_POLL_INTERVAL MIN_POLL_INTERVAL_MSEC
%token POLL_BACKOFF_RATE ETHDEV IFNAME ACKWAIT PROTOCOL PROXY_USER USER
%token PROXY_PASS PASS CON_T RECON_T RECON_SLEEP CHAN2_IDLE

/* server option tokens - some are shaed with the client, eg SERVER_PORT */
%token SRV_RESPONSE_DELAY MAX_CLIENTS MAX_PENDING IDLE_DISCONNECT CLIDATA_TIMEOUT SERVER_PORT_2
%token REDIR_HOST REDIR_PORT TEXT MIN_NACK_DELAY PKT_COUNT_THRESHOLD PKT_MAX_INTERVAL MAX_RESPONSE_DELAY

%start config 
%%

space:      SPACE
        |   SPACE space
        ;

newlines:   NEWLINE
        |   NEWLINE newlines 
        ;

emptiness:
        |   SPACE emptiness
        |   NEWLINE emptiness
        ;

config:     emptiness options_decl emptiness specific_decl { return; }
        |   emptiness specific_decl emptiness options_decl { return; }
        |   emptiness options_decl emptiness { return; /* client/server mode not spcified
                                                          will generate error in read_config */ }
        ;

open:       LEFT_BRACE newlines
        |   LEFT_BRACE space newlines
        ;

close:      RIGHT_BRACE NEWLINE
        |   RIGHT_BRACE space NEWLINE
        ;

option:    OPTION
        |  OPTION space
        ;

options_decl:       option open g_rules close
                ;

client:   CLIENT
        | CLIENT space
        ;

server:   SERVER
        | SERVER space
        ;

specific_decl:      client open c_rules close   { config->is_server = 0; }
                |   server open s_rules close   { config->is_server = 1; }
                ;

g_rules:    g_rule
        |   g_rule g_rules
        ;

g_rule:     space g_opt space newlines
        |   g_opt space newlines
        |   space g_opt newlines
        |   g_opt newlines
        ;

g_opt:     DEMONIZE space ANSWER {
                                    config->demonize = get_answer(yylval.name,"yes","no"); 
                                 }
          | TEST space ANSWER {
                                config->debug = get_answer(yylval.name,"yes","no");
                              }
          | TUN_FILE space FNAME {
                                    memset(config->tunfile, '\0', PATH_MAX);
                                    snprintf(config->tunfile, PATH_MAX-1, "%s", yylval.name);
                                 }
          | LOG_FILE space FNAME {
                                    memset(config->logfile, '\0', PATH_MAX);
                                    snprintf(config->logfile, PATH_MAX-1, "%s", yylval.name);
                                 }
          ;

c_rules:    c_rule
        |   c_rule c_rules
        ;

c_rule:     space c_opt space newlines
        |   c_opt space newlines
        |   space c_opt newlines
        |   c_opt newlines
        ;

c_opt:   DO_ROUTING space ANSWER 
            { 
                config->u.c.do_routing = get_answer(yylval.name, "yes", "no"); 
                //yylval.name = "";
            }
       | SERVER_IP space IP 
            {
                set_ip(&config->u.c.server_ip, config->u.c.server_ip_str, yylval.name);
            }
       | PROXY_IP space IP 
            {
                set_ip(&config->u.c.proxy_ip, config->u.c.proxy_ip_str, yylval.name);
            }
       | SERVER_PORT space PORT 
            {
                config->u.c.server_ports[0] = htons( atol(yylval.name) );
            }
       | SERVER_PORT_2 space PORT 
            {
                config->u.c.server_ports[1] = htons( atol(yylval.name) );
            }
       | PROXY_PORT space PORT 
            {
                config->u.c.proxy_port = htons( atol(yylval.name) );
            }
       | MAX_POLL_INTERVAL space NUM 
            {
                config->u.c.max_poll_interval = atoi(yylval.name);
            }
       | MIN_POLL_INTERVAL_MSEC space NUM 
            {
                config->u.c.min_poll_interval_msec = atoi(yylval.name);
            }
       | POLL_BACKOFF_RATE space NUM 
            {
                config->u.c.poll_backoff_rate = atoi(yylval.name);
            }
       | ETHDEV space IFNAME 
            {
                snprintf(config->u.c.if_name, 16, "%s", yylval.name);
            }
       | ACKWAIT space NUM 
            {
                config->u.c.ack_wait = atoi(yylval.name);
            }
       | PROTOCOL space NUM 
            {
                if( strcmp(yylval.name,"2") == 0 ) {
                    config->u.c.protocol = 2;
                } else if( strcmp(yylval.name,"1") == 0 ) {
                    config->u.c.protocol = 1;
                } else {
                    yy_error("unrecognized protocol", "must be 1 or 2");
                }
            }
       | IP_RANGE space RANGE   
            {
                if( !add_iprange(&config->u.c.ipr, yylval.name) ) {
                    die_error(yylineno, "not a valid range", 
                        "must be \"ip_address/maskbits\"");
                }
            }
       | PROXY_USER space USER 
           {
                strncpy(config->u.c.proxy_user, yylval.name, 40);
                if( *config->u.c.proxy_pass ) set_base64_user_pass();
           }
       | PROXY_PASS space PASS 
            {
                strncpy(config->u.c.proxy_pass, yylval.name, 80);
                if( *config->u.c.proxy_user ) set_base64_user_pass();
            }
       | CON_T space NUM
            {
                config->u.c.connect_tries = atoi(yylval.name);
            }
       | RECON_T space NUM
            {
                config->u.c.reconnect_tries = atoi(yylval.name);
            }
       | RECON_SLEEP space NUM
            {
                config->u.c.reconnect_sleep_sec = atoi(yylval.name);
            }
       | CHAN2_IDLE space NUM
            {
                config->u.c.channel_2_idle_allow = atoi(yylval.name);
            }
       ;

s_rules:    s_rule
        |   s_rule s_rules
        ;

s_rule:     space s_opt space newlines
        |   s_opt space newlines
        |   space s_opt newlines
        |   s_opt newlines
        ;

s_opt: MAX_CLIENTS space NUM 
            {
                config->u.s.max_clients = atoi(yylval.name);
            }
       | MAX_PENDING space NUM 
            {
                config->u.s.max_pending = atoi(yylval.name);
            }
       | IDLE_DISCONNECT space NUM 
            {
                config->u.s.idle_disconnect = atoi(yylval.name);
            }
       | CLIDATA_TIMEOUT space NUM 
            {
                config->u.s.clidata_timeout = atoi(yylval.name);
            }
       | SERVER_PORT space PORT  
            {
                config->u.s.server_ports[0] = htons( atol(yylval.name) );
            }
       | SERVER_PORT_2 space PORT 
            {
                config->u.s.server_ports[1] = htons( atol(yylval.name) );
            }
       | REDIR_HOST space TEXT  
            {
                config->u.s.redir_host = strdup(yylval.name);
            }
       | REDIR_PORT space PORT  
            {
                config->u.s.redir_port = htons( atol(yylval.name) );
            }
       | MIN_NACK_DELAY space NUM 
            {
                config->u.s.min_nack_delay = atoi( yylval.name );
            }
       | PKT_COUNT_THRESHOLD space NUM 
            {
                config->u.s.packet_count_threshold = atoi( yylval.name );
            }
       | PKT_MAX_INTERVAL space NUM 
            {
                config->u.s.packet_max_interval = atoi( yylval.name );
            }
       | MAX_RESPONSE_DELAY space NUM 
            {
                config->u.s.max_response_delay = atoi( yylval.name );
            }
       | IP_RANGE space RANGE 
            {
                if( !add_iprange(&config->u.s.ipr, yylval.name) ) {
                    die_error(yylineno, "not a valid range", 
                        "must be \"ip_address/maskbits\"");
                }
            }
       ;
%%

void die_error(int line, char *s1, char *s2)
{
    char *c;
    int i;

    fprintf(stderr, "Error parsing configuraton file\n");
    fprintf(stderr, "%s: line: %d, %s\n", s1, line, s2);

    if( (c=strchr(linehead, '\n')) ) *c = '\0';
    fprintf(stderr, " line text: \"%s\"\n", linehead);
    fprintf(stderr, "error near: -");
    for( i = textpoint - linehead; i > 0; i-- ) fprintf(stderr, "-");
    fprintf(stderr, "^\n");
    /* if( c ) *c = '\n'; */
    exit(EXIT_FAILURE);
}

void yyerror(char *s)
{
    die_error(yylineno, s, "");
}

void yy_error(char *s1, char *s2)
{
    die_error(yylineno, s1, s2);
}

static void set_ip(struct in_addr *a, char *dst, char *src)
{
    struct hostent host, *result = &host;
    char buf[4096];

    if( (result=resolve(src, &host, buf, sizeof(buf))) == NULL ) {
        yy_error("Invalid value or could not resolve", 
            "Must be a hostname or IP address");
    }

    a->s_addr = *(unsigned long *)(result->h_addr_list[0]);
    strncpy(dst, inet_ntoa(*a), 15);
}

static int get_answer(char *name, char *true_val, char *false_val)
{
    if(strstr(name, true_val) != NULL)
        return 1;
    else if(strstr(name, false_val) != NULL)
        return 0;
    else
        yy_error("invalid answer", "answer must be 'yes' or 'no'" );
}

static inline set_base64_user_pass( void ) 
{
    char user_pass[90];

    strcpy(user_pass, config->u.c.proxy_user);
    strcat(user_pass, ":");
    strcat(user_pass, config->u.c.proxy_pass);
    base64_encode(config->u.c.base64_user_pass, user_pass,
        strlen(user_pass)); 
}
