/**
  @file usb_moded-dsme.c

  Copyright (C) 2013-2016 Jolla. All rights reserved.

  @author: Philippe De Swert <philippe.deswert@jollamobile.com>
  @author: Jonni Rainisto <jonni.rainisto@jollamobile.com>
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

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "usb_moded-dsme.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded-log.h"

/* ========================================================================= *
 * dsme dbus constants
 * ========================================================================= */

#define DSME_DBUS_SERVICE               "com.nokia.dsme"

#define DSME_DBUS_REQUEST_PATH          "/com/nokia/dsme/request"
#define DSME_DBUS_REQUEST_IFACE         "com.nokia.dsme.request"
#define DSME_DBUS_GET_STATE_REQ         "get_state"

#define DSME_DBUS_SIGNAL_PATH           "/com/nokia/dsme/signal"
#define DSME_DBUS_SIGNAL_IFACE          "com.nokia.dsme.signal"
#define DSME_STATE_CHANGE_SIG           "state_change_ind"

#define DSME_STATE_CHANGE_MATCH\
     "type='signal'"\
     ",interface='"DSME_DBUS_SIGNAL_IFACE"'"\
     ",member='"DSME_STATE_CHANGE_SIG"'"

#define DSME_OWNER_CHANGE_MATCH\
     "type='signal'"\
     ",interface='"DBUS_INTERFACE_DBUS"'"\
     ",member='"DBUS_NAME_OWNER_CHANGED_SIG"'"\
     ",arg0='"DSME_DBUS_SERVICE"'"

/* ========================================================================= *
 * state data
 * ========================================================================= */

/* SystemBus connection ref used for dsme ipc */
static DBusConnection *dsme_con = NULL;

/* Flag for: device state == "USER" */
static gboolean in_user_state = FALSE;

/* Flag for: dsme is available on system bus */
static gboolean dsme_is_available = FALSE;

/** Checks if the device is is USER-state.
 *
 * @return 1 if it is in USER-state, 0 for not
 *
 */
gboolean is_in_user_state(void)
{
    return in_user_state;
}

/* ========================================================================= *
 * device state queries
 * ========================================================================= */

static void device_state_changed(const char *state)
{
    gboolean to_user_state = state && !strcmp(state, "USER");

    log_debug("device state: %s", state ?: "(null)");

    if( in_user_state != to_user_state ) {
        in_user_state = to_user_state;
        log_debug("in user state: %s",
                  in_user_state ? "true" : "false");
    }
}

static DBusPendingCall *device_state_query_pc = 0;

static void device_state_cancel(void)
{
    if( device_state_query_pc ) {
        dbus_pending_call_cancel(device_state_query_pc);
        dbus_pending_call_unref(device_state_query_pc),
            device_state_query_pc = 0;
    }
}

static void device_state_query_cb(DBusPendingCall *pending, void *aptr)
{
    DBusMessage *rsp = 0;
    const char  *dta = 0;
    DBusError    err = DBUS_ERROR_INIT;

    (void)aptr;

    if( !(rsp = dbus_pending_call_steal_reply(pending)) ) {
        log_err("did not get reply");
        goto EXIT;
    }

    if( dbus_set_error_from_message(&err, rsp) )
    {
        log_err("error reply: %s: %s", err.name, err.message);
        goto EXIT;
    }

    if( !dbus_message_get_args(rsp, &err,
                               DBUS_TYPE_STRING, &dta,
                               DBUS_TYPE_INVALID) )
    {
        if( strcmp(err.name, DBUS_ERROR_NAME_HAS_NO_OWNER) )
            log_err("parse error: %s: %s", err.name, err.message);
        goto EXIT;
    }

    device_state_changed(dta);

EXIT:

    if( rsp ) dbus_message_unref(rsp);

    dbus_error_free(&err);

    dbus_pending_call_unref(device_state_query_pc),
        device_state_query_pc = 0;
}

static void device_state_query(void)
{
    DBusMessage     *req = NULL;
    DBusPendingCall *pc  = 0;

    device_state_cancel();

    if( !dsme_con ) {
        log_err("not connected to system bus; skip device state query");
        goto EXIT;
    }

    req = dbus_message_new_method_call(DSME_DBUS_SERVICE,
                                       DSME_DBUS_REQUEST_PATH,
                                       DSME_DBUS_REQUEST_IFACE,
                                       DSME_DBUS_GET_STATE_REQ);
    if( !req ) {
        log_err("failed to construct %s.%s request",
                DSME_DBUS_REQUEST_IFACE,
                DSME_DBUS_GET_STATE_REQ);
        goto EXIT;
    }

    if( !dbus_connection_send_with_reply(dsme_con, req, &pc, -1) )
        goto EXIT;

    if( !pc )
        goto EXIT;

    if( !dbus_pending_call_set_notify(pc, device_state_query_cb, 0, 0) )
        goto EXIT;

    device_state_query_pc = pc, pc = 0;

EXIT:

    if( pc  ) dbus_pending_call_unref(pc);
    if( req ) dbus_message_unref(req);
}

