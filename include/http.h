/* -------------------------------------------------------------------------
 * http.h - htun defs for HTTP-related functions
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
/* $Id: http.h,v 2.35 2002/08/13 21:35:41 jehsom Exp $ */

#ifndef __HTTP_H
#define __HTTP_H

#include <netinet/in.h>

#define MATCH_204_HTTP10  "HTTP/1.0 204 "
#define MATCH_204_HTTP11  "HTTP/1.1 204 "
#define MATCH_200_HTTP10  "HTTP/1.0 200 "
#define MATCH_200_HTTP11  "HTTP/1.1 200 "
#define HDR_PROXY_CONNECTION "Proxy-Connection: "
#define HDR_CONTENT_LENGTH "Content-Length: "
#define HDR_CONNECTION "Connection: "
#define HDR_CONTENT_TYPE "Content-Type: "
#define HDR_HOST "Host: "
#define PROXY_AUTH_LINE "%s%s"
#define HDR_PROXY_AUTH "Proxy-Authorization: Basic "

#define BODY_500_BUSY "Sorry, the server is too busy to process your " \
                     "request, or the client limit has been reached. " \
                     "Try again later.\n"
#define RESPONSE_500_BUSY "HTTP/1.0 500 Busy\r\n" \
                     HDR_CONNECTION "Close\r\n" \
                     HDR_CONTENT_LENGTH "%d\r\n" \
                     HDR_CONTENT_TYPE "text/plain\r\n" \
                     "\r\n" \
                     BODY_500_BUSY, (sizeof(BODY_500_BUSY)-1)
#define BODY_500_ERR "An server error occurred while processing your " \
                     "request. Please contact the system administrator.\n"
#define RESPONSE_500_ERR "HTTP/1.0 500 Internal Server Error\r\n" \
                     HDR_CONNECTION "Close\r\n" \
                     HDR_CONTENT_LENGTH "%d\r\n" \
                     HDR_CONTENT_TYPE "text/plain\r\n" \
                     "\r\n" \
                     BODY_500_ERR, (sizeof(BODY_500_ERR)-1)
#define BODY_501     "Sorry, I don't know how to service your request"
#define RESPONSE_501 "HTTP/1.0 501 Not Implemented\r\n" \
                     HDR_CONNECTION "Close\r\n" \
                     HDR_CONTENT_LENGTH "%d\r\n" \
                     HDR_CONTENT_TYPE "text/plain\r\n" \
                     "\r\n" \
                     BODY_501, (sizeof(BODY_501)-1)
#define BODY_503     "Sorry, could not assign IP address within range.\n"
#define RESPONSE_503 "HTTP/1.0 503 Service Unavailable\r\n" \
                     HDR_CONNECTION "Close\r\n" \
                     HDR_CONTENT_LENGTH "%d\r\n" \
                     HDR_CONTENT_TYPE "text/plain\r\n" \
                     "\r\n" \
                     BODY_503, (sizeof(BODY_503)-1)
#define RESPONSE_200 "HTTP/1.0 200 OK\r\n" \
                     HDR_CONNECTION "Keep-Alive\r\n" \
                     HDR_CONTENT_LENGTH "%lu\r\n" \
                     "\r\n" \
                     "%s"
#define RESPONSE_200_NOBODY \
                     "HTTP/1.0 200 OK\r\n" \
                     HDR_CONNECTION "Keep-Alive\r\n" \
                     HDR_CONTENT_LENGTH "%d\r\n" \
                     "\r\n"
#define BODY_400     "Your user agent sent an invalid request.\n"
#define RESPONSE_400 "HTTP/1.0 400 Bad Request\r\n" \
                     HDR_CONNECTION "Close\r\n" \
                     HDR_CONTENT_LENGTH "%lu\r\n" \
                     HDR_CONTENT_TYPE "text/plain\r\n" \
                     "\r\n" \
                     BODY_400, (sizeof(BODY_400)-1)
#define BODY_412     "That MAC address has no registered send channel up. " \
                     "Connect the send channel before the receive channel\n"
#define RESPONSE_412 "HTTP/1.0 412 Precondition Failed\r\n" \
                     HDR_CONNECTION "Close\r\n" \
                     HDR_CONTENT_LENGTH "%lu\r\n" \
                     "\r\n" \
                     BODY_412, (sizeof(BODY_412)-1)
#define RESPONSE_204 "HTTP/1.0 204 No Data\r\n" \
                     HDR_CONNECTION "Keep-Alive\r\n" \
                     HDR_CONTENT_LENGTH "0\r\n" \
                     "\r\n"

