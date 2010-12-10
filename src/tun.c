/* -------------------------------------------------------------------------
 * tun.c - htun functions for manipulating the tun network device
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
/* $Id: tun.c,v 2.29 2002/08/16 01:49:47 jehsom Exp $ */

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/route.h>

#include "log.h"
#include "common.h"
#include "clidata.h"
#include "iprange.h"
#include "tun.h"
#include "util.h"

static struct rtentry default_gw;

#define _PATH_PROCNET_ROUTE "/proc/net/route"
int store_default_gw(void) {
    char buf[1024], iface[16];
    int iflags, metric, refcnt, use, mss, window, irtt;
    unsigned long gate_addr=0, mask_addr, net_addr;
    FILE *fp = fopen(_PATH_PROCNET_ROUTE, "r");
    struct sockaddr_in *gw = (struct sockaddr_in*)&default_gw.rt_gateway;
    struct sockaddr_in *dst = (struct sockaddr_in*)&default_gw.rt_dst;
    struct sockaddr_in *mask = (struct sockaddr_in*)&default_gw.rt_genmask;

    memset(&default_gw, 0, sizeof(struct rtentry));
    
    /* Make sure we're talking about IP here */
    dst->sin_family = AF_INET;
    gw->sin_family = AF_INET;
    mask->sin_family = AF_INET;

    if(!fp) {
        lprintf( log, FATAL, "Unable to open %s: %s", _PATH_PROCNET_ROUTE,
                strerror(errno) );
        return -1;
    }

    /* discard the header line */
    fgets(buf,1023,fp);

    while (fgets(buf, 1023, fp)) {
        sscanf(buf, "%16s %lX %lX %X %d %d %d %lX %d %d %d\n", 
               iface, &net_addr, &gate_addr,
               &iflags, &refcnt, &use, &metric, &mask_addr,
               &mss, &window, &irtt);

        dprintf( log, DEBUG, "Got: %s %lX %lX %X %d %d %d %lX %d %d %d\n",
                iface, net_addr, gate_addr,
                iflags, refcnt, use, metric, mask_addr,
                mss, window, irtt);
        
        if( net_addr != 0 ) continue;

        dprintf( log, DEBUG, "Found default gw line in /proc/net/route:");
        dprintf( log, DEBUG, buf );
        
        /* Copy the addresses into the structs */
        default_gw.rt_flags = iflags;
        default_gw.rt_metric = metric;
        gw->sin_addr.s_addr = gate_addr;
        dst->sin_addr.s_addr = net_addr;
        mask->sin_addr.s_addr = mask_addr;

        dprintf( log, DEBUG, "Got default gateway of %s.",
                inet_ntoa(gw->sin_addr) );

        break;
    }

    if( gate_addr == 0 ) {
        lprintf( log, INFO, "No default gateway found." );
        return 0;
    }

    return 0;
}


/*
 * sets routing table's default gateway to
 * the sock peer addr
 */
int set_default_gw( int sockfd ) {
    struct sockaddr_in *dst, *gw, *mask;
    struct rtentry route;

    memset(&route,0,sizeof(struct rtentry));

    dst = (struct sockaddr_in *)(&(route.rt_dst));
    gw = (struct sockaddr_in *)(&(route.rt_gateway));
    mask = (struct sockaddr_in *)(&(route.rt_genmask));

    /* Make sure we're talking about IP here */
    dst->sin_family = AF_INET;
    gw->sin_family = AF_INET;
    mask->sin_family = AF_INET;

    /* Set up the data for removing the default route */
    dst->sin_addr.s_addr = 0;
    gw->sin_addr.s_addr = 0;
    mask->sin_addr.s_addr = 0;
    route.rt_flags = RTF_UP | RTF_GATEWAY;
    
    /* Remove the default route */
    ioctl(sockfd,SIOCDELRT,&route);

    /* Set up the data for adding the default route */
    dst->sin_addr.s_addr = 0;
    gw->sin_addr.s_addr = config->u.c.peer_ip.s_addr;
    mask->sin_addr.s_addr = 0;
    route.rt_flags = RTF_UP | RTF_GATEWAY;
    
    /* Remove this route if it already exists */
    ioctl(sockfd,SIOCDELRT,&route);

    /* Add the default route */
    if( ioctl(sockfd,SIOCADDRT,&route) == -1 ) {
        lprintf( log, WARN, "Adding default route: %s", 
                strerror(errno));
        return -1;
    }

    lprintf( log, INFO, "Added default route successfully." );
    return 0;
}

