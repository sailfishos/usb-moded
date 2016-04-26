/*
  Copyright (C) 2010 Nokia Corporation. All rights reserved.
  Copyright (C) 2012 Jolla. All rights reserved.

  @author: Philippe De Swert <philippe.de-swert@nokia.com>
  @author: Philippe De Swert <philippe.deswert@jollamobile.com>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the Lesser GNU General Public License 
  version 2 as published by the Free Software Foundation. 

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.
 
  You should have received a copy of the Lesser GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
  02110-1301 USA
*/

#ifndef USB_MODED_H_
#define USB_MODED_H_

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <config.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/wait.h>

#include <glib-2.0/glib.h>
#include <glib-object.h>

#include "usb_moded-dyn-config.h"

#define USB_MODED_LOCKFILE	"/var/run/usb_moded.pid"
#define MAX_READ_BUF 512

/** 
 * a struct containing all the usb_moded info needed 
 */
typedef struct usb_mode  
{
  /*@{*/
  gboolean connected; 		/* connection status, 1 for connected */
  gboolean mounted;  		/* mount status, 1 for mounted -UNUSED atm- */
  gboolean android_usb_broken;  /* Used to keep an active gadget for broken Android kernels */
  char *mode;  			/* the mode name */
  char *module; 		/* the module name for the specific mode */
  struct mode_list_elem *data;  /* contains the mode data */
  /*@}*/
}usb_mode;

void set_usb_connected(gboolean connected);
void set_usb_connected_state(void);
void set_usb_mode(const char *mode);
const char * get_usb_mode(void);
void set_usb_module(const char *module);
const char * get_usb_module(void);
void set_usb_mode_data(struct mode_list_elem *data);
struct mode_list_elem * get_usb_mode_data(void);
gboolean get_usb_connection_state(void);
void set_usb_connection_state(gboolean state);
void set_charger_connected(gboolean state);
gchar *get_mode_list(void);
int valid_mode(const char *mode);


extern int cable_connection_delay;

#endif /* USB_MODED_H */
