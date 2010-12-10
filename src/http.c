/* -------------------------------------------------------------------------
 * http.c - htun functions for communicating with the http proxy
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
/* $Id: http.c,v 2.30 2002/08/24 22:54:42 jehsom Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "http.h"
#include "common.h"
#include "log.h"
#include "util.h"

int getheaders( int fd, char *buf, size_t len ){
    char line[HTTP_HEADER_MAX], *cp;

    *line=*buf='\0';
    /* Keep getting lines until we get a blank line ("\n" or "\r\n") */
    while( !(*line == '\n' || (line[0] == '\r' && line[1] == '\n')) ) {
        if( recvline( line, HTTP_HEADER_MAX, fd ) == NULL ) return -1;
        dprintf(log, DEBUG, "Just got header line: %s", line);
        strncat( buf, line, len-strlen(line)-5 );
        buf[len-1]='\0';
    }
    cp=buf+strlen(buf);
    while( cp > buf && (*(cp-1) == '\r' || *(cp-1) == '\n') ) cp--;
    strcpy( cp, "\r\n\r\n" );
    return 0;
}

inline char *header_value( const char *headers, const char *header_name ){
    char *cp;

    if( !headers ) return NULL;

    while( (cp=strstr(headers, header_name)) ) {
        if( cp == headers || cp[-1] == '\n' ) {
            if( !(cp=strchr(cp, ':')) ) continue;
            while (*(++cp) == ' ');
            return cp;
        }
    }
    return NULL;
}

inline int get_content_length( char *headers ) {
    char *cp = header_value(headers, HDR_CONTENT_LENGTH);
    int len;

    if( !cp ) return -1;
    if( cp[0] == '0' && isspace(cp[1]) ) return 0;
    len = strtol(cp, NULL, 0);
    return len ? len : -1;
}

char *getbody( int fd, char *headers, int *len ) {
    char *buf;
    
    *len = get_content_length(headers);
    if( *len < 1 ) { *len = 0; return NULL; }

    if( (buf=readloop(fd, *len)) == NULL ) return NULL;

    return buf;
}

/* 
 * Parses the request in line, and places the results in req.
 * returns -1 on failure, 0 on success.
 *
 * Requests are of the form (this is a regex):
 *
 * POST (http://x.x.x.x)?/(C(P[12])?|S|CR|R) HTTP/1.0
 *  where ### is one octect of an IP address, and the '?' is given only on a
 *  poll-only request.
 */
int parse_request( int clisock, char *reqbuf ) {
    char req[HTTP_REQUESTLINE_MAX];
    char *ptr = req;
    char **ptrptr = &ptr;
    char *method;
    char *uri;
    struct timeval tv;
    fd_set fds;
    int rc;

    tv.tv_usec = 0;
    tv.tv_sec = config->u.s.idle_disconnect;

    FD_ZERO(&fds);
    FD_SET(clisock, &fds);
    
    dprintf(log, DEBUG, "Entering select() on client fd #%d.", clisock);
    rc = select(clisock+1, &fds, NULL, NULL, &tv);
    if( rc == 0 ) {
        dprintf(log, WARN, 
                "select() on fd #%d timed out with no request.", clisock);
        return REQ_NONE;
    } else if ( rc == -1 ) {
        lprintf(log, WARN,
                "select() on fd #%d: %s", clisock, strerror(errno));
        return REQ_ERR;
    } else if ( recv(clisock, &req, 1, MSG_PEEK) == -1 && errno == EPIPE ) {
        lprintf(log, WARN,
                "select() on fd #%d exited: Connection reset by peer.",
                clisock);
        return REQ_ERR;
    } else {
        dprintf(log, DEBUG, "select() on fd %d retured with data.", clisock);
    }

    dprintf(log, DEBUG, 
            "Came out of select() on client fd #%d with data.", clisock);
 
    if( recvline(req, HTTP_REQUESTLINE_MAX, clisock) == NULL ) return REQ_ERR;
    
    chomp(req);

    if( reqbuf ) strcpy(reqbuf,req);

    dprintf( log, DEBUG, "parsing request: %s", req );
    
    /* If no space on the line, return */
    if( strchr(req, ' ') == NULL ) {
        lprintf(log, WARN, 
                "Client sent request with no space.");
        return REQ_ERR;
    }
 
    /* Get the method */
    if( (method=strtok_r(req, " ", ptrptr)) == NULL ) {
        lprintf(log, WARN, 
                "Could not tokenize request line method.");
        return REQ_ERR;
    }

    /* First look for a GET request */
    if( !xstrcasecmp(method, "get") ) {
        dprintf(log, DEBUG, "This is a GET request.");
        return REQ_GET;
    }

    /* Return error if it's not a post request */
    if( xstrcasecmp(method, "post") != 0 ) {
        dprintf(log, DEBUG, "This is neither POST nor GET.");
        return REQ_ERR;
    }
    
    /* Get the URI */
    if( (uri=strtok_r(NULL, " ", ptrptr)) == NULL ) {
        lprintf(log, WARN, 
                "Could not tokenize request line URI.");
        return REQ_ERR;
    }
    
    /* If we're now on http://, skip http://host.name and move to the IP */
    if( !strncmp(uri, "http://", 7) || !strncmp(uri, "https://", 8) ) {
        uri=strstr(uri,"//");
        uri+=2;
        if( (uri=strchr(uri,'/')) == NULL ) return REQ_ERR;
    }
    /* Skip the leading /  */
    uri++;

    chomp(uri);

    dprintf(log, DEBUG, "Should be URI: \"%s\"", uri);
 
    if( !xstrcasecmp(uri, "CP1") ) {
        dprintf(log, DEBUG, "This is a CP1 request");
        return REQ_CP1;
    } else if( !xstrcasecmp(uri, "P") ) {
        dprintf(log, DEBUG, "This is an P request");
        return REQ_P;
    } else if( !xstrcasecmp(uri, "S") ) {
        dprintf(log, DEBUG, "This is an S request");
        return REQ_S;
    } else if( !xstrcasecmp(uri, "CP2") ) {
        dprintf(log, DEBUG, "This is a CP2 request");
        return REQ_CP2;
    } else if( !xstrcasecmp(uri, "CR") ) {
        dprintf(log, DEBUG, "This is a CR request");
        return REQ_CR;
    } else if( !xstrcasecmp(uri, "R") ) {
        dprintf(log, DEBUG, "This is an R request");
        return REQ_R;
    } else if( !xstrcasecmp(uri, "F") ) {
        dprintf(log, DEBUG, "This is an F request");
        return REQ_F;
    } else {
        lprintf(log, WARN, "Unknown request: \"%s\"", uri);
        return REQ_ERR;
    }
}


