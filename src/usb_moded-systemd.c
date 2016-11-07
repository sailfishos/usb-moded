/**
  @file usb_moded-systemd.c

  Copyright (C) 2013-2016 Jolla oy. All rights reserved.

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

#define SYSTEMD_DBUS_SERVICE   "org.freedesktop.systemd1"
#define SYSTEMD_DBUS_PATH      "/org/freedesktop/systemd1"
#define SYSTEMD_DBUS_INTERFACE "org.freedesktop.systemd1.Manager"


/* SystemBus connection ref used for systemd control ipc */
static DBusConnection *systemd_con = NULL;

// QDBusObjectPath org.freedesktop.systemd1.Manager.StartUnit(QString name, QString mode)
// QDBusObjectPath org.freedesktop.systemd1.Manager.StopUnit(QString name, QString mode)


//  mode = replace
//  method = StartUnit or StopUnit
gboolean systemd_control_service(const char *name, const char *method)
{
    DBusMessage    *req = NULL;
    DBusMessage    *rsp = NULL;
    DBusError       err = DBUS_ERROR_INIT;
    const char     *arg = "replace";
    const char     *res = 0;


    log_debug("%s(%s) ...", method, name);

    if( !systemd_con ) {
        log_err("not connected to system bus; skip systemd unit control");
        goto EXIT;
    }

    req = dbus_message_new_method_call(SYSTEMD_DBUS_SERVICE,
                                       SYSTEMD_DBUS_PATH,
                                       SYSTEMD_DBUS_INTERFACE,
                                       method);
    if( !req ) {
        log_err("failed to construct %s.%s request",
                SYSTEMD_DBUS_INTERFACE,
                method);
        goto EXIT;
    }

    if( !dbus_message_append_args(req,
                                  DBUS_TYPE_STRING, &name,
                                  DBUS_TYPE_STRING, &arg,
                                  DBUS_TYPE_INVALID))
    {
        log_debug("error appending arguments");
        goto EXIT;
    }


    rsp = dbus_connection_send_with_reply_and_block(systemd_con, req, -1, &err);
    if( !rsp ) {
        log_err("no reply to %s.%s request: %s: %s",
                SYSTEMD_DBUS_INTERFACE,
                method,
                err.name, err.message);
        goto EXIT;
    }

    if( dbus_set_error_from_message(&err, rsp) ) {
        log_err("got error reply to %s.%s request: %s: %s",
                SYSTEMD_DBUS_INTERFACE,
                method,
                err.name, err.message);
        goto EXIT;
    }

    if( !dbus_message_get_args(rsp, &err,
                               DBUS_TYPE_OBJECT_PATH, &res,
                               DBUS_TYPE_INVALID) ) {
        log_err("failed to parse reply to %s.%s request: %s: %s",
                SYSTEMD_DBUS_INTERFACE,
                method,
                err.name, err.message);
        goto EXIT;
    }

EXIT:

    dbus_error_free(&err);

    if( rsp ) dbus_message_unref(rsp);
    if( req ) dbus_message_unref(req);

    log_debug("%s(%s) -> %s", method, name, res ?: "N/A");

    return res != 0;
}

/* ========================================================================= *
 * start/stop systemd control availability
 * ========================================================================= */

gboolean
systemd_control_start(void)
{
    gboolean ack = FALSE;

    log_debug("starting systemd control");

    /* Get connection ref */
    if( (systemd_con = usb_moded_dbus_get_connection()) == 0 )
    {
        log_err("Could not connect to dbus for systemd control\n");
        goto cleanup;
    }
    ack = TRUE;

cleanup:

    return ack;
}

void
systemd_control_stop(void)
{
    log_debug("stopping systemd control");

    if(systemd_con)
    {
        /* Let go of connection ref */
        dbus_connection_unref(systemd_con),
            systemd_con = 0;
    }
}