int restore_default_gw(void) {
    int sd;
    struct rtentry route;

    if( (sd=socket(PF_INET, SOCK_DGRAM, 0)) == -1 ) {
        lprintf( log, FATAL, "Getting socket: %s\n", strerror(errno) );
        return -1;
    }

    route.rt_flags = RTF_UP | RTF_GATEWAY;
    
    getprivs("restoring default gw");
    /* Remove the default route */
    ioctl(sd,SIOCDELRT,&route);
    
    if( ioctl(sd,SIOCADDRT,&default_gw) == -1 ) {
        lprintf( log, WARN, "Restoring previous default gw: %s",
                strerror(errno) );
    }
    dropprivs("done restoring default gw");

    return 0;
}

/* 
 * Sets the IP address of the tun interface given in ifr to the value in the
 * passed in struct in_addr. Returns 0 on success, -1 on failure (e.g. address
 * already in use).
 */
static inline int tun_setaddr( int sd, struct ifreq *ifr, struct in_addr ip, 
                                            clidata_list_t *clients ) {
    struct sockaddr_in *addr;

    /* set the local IP of the device */
    addr = (struct sockaddr_in *)(&(ifr->ifr_addr));
    addr->sin_family = AF_INET;
    addr->sin_port = 0;
    addr->sin_addr.s_addr = ip.s_addr;
    if( clients && ip_used(clients, ip) ) return -1;
    if( ioctl(sd, SIOCSIFADDR, ifr) == -1 ) {
        lprintf(log, ERROR, "Assigning %s IP addr %s: %s",
                ifr->ifr_name, inet_ntoa(ip), strerror(errno));
        return -1;
    } else {
        lprintf(log, INFO, "Configured %s with ip %s.",
                ifr->ifr_name, inet_ntoa(ip));
        return 0;
    }
}

/* 
 * Sets the peer IP addr of the tun interface given in ifr to the value in the
 * passed in struct in_addr. Returns 0 on success, -1 on failure (e.g. address
 * already in use).
 */
static inline int tun_setpeeraddr(int sd, struct ifreq *ifr, struct in_addr ip,
                                            clidata_list_t *clients ) {
    struct sockaddr_in *addr;

    /* Set the peer IP of the address */
    addr = (struct sockaddr_in *)(&(ifr->ifr_dstaddr));
    addr->sin_family = AF_INET;
    addr->sin_port = 0;
    addr->sin_addr.s_addr = ip.s_addr;
    if( clients && ip_used(clients, ip) ) return -1;
    if( ioctl(sd, SIOCSIFDSTADDR, ifr) == -1 ) {
        lprintf(log, ERROR, "Assigning %s peer IP addr %s: %s",
                ifr->ifr_name, inet_ntoa(ip), strerror(errno));
        return -1;
    } else {
        lprintf(log, INFO, "Configured %s with peer ip %s.",
                ifr->ifr_name, inet_ntoa(ip));
        return 0;
    }
}


/*
 * fetches the mac addr
 * returns a pointer to a static buffer of the mac addr
 */
char *get_mac( char *ifname )
{
    int sd;
    struct ifreq ifr;
    static char mac[14];
    unsigned char *macbits;

    strncpy(ifr.ifr_name, ifname, 16);

    /* the mac is static so if has been filled in just return it */
    if( *mac ) return mac;

    if( (sd=socket(PF_INET, SOCK_DGRAM, 0)) == -1 ) {
        lprintf( log, FATAL, "Getting socket: %s\n", strerror(errno) );
        return NULL;
    }

    if( ioctl(sd, SIOCGIFHWADDR, &ifr) == -1 ) {
        lprintf(log, FATAL, "Getting if_name hwaddr: %s", strerror(errno));
        return NULL;
    }

    macbits = (unsigned char *)(&ifr.ifr_hwaddr.sa_data);

    sprintf(mac, "%02X%02X%02X%02X%02X%02X",
            macbits[0], macbits[1], macbits[2],
            macbits[3], macbits[4], macbits[5] );

    return mac;
}