/* FIXME: redir_host & redir_port should soon be in the config */
int proxy_request( int clisock, char *req, char *hdrs ) {
    int s, len;
    struct hostent host, *result = &host;
    char hostdata[4096];
    struct sockaddr_in addr;
    char *body;
    char srvhdrs[HTTP_HEADERS_MAX];
    char **lines = splitlines(hdrs);
    int i;

    if( !lines ) {
        lprintf(log, WARN, "Could not splitlines() on client headers");
        goto err_1;
    }

    chomp(req);

    if( (result=resolve(config->u.s.redir_host, &host, hostdata, sizeof(hostdata)))
            == NULL ) {
        lprintf(log, WARN, "Could not resolve host %s.",
                config->u.s.redir_host);
        goto err_1;
    }
    
    if( (s=socket(PF_INET,SOCK_STREAM,IPPROTO_TCP)) == -1 ) {
        lprintf(log, ERROR, "Could not get socket.");
        goto err_1;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = *(unsigned long *)(result->h_addr_list[0]);
    addr.sin_port = config->u.s.redir_port;

    if( connect(s, (struct sockaddr *)&addr, sizeof(addr)) == -1 ) {
        lprintf(log, WARN, "Could not connect to %s at IP %s:%d.",
                config->u.s.redir_host, inet_ntoa(addr.sin_addr),
                ntohs(addr.sin_port));
        goto err_2;
    }

    dprintf(log, DEBUG, "sending server: %s", req);
    fdprintf(s, "%s\r\n", req);

    for( i=0; lines[i]; i++ ) {
        if( !strncmp(lines[i], HDR_CONNECTION, sizeof(HDR_CONNECTION)-1) ||
            !strncmp(lines[i], HDR_HOST, sizeof(HDR_HOST)-1) ) continue;
        if( strlen(lines[i]) <= 1 ) continue;
        chomp(lines[i]);
        dprintf(log, DEBUG, "sending server: %s", lines[i]);
        fdprintf(s, "%s\r\n", lines[i]);
    }
    
    dprintf(log, DEBUG, "sending server: '" HDR_HOST "%s:%d\r\n'",
            config->u.s.redir_host, ntohs(config->u.s.redir_port));
    fdprintf(s, HDR_HOST "%s:%d\r\n", config->u.s.redir_host,
            ntohs(config->u.s.redir_port));
    dprintf(log, DEBUG, "sending server: '" HDR_CONNECTION "Close\r\n\r\n'");
    fdprintf(s, HDR_CONNECTION "Close\r\n\r\n");

    if( (body=getbody(clisock, hdrs, &len)) ) {
        write(s, body, len);
        free(body);
    }

    getheaders(s, srvhdrs, sizeof(srvhdrs));
    write(clisock, srvhdrs, strlen(srvhdrs));

    if( (body=getbody(s, srvhdrs, &len)) ) {
        write(clisock, body, len);
        free(body);
    }

    close(s);
    free(lines);
    return 0;

err_2:
    close(s);
err_1:
    free(lines);
    return -1;
}

