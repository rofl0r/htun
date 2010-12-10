/* -------------------------------------------------------------------------
 * server.h - htun server defs
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
/* $Id: server.h,v 2.13 2002/08/07 17:24:45 jehsom Exp $ */

#ifndef __SERVER_H
#define __SERVER_H

#include "tpool.h"
#include "clidata.h"

/* Initializes the server. Returns the value that will go to the OS */
int server_main( void );

/* 
 * Other files call this function to start the tunfile reader thread
 */
int srv_start_tunfile_reader( clidata_t *client );

/* 
 * Other files call this function to start the tunfile writer thread
 */
int srv_start_tunfile_writer( clidata_t *client );

extern clidata_list_t *clients;

#endif