/*
 * Brings up the interface given by ifr. sd is a socket descriptor.
 */
static inline int tun_up(int sd, struct ifreq *ifr) {
    /* Get the current flags */
    if( ioctl(sd, SIOCGIFFLAGS, ifr) == -1 ) {
        lprintf(log, FATAL, "Getting tun0 flags: %s", strerror(errno));
        return -1;
    }

    /* Turn on the UP flag */
    ifr->ifr_flags |= IFF_UP;
    if( ioctl(sd, SIOCSIFFLAGS, ifr) == -1 ) {
        lprintf(log, FATAL, "Setting tun0 flags: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/*
 * Sets tun specific flags. tunfd is a filedes to the tun device file.
 */
static inline int tun_setflags(int tunfd) {
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags &= ~TUN_NOCHECKSUM;
    /* Turn on checksumming */
    if( ioctl(tunfd, TUNSETNOCSUM, 0) == -1 ) {
        lprintf(log, FATAL, "Setting tun0 NOCSUM flags: %s", strerror(errno));
        return -1;
    }
#if 0
    /* turn on debugging */
    if( ioctl(tunfd, TUNSETDEBUG, 1) == -1 ) {
        dprintf(log, FATAL, "Setting tun0 DEBUG flags: %s", strerror(errno));
        return -1;
    }
#endif
    return 0;
}

/*
 * Pass in an uninitialized struct ifreq and tun_open() will open the tun
 * device file and fill in ifr for you. Returns the file descriptor to the tun
 * device, or -1 on error.
 */
static inline int tun_open(struct ifreq *ifr) {
    int tunfd;

    if ((tunfd = open(config->tunfile, O_RDWR)) < 0) {
        lprintf(log, FATAL, "Unable to open %s: %s.", 
                config->tunfile, strerror(errno) );
        return -1;
    }

    memset(ifr, 0, sizeof(struct ifreq));
    ifr->ifr_flags = IFF_TUN;
    strcpy(ifr->ifr_name, "tun%d");
    if( ioctl(tunfd, TUNSETIFF, ifr) == -1 ) {
        close(tunfd);
        lprintf( log, FATAL, "Setting up tun interface: %s\n", 
                strerror(errno));
        return -1;
    }

    lprintf(log, INFO, "Allocated tun device %s\n", ifr->ifr_name);
    return tunfd;
}


/* 
 * Allocates a tun device based on the input set of acceptable ip ranges.
 * Simply attempts to bring up the interface with successive IPs until it
 * succeeds. Then attempts to assign peer addresses with successive IPs until
 * that succeeds.  On success, returns 0 and sets the following variables:
 *      clidata->tunfd
 *      clidata->cliaddr
 *      clidata->srvaddr
 * On failure, returns -1
 */
int srv_tun_alloc(clidata_t *clidata, clidata_list_t *clients) {
    struct ifreq ifr;
    struct in_addr ip;
    int sd=0;
    int step=0;
    iprange_t *crange, *srange;
    char ip1[16];
    char ip2[16];

    getprivs("Bringing up tun interface");
    if( (clidata->tunfd=tun_open(&ifr)) == -1 ) goto alloc_error_1;

    if( (sd=socket(PF_INET, SOCK_DGRAM, 0)) == -1 ) {
        lprintf( log, FATAL, "Getting socket: %s\n", strerror(errno) );
        return -1;
    }

    /* 
     * Big ugly for loop to bring up the interface based on the client's and
     * server's IP ranges
     */
    for( crange = clidata->iprange; crange; crange = crange->next ) {
        for( srange = config->u.s.ipr; srange; srange = srange->next ) {
            unsigned long cmask = (0xFFFFFFFF << (32-crange->maskbits));
            unsigned long smask = (0xFFFFFFFF << (32-srange->maskbits));
            /* The overall mask is the smaller mask (with fewer 1s) */
            unsigned long mask = smask & cmask;

            strcpy(ip1, inet_ntoa(crange->net));
            strcpy(ip2, inet_ntoa(srange->net));
            dprintf(log, DEBUG, "Attempting c:%s/%lu s:%s/%lu",
                    ip1, crange->maskbits,
                    ip2, srange->maskbits);
            /* If the nets masked at the smaller mask don't match up, we know
             * that neither of these ipranges is a subset of the other.  */
            if( (ntohl(crange->net.s_addr) & mask) 
                    != (ntohl(srange->net.s_addr) & mask) ) {
                dprintf(log, DEBUG, "That range did not work.");
                continue;
            }

             /* Start from the first IP address of the smaller range */
            ip.s_addr = (cmask>smask)?crange->net.s_addr:srange->net.s_addr;

            dprintf(log, DEBUG, "That range worked. mask=0x%lX, startip=%s",
                    mask, inet_ntoa(ip));

            /* set the local IP of the device */
            while( ip_ok(crange, &ip) && ip_ok(srange, &ip) ) {
                if( tun_setaddr(sd, &ifr, ip, clients) == -1 ) {
                    dprintf(log, DEBUG, "Couldn't assign %s %s.",
                            ifr.ifr_name, inet_ntoa(ip));
                } else {
                    dprintf(log, DEBUG, 
                            "Successfully set tun localip to %s",
                            inet_ntoa(ip));
                    clidata->srvaddr.s_addr = ip.s_addr;
                    step++;
                }
                ip.s_addr = htonl(1 + ntohl(ip.s_addr));
                if( step == 1 ) break;
            }
            /* If we've exhausted our current range, continue to the next */
            if( step == 0 ) continue;
                

            /* Set the peer IP of the address */
            while( ip_ok(crange, &ip) && ip_ok(srange, &ip) ) {
                if( tun_setpeeraddr(sd, &ifr, ip, clients) == -1 ) {
                    dprintf(log, DEBUG, "Couldn't assign %s peer %s.",
                            ifr.ifr_name, inet_ntoa(ip));
                } else {
                    dprintf(log, DEBUG, 
                            "Successfully set tun peerip to %s",
                            inet_ntoa(ip));
                    clidata->cliaddr.s_addr = ip.s_addr;
                    step++;
                }
                ip.s_addr = htonl(1 + ntohl(ip.s_addr));
                if( step == 2 ) goto outta_here;
            } /* while */
        } /* for */
    } /* for */
outta_here:

    if( step < 2 ) goto alloc_error;

    /* set no checksumming, etc */
    tun_setflags(clidata->tunfd);

    /* Bring up the interface */
    if( tun_up(sd, &ifr) == -1 ) goto alloc_error;

    dropprivs("tun interface up");
    return 0;

alloc_error:
    close(clidata->tunfd);
alloc_error_1:
    clidata->tunfd=-1;
    close(sd);
    dropprivs("Failed to configure tun interface");
    return -1;
}

/*
 * sets up the tun dev according to the specidied
 * local and peer ip
 * returns the tunfd
 */
int cli_tun_alloc( struct in_addr local, struct in_addr peer )
{
    struct ifreq ifr;
    int fd, sock;

    dprintf(log, DEBUG, "stating setup");

    if( (sock=socket(PF_INET, SOCK_DGRAM, 0)) == -1 ) {
        lprintf( log, FATAL, "Getting socket: %s\n", strerror(errno) );
        return -1;
    }

    if( (fd = tun_open(&ifr)) < 0 ) {
        lprintf( log, FATAL, "setting up tun dev\n");
        return -1;
    }

    if( tun_setaddr(sock, &ifr, local, NULL) == -1 ) {
        lprintf( log, FATAL, "setting tun local addr tun dev\n");
        return -1;
    }

    if( tun_setpeeraddr(sock, &ifr, peer, NULL) == -1 ) {
        lprintf( log, FATAL, "setting tun peer addr tun dev\n");
        return -1;
    }
    
    if( tun_setflags(fd) == -1 ) {
        lprintf( log, FATAL, "setting tun flags\n");
        return -1;
    }

    if( tun_up(sock, &ifr) == -1 ) {
        lprintf( log, FATAL, "bringing up tun dev\n");
        return -1;
    }

    dprintf(log, DEBUG, "done");
    
    return fd;
}
