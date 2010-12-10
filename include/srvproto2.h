/* -------------------------------------------------------------------------
 * srvproto2.h - htun protocol 2 functions for the server side
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
/* $Id: srvproto2.h,v 1.8 2002/04/13 22:04:43 jehsom Exp $ */

#ifndef __SRVPROTO2_H
#define __SRVPROTO2_H

#include "clidata.h"

#define CP2_OK_MAXBODY 50

clidata_t *handle_cp( int clisock, char *hdrs, int protover );

clidata_t *handle_cr( int clisock, char *hdrs );

int handle_f_p2( clidata_t **client );

int handle_s_p2( clidata_t *client, char *hdrs );

int handle_r_p2( clidata_t *client, char *hdrs );

#endif
