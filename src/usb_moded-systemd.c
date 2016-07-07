/**
  @file	usb_moded-systemd.c

  Copyright (C) 2013 Jolla oy. All rights reserved.

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

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "usb_moded-dbus.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded.h"
#include "usb_moded-log.h"
#include "usb_moded-systemd.h"

static DBusConnection * get_systemd_dbus_connection(void)
{
  DBusError       error;
  DBusConnection *conn = 0;

  dbus_error_init(&error);

  conn = dbus_connection_open_private("unix:path=/run/systemd/private", &error);
  if (!conn) 
  {
     if (dbus_error_is_set(&error))
            log_err("Cannot connect to systemd: %s", error.message);
     else
            log_err("Cannot connect to systemd");
     dbus_error_free(&error);
     return 0;
  }

  return conn;
}

//  mode = replace
//  method = StartUnit or StopUnit
int systemd_control_service(const char *name, const char *method) 
{

  DBusConnection *bus;
  DBusError error;
  DBusMessage *msg = NULL, *reply = NULL;
  int ret = 1;
  const char * replace = "replace";

  dbus_error_init(&error);

  log_debug("Handling %s, with systemd, method %s\n", name, method);

  bus = get_systemd_dbus_connection();
  if(!bus)
	return(ret);

  msg = dbus_message_new_method_call("org.freedesktop.systemd1", 
        "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", method);
  if(msg)
  {
	if(!dbus_message_append_args (msg, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &replace, DBUS_TYPE_INVALID))
	{
		log_debug("error appending arguments\n");
        	dbus_message_unref(msg);	
		goto quit;
	}
	reply = dbus_connection_send_with_reply_and_block(bus, msg, -1, &error);
	if(reply)
 	{
		dbus_message_unref(reply);
		ret = 0;
	}
        dbus_message_unref(msg);	
  }

quit:
  dbus_connection_close(bus);
  dbus_connection_unref(bus);
  dbus_error_free(&error);

  return(ret); 
}
