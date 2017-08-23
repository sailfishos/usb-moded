/*
  Copyright (C) 2010 Nokia Corporation. All rights reserved.
  Copyright (C) 2012-2016 Jolla. All rights reserved.

  @author: Philippe De Swert <philippe.de-swert@nokia.com>
  @author: Philippe De Swert <philippe.deswert@jollamobile.com>
  @author: Simo Piiroinen <simo.piiroinen@jollamobile.com>

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

typedef enum mode_list_type_t {
    SUPPORTED_MODES_LIST,
    AVAILABLE_MODES_LIST
} mode_list_type_t;

void set_usb_connected(gboolean connected);
void set_usb_connected_state(void);
void set_usb_mode(const char *mode);
void rethink_usb_charging_fallback(void);
const char * get_usb_mode(void);
void set_usb_module(const char *module);
const char * get_usb_module(void);
void set_usb_mode_data(struct mode_list_elem *data);
struct mode_list_elem * get_usb_mode_data(void);
gboolean get_usb_connection_state(void);
void set_usb_connection_state(gboolean state);
void set_charger_connected(gboolean state);
gchar *get_mode_list(mode_list_type_t type);
gchar *get_available_mode_list(void);
int valid_mode(const char *mode);

/** Name of the wakelock usb_moded uses for temporary suspend delay */
#define USB_MODED_WAKELOCK_STATE_CHANGE        "usb_moded_state"

/** Name of the wakelock usb_moded uses for input processing */
#define USB_MODED_WAKELOCK_PROCESS_INPUT       "usb_moded_input"

/** How long usb_moded will delay suspend by default [ms] */
#define USB_MODED_SUSPEND_DELAY_DEFAULT_MS      5000

/** How long usb_moded is allowed to block suspend [ms] */
#define USB_MODED_SUSPEND_DELAY_MAXIMUM_MS \
	(USB_MODED_SUSPEND_DELAY_DEFAULT_MS * 2)

void acquire_wakelock(const char *wakelock_name);
void release_wakelock(const char *wakelock_name);

void allow_suspend(void);
void delay_suspend(void);

extern int cable_connection_delay;

void usb_moded_stop(int exitcode);

int usb_moded_system_(const char *file, int line, const char *func, const char *command);
#define usb_moded_system(command)  usb_moded_system_(__FILE__,__LINE__,__FUNCTION__,(command))

FILE *usb_moded_popen_(const char *file, int line, const char *func, const char *command, const char *type);
#define usb_moded_popen(command, type) usb_moded_popen_(__FILE__,__LINE__,__FUNCTION__,(command),(type))

void usb_moded_usleep_(const char *file, int line, const char *func, useconds_t usec);
#define usb_moded_usleep(usec)    usb_moded_usleep_(__FILE__,__LINE__,__FUNCTION__,(usec))
#define usb_moded_msleep(msec)    usb_moded_usleep_(__FILE__,__LINE__,__FUNCTION__,(msec)*1000)
#define usb_moded_sleep(sec)      usb_moded_usleep_(__FILE__,__LINE__,__FUNCTION__,(sec)*1000000)

#endif /* USB_MODED_H */
