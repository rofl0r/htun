/* -------------------------------------------------------------------------
 * parse.l - htun config parser
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
/* $Id: parse.l,v 2.24 2003/01/24 01:20:50 jehsom Exp $ */

%{
#include <stdio.h>
#include "common.h"
#include "y.tab.h" /* the generated yacc file */

extern config_data_t *config;
extern char *linehead, *textpoint;
int lineno = 1;
%}

comment [\t ]*[#|;].*\n
space   [\t ]+
ip      (([0-2]){0,1}([0-9]){1,2}\.){3}([0-2]){0,1}([0-9]){1,2}
port    [1-9][0-9]{0,4}
file    ((\/)|(\.\/))[a-zA-Z0-9\.\/]+
range   [0-9]*
text    [a-zA-Z0-9\.\/]*
type    (server|client)
num     [1-9][0-9]*
ans     (yes|no)
user    [^\n\t :]+
pass    [^ \t\n]*


    /* now we get the builtin push/pop state */
%option stack
%option yylineno
%s PRE_CLI PRE_SRV PRE_OPTIONS OPT
%x SRV CLI IP_S NUM_S ANS_S PORT_S FILE_S IPR IFN RDH USER_S PASS_S
%%

<*>\n               { linehead = yytext+1; } REJECT;
<*>.                { textpoint = yytext; if(!linehead) linehead = textpoint; } REJECT;
<*>{space}          { return SPACE; }
<*>{comment}        { return NEWLINE; }
<*>(\n)             { return NEWLINE; }
                     
<PRE_CLI>(\{)                   { yy_push_state(CLI); return LEFT_BRACE;}
<PRE_SRV>(\{)                   { yy_push_state(SRV); return LEFT_BRACE;}
<PRE_OPTIONS>(\{)               { yy_push_state(OPT); return LEFT_BRACE;}

<ANS_S>{ans}                    { yy_pop_state(); yylval.name = yytext; return ANSWER; }

<IP_S>{text}                    { yy_pop_state(); yylval.name = yytext; return IP; }

<NUM_S>{num}                    { yy_pop_state(); yylval.name = yytext; return NUM; }

<PORT_S>{port}                  { yy_pop_state(); yylval.name = yytext; return PORT; }

<FILE_S>{file}                  { yy_pop_state(); yylval.name = yytext; return FNAME; }

<IFN>{text}                     { yy_pop_state(); yylval.name = yytext; return IFNAME; }

<RDH>{text}                     { yy_pop_state(); yylval.name = yytext; return TEXT; }

<IPR>{ip}\/{range}              { yy_pop_state(); yylval.name = yytext; return RANGE; }

<USER_S>{user}                  { yy_pop_state(); yylval.name = yytext; return USER; }

<PASS_S>{pass}                  { yy_pop_state(); yylval.name = yytext; return PASS; }

<CLI>{ 
    (\})                       { BEGIN 0; return RIGHT_BRACE; }
    (do_routing)               { yy_push_state(ANS_S); return DO_ROUTING; }
    (protocol)                 { yy_push_state(NUM_S); return PROTOCOL; }

    (proxy_ip)                 { yy_push_state(IP_S); return PROXY_IP; }
    (proxy_port)               { yy_push_state(PORT_S); return PROXY_PORT; }
    (proxy_user)               { yy_push_state(USER_S); return PROXY_USER; }
    (proxy_pass)               { yy_push_state(PASS_S); return PROXY_PASS; }

    (server_ip)                { yy_push_state(IP_S); return SERVER_IP; }
    (server_port)              { yy_push_state(PORT_S); return SERVER_PORT; }
    (secondary_server_port)    { yy_push_state(PORT_S); return SERVER_PORT_2; }

    (if_name)                  { yy_push_state(IFN); return ETHDEV; }
    (iprange)                  { yy_push_state(IPR); return IP_RANGE; }

    (connect_tries)            { yy_push_state(NUM_S); return CON_T; }
    (reconnect_tries)          { yy_push_state(NUM_S); return RECON_T; }
    (reconnect_sleep_sec)      { yy_push_state(NUM_S); return RECON_SLEEP; }

    (channel_2_idle_allow)     { yy_push_state(NUM_S); return CHAN2_IDLE; }
    (min_poll_interval_msec)   { yy_push_state(NUM_S); return MIN_POLL_INTERVAL_MSEC; }
    (max_poll_interval)        { yy_push_state(NUM_S); return MAX_POLL_INTERVAL; }
    (poll_backoff_rate)        { yy_push_state(NUM_S); return POLL_BACKOFF_RATE; }
    (ack_wait)                 { yy_push_state(NUM_S); return ACKWAIT; }
}

<SRV>{
    (\})                       { BEGIN 0; return RIGHT_BRACE; }
    (iprange)                  { yy_push_state(IPR); return IP_RANGE; }

    /* Aliases. It's obvious it's a server port when it's in the server * section! */
    ((server_)?port)           { yy_push_state(PORT_S); return SERVER_PORT; }
    
    /* Aliases. It's obvious it's a server port when it's in the server * section! */
    (secondary_(server_)?port) { yy_push_state(PORT_S); return SERVER_PORT_2; }

    (max_clients)              { yy_push_state(NUM_S); return MAX_CLIENTS; }
    (redirect_host)            { yy_push_state(RDH); return REDIR_HOST; }
    (redirect_port)            { yy_push_state(PORT_S); return REDIR_PORT; }

    (max_pending)              { yy_push_state(NUM_S); return MAX_PENDING; }
    (idle_disconnect)          { yy_push_state(NUM_S); return IDLE_DISCONNECT; }
    (clidata_timeout)          { yy_push_state(NUM_S); return CLIDATA_TIMEOUT; }
    (min_nack_delay)           { yy_push_state(NUM_S); return MIN_NACK_DELAY; }
    (packet_count_threshold)   { yy_push_state(NUM_S); return PKT_COUNT_THRESHOLD; }
    (packet_max_interval)      { yy_push_state(NUM_S); return PKT_MAX_INTERVAL; }
    (max_response_delay)       { yy_push_state(NUM_S); return MAX_RESPONSE_DELAY; }
}

<OPT>{
    (daemonize)                { yy_push_state(ANS_S); return DEMONIZE; }
    (debug)                    { yy_push_state(ANS_S); return TEST; }
    (tunfile)                  { yy_push_state(FILE_S); return TUN_FILE; }
    (logfile)                  { yy_push_state(FILE_S); return LOG_FILE; }
    (\})                       { BEGIN 0; yylval.name = ""; return RIGHT_BRACE; }
}

(client)                        { yy_push_state(PRE_CLI); return CLIENT; }
(server)                        { yy_push_state(PRE_SRV); return SERVER; }
(options)                       { yy_push_state(PRE_OPTIONS); return OPTION; }

<*>(.)                          { return ERROR; }

%%

config_data_t *read_config( const char *configfile )
{
    FILE *fin = fopen( configfile, "r" );

    if( fin == NULL ) {
        fprintf(stderr, "Unable to open config file \"%s\"\n", configfile );
        return NULL;
    }

    yyin = fin;

    if((config = calloc(1, sizeof(config_data_t))) == NULL) {
        fprintf(stderr, "Fatal: unable to malloc() space for config data.\n");
        return NULL;
    }
    config->is_server = -1;

    strncpy(config->cfgfile, configfile, PATH_MAX);
    config->cfgfile[PATH_MAX-1] = '\0';

    yyparse();

    if( config->is_server == -1 ) {
        fprintf(stderr, "Error \"server\" or \"client\" not declared in config file.\n");
        exit(EXIT_FAILURE);
    }

    return config;
}
