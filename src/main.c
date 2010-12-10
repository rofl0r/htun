/* -------------------------------------------------------------------------
 * main.c - htun main functions
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
/* $Id: main.c,v 2.22 2002/08/15 04:07:03 jehsom Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <pwd.h>

#include "version.h"
#include "common.h"
#include "server.h"
#include "client.h"
#include "tun.h"

log_t *log;

static inline void usage(void) {
    fprintf( stderr,
        "Htun " VERSION "\n"
        "Usage: htund [OPTION]...\n"
        "Options:\n"
        "   -c cfgfile   Use cfgfile as the config file.\n"
        "   -f           Run htund in the foreground.\n"
#ifdef _DEBUG
        "   -d           Include debug-level messages in the log.\n"
#endif
        "   -v           Print the htund version information and exit.\n"
        "   -h           Print usage information and exit.\n" );
    fprintf( stderr,
        "   -t tunfile   Use tunfile as the tun device file.\n"
        "   -l logfile   Use logfile as the log output file.\n"
        "                 Give - for stdout; this implies -f.\n"
        "   -r           Do not alter default route.\n"
        "   -p port      Set server port to given value\n"
        "   -o           cOnfigtest only, check config syntax\n" );
    return;
}

int main( int argc, char *argv[] ) {
    sigset_t newmask;
    char c;
    extern char *optarg;
    extern int optind, opterr, optopt;
    char *cfgfile = HTUN_DEFAULT_CFGFILE;
    char *tunfile = NULL;
    char *logfile = NULL;
    unsigned short port=0;
    int foreground = 0;
    int dont_route = 0;
    int config_test_only = 0;
    int debug = 0;
    unsigned int logflags = 0;

    dropprivs("");

    while( (c=getopt(argc, argv, "rdfc:vht:l:p:o")) != -1 ) {
        switch(c) {
            case 'r':
                dont_route = 1;
                break;
/* #ifdef _DEBUG */
            case 'd':
                debug = 1;
                break;
/* #endif */
            case 'o':
                debug = 1;
                config_test_only = 1;
                break;
            case 'f':
                foreground = 1;
                break;
            case 'c':
                cfgfile = optarg;
                break;
            case 'v':
                fprintf( stderr, 
                  "HTun " VERSION "\n"
                  "(c) 2002  Moshe Jacobson <moshe+htun@runslinux.net>\n"
                  "      and Ola Nordstrom <ola@triblock.com>\n"
                  "This program is distributed under the GNU General Public License.\n"
                  "for details, see the LICENSE file packaged with htun.\n");
                return EXIT_SUCCESS;
            case 't':
                tunfile = optarg;
                break;
            case 'l':
                logfile = optarg;
                break;
            case 'p':
                port = htons((unsigned short)strtoul(optarg,NULL,0));
                if(!port) {
                    fprintf( stderr, "Invalid port specified: %s.\n", 
                            argv[optind] );
                    return EXIT_FAILURE;
                }
                break;
            case 'h':
                usage();
                return EXIT_SUCCESS;
            default:
                usage();
                return EXIT_FAILURE;
        }
    }
                
    /* Read the config */
    if( (config=read_config(cfgfile)) == NULL ) {
        fprintf( stderr, "Fatal: Reading cfgfile \"%s\": %s\n",
                cfgfile, strerror(errno) );
        return EXIT_FAILURE;
    }

    if( config_test_only == 1 ) {
        log = log_open("-", LOG_STDERR|LOG_NODATE|LOG_DEBUG|LOG_NOLVL|LOG_FUNC);
        print_config(config);
        log_close(log);
        return EXIT_SUCCESS;
    }

    /* Now override config values with cmdline values */
    strcpy(config->cfgfile,cfgfile);
    if( tunfile ) strncpy(config->tunfile,tunfile,PATH_MAX);
    if( logfile ) strncpy(config->logfile,logfile,PATH_MAX);
    if( port ) {
        if( config->is_server )
            config->u.s.server_ports[0] = htons(port);
        else
            config->u.c.proxy_port = (port);
    }
    if( dont_route ) config->u.c.do_routing = 0;
    if( debug ) config->debug = 1;
    

    /* Open the log file */
    if( config->debug ) logflags |= LOG_DEBUG;
    logflags |= LOG_FUNC;
    getprivs("opening logfile");
    if( !(log=log_open( config->logfile, logflags )) ) {
        fprintf( stderr, "Warning: Could not open logfile %s: %s\n",
                config->logfile, strerror(errno) );
    }
    dropprivs("logfile opened");

    /* Daemonize unless -f or -l - have been specified */
    if( !foreground && strcmp(config->logfile,"-") ) daemonize();
    
    lprintf( log, INFO, "HTun " VERSION " started." );

    init_signames();

    /* Ignore SIGPIPE. We will use errno=EPIPE instead. */
    signal(SIGPIPE,SIG_IGN);
    signal(SIGWINCH,SIG_IGN);

    /* Mask TERM, HUP, INT, and USR1 */
    sigemptyset(&newmask);
    sigaddset(&newmask, SIGINT);
    sigaddset(&newmask, SIGHUP);
    sigaddset(&newmask, SIGTERM);
    sigaddset(&newmask, SIGUSR1);
    sigaddset(&newmask, SIGUSR2);
    sigaddset(&newmask, SIGALRM);
    sigaddset(&newmask, SIGCONT);
    sigaddset(&newmask, SIGTSTP);
    sigaddset(&newmask, SIGWINCH);
    sigprocmask( SIG_BLOCK, &newmask, NULL );

    return config->is_server ? server_main() : client_main();
}