static void device_state_signal(DBusMessage *msg)
{
    DBusError   err = DBUS_ERROR_INIT;
    const char *dta = 0;

    if( !dbus_message_get_args(msg, &err,
                               DBUS_TYPE_STRING, &dta,
                               DBUS_TYPE_INVALID) )
    {
        log_err("failed to parse signal: %s: %s",
                err.name, err.message);
    }
    else
    {
        device_state_changed(dta);
    }
    dbus_error_free(&err);
}

/* ========================================================================= *
 * dsme name owner tracking
 * ========================================================================= */

static void dsme_available_changed(const char *owner)
{
    gboolean is_available = (owner && *owner);

    if( dsme_is_available != is_available ) {
        dsme_is_available = is_available;
        log_debug("dsme is %s", dsme_is_available ? "running" : "stopped");

        /* Forget cached device state */
        device_state_changed("UNKNOWN");

        /* Query current state on dsme startup */
        if( dsme_is_available ) {
            device_state_query();
        }
    }
}

static DBusPendingCall *dsme_available_pc = 0;

static void dsme_available_cb(const char *owner)
{
    dsme_available_changed(owner);

    dbus_pending_call_unref(dsme_available_pc),
        dsme_available_pc = 0;
}

static void dsme_available_cancel(void)
{
    if( dsme_available_pc )
    {
        dbus_pending_call_cancel(dsme_available_pc);
        dbus_pending_call_unref(dsme_available_pc),
            dsme_available_pc = 0;
    }
}

static void dsme_available_query(void)
{
    dsme_available_cancel();

    usb_moded_get_name_owner_async(DSME_DBUS_SERVICE,
                                   dsme_available_cb,
                                   &dsme_available_pc);
}

static void name_owner_signal(DBusMessage *msg)
{
    DBusError   err  = DBUS_ERROR_INIT;
    const char *name = 0;
    const char *prev = 0;
    const char *curr = 0;

    if( !dbus_message_get_args(msg, &err,
                               DBUS_TYPE_STRING, &name,
                               DBUS_TYPE_STRING, &prev,
                               DBUS_TYPE_STRING, &curr,
                               DBUS_TYPE_INVALID) )
    {
        log_err("failed to parse signal: %s: %s",
                err.name, err.message);
    }
    else if( !strcmp(name, DSME_DBUS_SERVICE) )
    {
        dsme_available_changed(curr);
    }
    dbus_error_free(&err);
}

/* ========================================================================= *
 * dbus message filter
 * ========================================================================= */

static DBusHandlerResult
dsme_dbus_filter_cb(DBusConnection *con, DBusMessage *msg, void *user_data)
{
    (void)con;
    (void)user_data;

    if( dbus_message_is_signal(msg,
                               DSME_DBUS_SIGNAL_IFACE,
                               DSME_STATE_CHANGE_SIG) )
    {
        device_state_signal(msg);
    }
    else if( dbus_message_is_signal(msg,
                                    DBUS_INTERFACE_DBUS,
                                    DBUS_NAME_OWNER_CHANGED_SIG) )
    {
        name_owner_signal(msg);
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ========================================================================= *
 * start/stop dsme tracking
 * ========================================================================= */

gboolean
dsme_listener_start(void)
{
    gboolean ack = FALSE;

    /* Get connection ref */
    if( (dsme_con = usb_moded_dbus_get_connection()) == 0 )
    {
        log_err("Could not connect to dbus for dsme\n");
        goto cleanup;
    }

    /* Add filter callback */
    if( !dbus_connection_add_filter(dsme_con,
                                    dsme_dbus_filter_cb , 0, 0) )
    {
        log_err("adding system dbus filter for dsme failed");
        goto cleanup;
    }

    /* Add match without blocking / error checking */
    dbus_bus_add_match(dsme_con, DSME_STATE_CHANGE_MATCH, 0);
    dbus_bus_add_match(dsme_con, DSME_OWNER_CHANGE_MATCH, 0);

    /* Initiate async dsme name owner query */
    dsme_available_query();

    ack = TRUE;

cleanup:

    return ack;
}

void
dsme_listener_stop(void)
{
    /* Cancel pending dbus queries */
    dsme_available_cancel();
    device_state_cancel();

    if(dsme_con)
    {
        /* Remove filter callback */
        dbus_connection_remove_filter(dsme_con,
                                      dsme_dbus_filter_cb, 0);

        if( dbus_connection_get_is_connected(dsme_con) ) {
            /* Remove match without blocking / error checking */
            dbus_bus_remove_match(dsme_con, DSME_STATE_CHANGE_MATCH, 0);
            dbus_bus_remove_match(dsme_con, DSME_OWNER_CHANGE_MATCH, 0);
        }

        /* Let go of connection ref */
        dbus_connection_unref(dsme_con),
            dsme_con = 0;
    }
}
