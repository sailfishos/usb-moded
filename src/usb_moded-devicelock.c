/**

  @file: usb_moded-devicelock.c

  Copyright (C) 2010 Nokia Corporation. All rights reserved.
  Copyright (C) 2013-2016 Jolla Ltd.

  @author: Philippe De Swert <philippe.de-swert@nokia.com>
  @author: Philippe De Swert <philippe.deswert@jollamobile.com>
  @author: Jonni Rainisto <jonni.rainisto@jollamobile.com>
  @author: Vesa Halttunen <vesa.halttunen@jollamobile.com>
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

/*
 * Interacts with the devicelock to know if we can expose the system contents or not
 */

/*============================================================================= */

#include <stdlib.h>
#include <string.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "usb_moded-devicelock.h"
#include "usb_moded-lock.h"
#include "usb_moded-log.h"
#include "usb_moded.h"
#include "usb_moded-modes.h"
#include "usb_moded-dbus-private.h"

/* ========================================================================= *
 * devicelock state type
 * ========================================================================= */

/** Devicelock states  */
typedef enum
{
    /** Devicelock is not active */
    DEVICE_LOCK_UNLOCKED  = 0,

    /** Devicelock is active */
    DEVICE_LOCK_LOCKED    = 1,

    /** Initial startup value; from usb-moded p.o.v. equals locked */
    DEVICE_LOCK_UNDEFINED = 2,
}  devicelock_state_t;

/** Return human readable representation of devicelock state enum
 */
static const char *
devicelock_state_repr(devicelock_state_t state)
{
    const char *repr = "DEVICE_LOCK_<INVALID>";

    switch( state )
    {
    case DEVICE_LOCK_UNLOCKED:  repr = "DEVICE_LOCK_UNLOCKED";  break;
    case DEVICE_LOCK_LOCKED:    repr = "DEVICE_LOCK_LOCKED";    break;
    case DEVICE_LOCK_UNDEFINED: repr = "DEVICE_LOCK_UNDEFINED"; break;
    default: break;
    }

    return repr;
}

/* ========================================================================= *
 * module state data
 * ========================================================================= */

/* SystemBus connection ref used for devicelock ipc */
static DBusConnection *devicelock_con = NULL;

/* Cached devicelock state */
static devicelock_state_t device_lock_state = DEVICE_LOCK_UNDEFINED;

/* Flag for: devicelock is available on system bus */
static gboolean devicelock_is_available = FALSE;

/* ========================================================================= *
 * functionality provided to the other modules
 * ========================================================================= */

/** Checks if the device is locked.
 *
 * @return TRUE for unlocked, FALSE for locked
 *
 */
gboolean usb_moded_get_export_permission(void)
{
    gboolean unlocked = (device_lock_state == DEVICE_LOCK_UNLOCKED);

    return unlocked;
}

/* ========================================================================= *
 * devicelock state queries
 * ========================================================================= */

static void devicelock_state_changed(devicelock_state_t state)
{
    if( device_lock_state == state )
        goto EXIT;

    log_debug("devicelock state: %s -> %s",
              devicelock_state_repr(device_lock_state),
              devicelock_state_repr(state));
    device_lock_state = state;

    rethink_usb_charging_fallback();

EXIT:
    return;
}

static DBusPendingCall *devicelock_state_query_pc = 0;

static void devicelock_state_cancel(void)
{
    if( devicelock_state_query_pc ) {
        dbus_pending_call_cancel(devicelock_state_query_pc);
        dbus_pending_call_unref(devicelock_state_query_pc),
            devicelock_state_query_pc = 0;
    }
}

static void devicelock_state_query_cb(DBusPendingCall *pending, void *aptr)
{
    DBusMessage *rsp = 0;
    dbus_int32_t dta = DEVICE_LOCK_UNDEFINED;
    DBusError    err = DBUS_ERROR_INIT;

    (void)aptr;

    if( !(rsp = dbus_pending_call_steal_reply(pending)) )
    {
        log_err("%s.%s: no reply",
                DEVICELOCK_INTERFACE, DEVICELOCK_GET_STATE_REQ);
        goto EXIT;
    }

    if( dbus_set_error_from_message(&err, rsp) )
    {
        log_err("%s.%s: error reply: %s: %s",
                DEVICELOCK_INTERFACE, DEVICELOCK_GET_STATE_REQ,
                err.name, err.message);
        goto EXIT;
    }

    if( !dbus_message_get_args(rsp, &err, DBUS_TYPE_INT32, &dta,
                               DBUS_TYPE_INVALID) )
    {
        log_err("%s.%s: parse error: %s: %s",
                DEVICELOCK_INTERFACE, DEVICELOCK_GET_STATE_REQ,
                err.name, err.message);
        goto EXIT;
    }

EXIT:
    devicelock_state_changed(dta);

    if( rsp ) dbus_message_unref(rsp);

    dbus_error_free(&err);

    dbus_pending_call_unref(devicelock_state_query_pc),
        devicelock_state_query_pc = 0;
}