#define REQ_P2_CS   "POST http://%s:%d/CP2 HTTP/1.0\r\n" \
                    HDR_PROXY_CONNECTION "Keep-Alive\r\n" \
                    HDR_CONTENT_LENGTH "%d\r\n" \
                    PROXY_AUTH_LINE \
                    "\r\n"
#define REQ_P2_CR   "POST http://%s:%d/CR HTTP/1.0\r\n" \
                    HDR_PROXY_CONNECTION "Keep-Alive\r\n" \
                    HDR_CONTENT_LENGTH "%d\r\n" \
                    PROXY_AUTH_LINE \
                    "\r\n"
#define REQ_P2_R    "POST http://%s:%d/R HTTP/1.0\r\n" \
                    HDR_PROXY_CONNECTION "Keep-Alive\r\n" \
                    HDR_CONTENT_LENGTH "%d\r\n" \
                    PROXY_AUTH_LINE \
                    "\r\n"
#define REQ_P2_S    "POST http://%s:%d/S HTTP/1.0\r\n" \
                    HDR_PROXY_CONNECTION "Keep-Alive\r\n" \
                    HDR_CONTENT_LENGTH "%d\r\n" \
                    PROXY_AUTH_LINE \
                    "\r\n"
#define REQ_P1_S    REQ_P2_S
#define REQ_P1_P    "POST http://%s:%d/P HTTP/1.0\r\n" \
                    HDR_PROXY_CONNECTION "Keep-Alive\r\n" \
                    HDR_CONTENT_LENGTH "2\r\n" \
                    PROXY_AUTH_LINE \
                    "\r\n" \
                    ":)"
#define REQ_P2_F    "POST http://%s:%d/F HTTP/1.0\r\n" \
                    HDR_PROXY_CONNECTION "Close\r\n" \
                    HDR_CONTENT_LENGTH "2\r\n" \
                    PROXY_AUTH_LINE \
                    "\r\n" \
                    ":("
#define REQ_P1_F    REQ_P2_F

#define REQ_P1_CS   "POST http://%s:%d/CP1 HTTP/1.0\r\n" \
                    HDR_PROXY_CONNECTION "Keep-Alive\r\n" \
                    HDR_CONTENT_LENGTH "%d\r\n" \
                    PROXY_AUTH_LINE \
                    "\r\n"

#define P1_CS 1
#define P1_S  2
#define P1_P  3
#define P1_F  4
#define P2_CS 5
#define P2_CR 6
#define P2_S  7
#define P2_R  8
#define P2_F  9

#define HTTP_HEADER_MAX 256
#define HTTP_HEADERS_MAX 65536
#define HTTP_REQUESTLINE_MAX 1024

#define HTTP_METHOD_MAX 10

/* 
 * Following are #defines that parse_request returns, indicating what type of
 * request was given on the HTTP request line.
 */
#define REQ_NONE -1
#define REQ_ERR 0
#define REQ_GET 1
#define REQ_CP1 2
#define REQ_S   3
#define REQ_CP2 4
#define REQ_CR  5
#define REQ_R   6
#define REQ_F   7
#define REQ_P   8
                   
#define max(a,b) ((a)>(b)?(a):(b))
#define min(a,b) ((b)>(a)?(a):(b))

typedef struct {
    char method[HTTP_METHOD_MAX];
    struct in_addr cli_ip;
    int pollonly;
} http_request_t;
                   
/* 
 * Return a pointer to the location in headers corresponding to the start of
 * the value of the header field given in header_name.  
 */
char *header_value( const char *headers, const char *header_name );

/*
 * Gets all HTTP headers from the socket fd, and puts at most len bytes of
 * them into buf.
 */
int getheaders( int fd, char *buf, size_t len );

/* 
 * Reads the request line from clisock and returns one of the REQ_* types
 * above depending on the type of request made.  returns -1 on failure.
 * If you supply a buffer in reqbuf, parse_request will place the full request
 * into the buffer so you can use it later. Buffer must be at least size
 * HTTP_REQUESTLINE_MAX to prevent buffer overflows.
 */
int parse_request( int clisock, char *reqbuf );

/*
 * Handles a post request on the server
 */
int handle_post( int clisock );

/*
 * Gets the body of an HTTP request from the file descriptor fd, based on the
 * content length given in the supplied headers, and returns it in a
 * DYNAMICALLY ALLOCATED BUFFER. Puts the length of the data into *len.
 */
char *getbody( int fd, char *headers, int *len );

/*
 * Sends a 503 error to the client
 */
void send_err( int clisock );

int proxy_request( int clisock, char *req, char *hdrs );

/* Returns the content length from the headers, or -1 on error */
int get_content_length( char *headers );

#endif /* __HTTP_H */
