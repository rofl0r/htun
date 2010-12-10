/* -------------------------------------------------------------------------
 * srvproto1.h - htun protocol 1 functions for the server side
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
/* $Id: srvproto1.h,v 2.3 2002/04/15 01:07:06 jehsom Exp $ */

#ifndef __SRVPROTO1_H
#define __SRVPROTO1_H

#include "clidata.h"

int handle_f_p1( clidata_t **clientp );

int handle_s_p1( clidata_t *client, char *hdrs );

int handle_p_p1( clidata_t *client, char *hdrs );

#endif