static void devicelock_state_query(void)
{
    DBusMessage     *req = NULL;
    DBusPendingCall *pc  = 0;

    devicelock_state_cancel();

    log_debug("querying device lock state");

    if( !devicelock_con ) {
        log_err("not connected to system bus; skip device state query");
        goto EXIT;
    }

    req = dbus_message_new_method_call(DEVICELOCK_SERVICE,
                                       DEVICELOCK_OBJECT,
                                       DEVICELOCK_INTERFACE,
                                       DEVICELOCK_GET_STATE_REQ);

    if( !req )
    {
        log_err("%s.%s: failed to construct request",
                DEVICELOCK_INTERFACE, DEVICELOCK_GET_STATE_REQ);
        goto EXIT;
    }

    if( !dbus_connection_send_with_reply(devicelock_con, req, &pc, -1) )
        goto EXIT;

    if( !pc )
        goto EXIT;

    if( !dbus_pending_call_set_notify(pc, devicelock_state_query_cb, 0, 0) )
        goto EXIT;

    devicelock_state_query_pc = pc, pc = 0;

EXIT:

    if( pc  ) dbus_pending_call_unref(pc);
    if( req ) dbus_message_unref(req);
}

static void devicelock_state_signal(DBusMessage *msg)
{
    DBusError    err = DBUS_ERROR_INIT;
    dbus_int32_t dta = DEVICE_LOCK_LOCKED;

    if( !dbus_message_get_args(msg, &err,
                               DBUS_TYPE_INT32, &dta,
                               DBUS_TYPE_INVALID) )
    {
        log_err("failed to parse  %s.%s signal: %s: %s",
                DEVICELOCK_INTERFACE, DEVICELOCK_STATE_CHANGED_SIG,
                err.name, err.message);
    }

    devicelock_state_changed(dta);

    dbus_error_free(&err);
}

/* ========================================================================= *
 * devicelock name owner tracking
 * ========================================================================= */

static void devicelock_available_changed(const char *owner)
{
    gboolean is_available = (owner && *owner);

    if( devicelock_is_available != is_available ) {
        devicelock_is_available = is_available;
        log_debug("devicelock is %s",
                  devicelock_is_available ? "running" : "stopped");

        /* Forget cached device state */
        devicelock_state_changed(DEVICE_LOCK_UNDEFINED);

        /* Query current state on devicelock startup */
        if( devicelock_is_available ) {
            devicelock_state_query();
        }
    }
}

static DBusPendingCall *devicelock_available_pc = 0;

static void devicelock_available_cb(const char *owner)
{
    devicelock_available_changed(owner);

    dbus_pending_call_unref(devicelock_available_pc),
        devicelock_available_pc = 0;
}

static void devicelock_available_cancel(void)
{
    if( devicelock_available_pc )
    {
            dbus_pending_call_cancel(devicelock_available_pc);
        dbus_pending_call_unref(devicelock_available_pc),
            devicelock_available_pc = 0;
    }
}

static void devicelock_available_query(void)
{
    devicelock_available_cancel();

    log_debug("querying %s name owner", DEVICELOCK_SERVICE);

    usb_moded_get_name_owner_async(DEVICELOCK_SERVICE,
                                   devicelock_available_cb,
                                   &devicelock_available_pc);
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
    else if( !strcmp(name, DEVICELOCK_SERVICE) )
    {
        devicelock_available_changed(curr);
    }
    dbus_error_free(&err);
}

/* ========================================================================= *
 * dbus message filter
 * ========================================================================= */

static DBusHandlerResult
devicelock_dbus_filter_cb(DBusConnection *con, DBusMessage *msg, void *aptr)
{
    (void)con;
    (void)aptr;

    if( dbus_message_is_signal(msg,
                               DEVICELOCK_INTERFACE,
                               DEVICELOCK_STATE_CHANGED_SIG) )
    {
        devicelock_state_signal(msg);
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
 * start/stop devicelock state tracking
 * ========================================================================= */

gboolean
start_devicelock_listener(void)
{
    gboolean ack = FALSE;

    log_debug("starting devicelock listener");

    /* Get connection ref */
    if( (devicelock_con = usb_moded_dbus_get_connection()) == 0 )
    {
        log_err("Could not connect to dbus for devicelock\n");
        goto cleanup;
    }

    /* Add filter callback */
    if( !dbus_connection_add_filter(devicelock_con,
                                    devicelock_dbus_filter_cb , 0, 0) )
    {
        log_err("adding system dbus filter for devicelock failed");
        goto cleanup;
    }

    /* Add match without blocking / error checking */
    dbus_bus_add_match(devicelock_con, DEVICELOCK_STATE_CHANGED_MATCH,0);
    dbus_bus_add_match(devicelock_con, DEVICELOCK_NAME_OWNER_CHANGED_MATCH,0);

    /* Initiate async devicelock name owner query */
    devicelock_available_query();

    ack = TRUE;

cleanup:

    return ack;
}

void
stop_devicelock_listener(void)
{
    log_debug("stopping devicelock listener");

    /* Do note leave pending queries behind */
    devicelock_state_cancel();
    devicelock_available_cancel();

    if(devicelock_con)
    {
        /* Remove filter callback */
        dbus_connection_remove_filter(devicelock_con,
                                      devicelock_dbus_filter_cb, 0);

        if( dbus_connection_get_is_connected(devicelock_con) ) {
            /* Remove match without blocking / error checking */
            dbus_bus_remove_match(devicelock_con,
                                  DEVICELOCK_STATE_CHANGED_MATCH, 0);
            dbus_bus_remove_match(devicelock_con,
                                  DEVICELOCK_NAME_OWNER_CHANGED_MATCH, 0);
        }

        /* Let go of connection ref */
        dbus_connection_unref(devicelock_con),
            devicelock_con = 0;
    }
}
