/**
 * @file usb_moded-dsme.c
 *
 * Copyright (c) 2013 - 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author Jonni Rainisto <jonni.rainisto@jollamobile.com>
 * @author Simo Piiroinen <simo.piiroinen@jollamobile.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the Lesser GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the Lesser GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include "usb_moded-dsme.h"

#include "usb_moded-control.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded-log.h"
#include "usb_moded-modesetting.h"

#include <stdlib.h>
#include <unistd.h>

#include <dsme/state.h>
#include <dsme/protocol.h>
#include <dsme/processwd.h>

/* ========================================================================= *
 * DSME D-Bus Constants
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
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * DSME_STATE
 * ------------------------------------------------------------------------- */

static const char   *dsme_state_repr       (dsme_state_t state);
static dsme_state_t  dsme_state_parse      (const char *name);
static void          dsme_state_update     (dsme_state_t state);
static bool          dsme_state_is_shutdown(void);
static bool          dsme_state_is_user    (void);

/* ------------------------------------------------------------------------- *
 * DSME_SOCKET
 * ------------------------------------------------------------------------- */

static bool     dsme_socket_send_message  (gpointer msg);
static void     dsme_socket_processwd_pong(void);
static void     dsme_socket_processwd_init(void);
static void     dsme_socket_processwd_quit(void);
static void     dsme_socket_query_state   (void);
static gboolean dsme_socket_recv_cb       (GIOChannel *source, GIOCondition condition, gpointer data);
static bool     dsme_socket_is_connected  (void);
static bool     dsme_socket_connect       (void);
static void     dsme_socket_disconnect    (void);

/* ------------------------------------------------------------------------- *
 * DSME_DBUS
 * ------------------------------------------------------------------------- */

static void              dsme_dbus_device_state_update  (const char *state);
static void              dsme_dbus_device_state_query_cb(DBusPendingCall *pending, void *aptr);
static void              dsme_dbus_device_state_query   (void);
static void              dsme_dbus_device_state_cancel  (void);
static void              dsme_dbus_device_state_signal  (DBusMessage *msg);
static bool              dsme_dbus_name_owner_available (void);
static void              dsme_dbus_name_owner_update    (const char *owner);
static void              dsme_dbus_name_owner_query_cb  (const char *owner);
static void              dsme_dbus_name_owner_query     (void);
static void              dsme_dbus_name_owner_cancel    (void);
static void              dsme_dbus_name_owner_signal    (DBusMessage *msg);
static DBusHandlerResult dsme_dbus_filter_cb            (DBusConnection *con, DBusMessage *msg, void *user_data);
static bool              dsme_dbus_init                 (void);
static void              dsme_dbus_quit                 (void);

/* ------------------------------------------------------------------------- *
 * DSME
 * ------------------------------------------------------------------------- */

gboolean dsme_start_listener(void);
void     dsme_stop_listener(void);
gboolean dsme_in_user_state(void);

/* ========================================================================= *
 * DSME_STATE_TRACKING
 * ========================================================================= */

/** Lookup table for dsme state name <-> state enum conversion */
static const struct
{
    const char   *name;
    dsme_state_t  state;
} dsme_states[] =
{
#define DSME_STATE(NAME, VALUE) { #NAME, DSME_STATE_##NAME },
#include <dsme/state_states.h> // NOTRIM
#undef  DSME_STATE
};

/* Cached dsme state */
static dsme_state_t dsme_state_val = DSME_STATE_NOT_SET;

/* Flag for: dsme_state_val is USER */
static bool dsme_user_state = false;

/* Flag for: dsme_state_val is SHUTDOWN | REBOOT */
static bool dsme_shutdown_state = false;

/** Convert dsme state enum value to string
 */
static const char *
dsme_state_repr(dsme_state_t state)
{
    LOG_REGISTER_CONTEXT;

    const char *repr = "DSME_STATE_UNKNOWN";

    for( size_t i = 0; i < G_N_ELEMENTS(dsme_states); ++i ) {
        if( dsme_states[i].state == state ) {
            repr = dsme_states[i].name;
            break;
        }
    }

    return repr;
}

/** Convert dsme state name to enum value
 */
static dsme_state_t
dsme_state_parse(const char *name)
{
    LOG_REGISTER_CONTEXT;

    dsme_state_t state = DSME_STATE_NOT_SET;

    for( size_t i = 0; i < G_N_ELEMENTS(dsme_states); ++i ) {
        if( !strcmp(dsme_states[i].name, name) ) {
            state = dsme_states[i].state;
            break;
        }
    }

    return state;
}

/** Update cached dsme state
 */
static void
dsme_state_update(dsme_state_t state)
{
    LOG_REGISTER_CONTEXT;

    /* Handle state change */
    if( dsme_state_val != state ) {
        log_debug("dsme_state: %s -> %s",
                  dsme_state_repr(dsme_state_val),
                  dsme_state_repr(state));
        dsme_state_val = state;
    }

    /* Handle entry to / exit from USER state */
    bool user_state = (state == DSME_STATE_USER);

    if( dsme_user_state != user_state ) {
        dsme_user_state = user_state;
        log_debug("in user state: %s", dsme_user_state ? "true" : "false");

        control_rethink_usb_charging_fallback();
    }

    /* Handle entry to / exit from SHUTDOWN / REBOOT state */
    bool shutdown_state = (state == DSME_STATE_SHUTDOWN ||
                           state == DSME_STATE_REBOOT);

    if( dsme_shutdown_state != shutdown_state ) {
        dsme_shutdown_state = shutdown_state;
        log_debug("in shutdown: %s", dsme_shutdown_state ? "true" : "false");

        /* Re-evaluate dsmesock connection */
        if( !dsme_shutdown_state )
            dsme_socket_connect();
    }
}

/** Checks if the device is shutting down (or rebooting)
 *
 * @return true if device is shutting down, false otherwise
 */
static bool
dsme_state_is_shutdown(void)
{
    LOG_REGISTER_CONTEXT;

    return dsme_shutdown_state;
}

/** Checks if the device is is USER-state.
 *
 * @return true if device is in USER-state, false otherwise
 */
static bool
dsme_state_is_user(void)
{
    LOG_REGISTER_CONTEXT;

    return dsme_user_state;
}

/* ========================================================================= *
 * DSME_SOCKET_IPC
 * ========================================================================= */

/* Connection object for libdsme based ipc with dsme */
static dsmesock_connection_t *dsme_socket_con = NULL;

/** I/O watch for dsme_socket_con */
static guint dsme_socket_iowatch = 0;

/** Generic send function for dsmesock messages
 *
 * @param msg A pointer to the message to send
 */
static bool
dsme_socket_send_message(gpointer msg)
{
    LOG_REGISTER_CONTEXT;

    bool res = false;

    if( !dsme_socket_con ) {
        log_warning("failed to send %s to dsme; %s",
                    dsmemsg_name(msg),"not connected");
        goto EXIT;
    }

    if( dsmesock_send(dsme_socket_con, msg) == -1) {
        log_err("failed to send %s to dsme; %m",
                dsmemsg_name(msg));
    }

    log_debug("%s sent to DSME", dsmemsg_name(msg));

    res = true;

EXIT:
    return res;
}

/** Send process watchdog pong message to DSME
 */
static void
dsme_socket_processwd_pong(void)
{
    LOG_REGISTER_CONTEXT;

    DSM_MSGTYPE_PROCESSWD_PONG msg =
        DSME_MSG_INIT(DSM_MSGTYPE_PROCESSWD_PONG);

    msg.pid = getpid();

    dsme_socket_send_message(&msg);
}

/** Register to DSME process watchdog
 */
static void
dsme_socket_processwd_init(void)
{
    LOG_REGISTER_CONTEXT;

    DSM_MSGTYPE_PROCESSWD_CREATE msg =
        DSME_MSG_INIT(DSM_MSGTYPE_PROCESSWD_CREATE);

    msg.pid = getpid();

    dsme_socket_send_message(&msg);
}

/** Unregister from DSME process watchdog
 */
static void
dsme_socket_processwd_quit(void)
{
    LOG_REGISTER_CONTEXT;

    DSM_MSGTYPE_PROCESSWD_DELETE msg =
        DSME_MSG_INIT(DSM_MSGTYPE_PROCESSWD_DELETE);

    msg.pid = getpid();

    dsme_socket_send_message(&msg);
}

/** Query current DSME state
 */
static void
dsme_socket_query_state(void)
{
    LOG_REGISTER_CONTEXT;

    DSM_MSGTYPE_STATE_QUERY msg =
        DSME_MSG_INIT(DSM_MSGTYPE_STATE_QUERY);

    dsme_socket_send_message(&msg);
}

/** Callback for pending I/O from dsmesock
 *
 * @param source     (not used)
 * @param condition  I/O condition that caused the callback to be called
 * @param data       (not used)
 *
 * @return TRUE if iowatch is to be kept, or FALSE if it should be removed
 */
static gboolean
dsme_socket_recv_cb(GIOChannel *source,
                    GIOCondition condition,
                    gpointer data)
{
    LOG_REGISTER_CONTEXT;

    gboolean keep_going = TRUE;
    dsmemsg_generic_t *msg = 0;

    DSM_MSGTYPE_STATE_CHANGE_IND *msg2;

    (void)source;
    (void)data;

    if( condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL) ) {
        if( !dsme_state_is_shutdown() )
            log_crit("DSME socket hangup/error");
        keep_going = FALSE;
        goto EXIT;
    }

    if( !(msg = dsmesock_receive(dsme_socket_con)) )
        goto EXIT;

    if( DSMEMSG_CAST(DSM_MSGTYPE_CLOSE, msg) ) {
        if( !dsme_state_is_shutdown() )
            log_warning("DSME socket closed");
        keep_going = FALSE;
    }
    else if( DSMEMSG_CAST(DSM_MSGTYPE_PROCESSWD_PING, msg) ) {
        dsme_socket_processwd_pong();

        /* Do heartbeat actions here */
        modesetting_verify_values();
    }
    else if( (msg2 = DSMEMSG_CAST(DSM_MSGTYPE_STATE_CHANGE_IND, msg)) ) {
        dsme_state_update(msg2->state);
    }
    else {
        log_debug("Unhandled %s message received from DSME",
                  dsmemsg_name(msg));
    }

EXIT:
    free(msg);

    if( !keep_going ) {
        if( !dsme_state_is_shutdown() ) {
            log_warning("DSME i/o notifier disabled;"
                        " assuming dsme was stopped");
        }

        /* mark notifier as removed */
        dsme_socket_iowatch = 0;

        /* close and wait for possible dsme restart */
        dsme_socket_disconnect();
    }

    return keep_going;
}

/** Predicate for: socket connection to dsme exists
 *
 * @return true if connected, false otherwise
 */
static bool
dsme_socket_is_connected(void)
{
    LOG_REGISTER_CONTEXT;

    return dsme_socket_iowatch;
}

/** Initialise dsmesock connection
 *
 * @return true on success, false on failure
 */
static bool
dsme_socket_connect(void)
{
    LOG_REGISTER_CONTEXT;

    GIOChannel *iochan = NULL;

    /* No new connections during shutdown */
    if( dsme_state_is_shutdown() )
        goto EXIT;

    /* No new connections uness dsme dbus service is available */
    if( !dsme_dbus_name_owner_available() )
        goto EXIT;

    /* Already connected ? */
    if( dsme_socket_iowatch )
        goto EXIT;

    log_debug("Opening DSME socket");

    if( !(dsme_socket_con = dsmesock_connect()) ) {
        log_err("Failed to open DSME socket");
        goto EXIT;
    }

    log_debug("Adding DSME socket notifier");

    if( !(iochan = g_io_channel_unix_new(dsme_socket_con->fd)) ) {
        log_err("Failed to set up I/O channel for DSME socket");
        goto EXIT;
    }

    dsme_socket_iowatch =
        g_io_add_watch(iochan,
                       G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                       dsme_socket_recv_cb, NULL);

    /* Register with DSME's process watchdog */
    dsme_socket_processwd_init();

    /* Query current state */
    dsme_socket_query_state();

EXIT:
    if( iochan ) g_io_channel_unref(iochan);

    /* All or nothing */
    if( !dsme_socket_iowatch )
        dsme_socket_disconnect();

    return dsme_socket_is_connected();
}

/** Close dsmesock connection
 */
static void
dsme_socket_disconnect(void)
{
    LOG_REGISTER_CONTEXT;

    if( dsme_socket_iowatch ) {
        log_debug("Removing DSME socket notifier");
        g_source_remove(dsme_socket_iowatch);
        dsme_socket_iowatch = 0;

        /* Still having had a live socket notifier means we have
         * initiated the dsmesock disconnect and need to deactivate
         * the process watchdog before actually disconnecting */
        dsme_socket_processwd_quit();
    }

    if( dsme_socket_con ) {
        log_debug("Closing DSME socket");
        dsmesock_close(dsme_socket_con);
        dsme_socket_con = 0;
    }
}

/* ========================================================================= *
 * DSME_DBUS_IPC
 * ========================================================================= */

/* SystemBus connection ref used for dsme dbus ipc */
static DBusConnection *dsme_dbus_con = NULL;

/* ------------------------------------------------------------------------- *
 * device state tracking
 * ------------------------------------------------------------------------- */

static void
dsme_dbus_device_state_update(const char *state)
{
    LOG_REGISTER_CONTEXT;

    dsme_state_update(dsme_state_parse(state));
}

static DBusPendingCall *dsme_dbus_device_state_query_pc = 0;

static void
dsme_dbus_device_state_query_cb(DBusPendingCall *pending, void *aptr)
{
    LOG_REGISTER_CONTEXT;

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

    dsme_dbus_device_state_update(dta);

EXIT:

    if( rsp ) dbus_message_unref(rsp);

    dbus_error_free(&err);

    dbus_pending_call_unref(dsme_dbus_device_state_query_pc),
        dsme_dbus_device_state_query_pc = 0;
}

static void
dsme_dbus_device_state_query(void)
{
    LOG_REGISTER_CONTEXT;

    DBusMessage     *req = NULL;
    DBusPendingCall *pc  = 0;

    dsme_dbus_device_state_cancel();

    if( !dsme_dbus_con ) {
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

    if( !dbus_connection_send_with_reply(dsme_dbus_con, req, &pc, -1) )
        goto EXIT;

    if( !pc )
        goto EXIT;

    if( !dbus_pending_call_set_notify(pc, dsme_dbus_device_state_query_cb, 0, 0) )
        goto EXIT;

    dsme_dbus_device_state_query_pc = pc, pc = 0;

EXIT:

    if( pc  ) dbus_pending_call_unref(pc);
    if( req ) dbus_message_unref(req);
}

static void
dsme_dbus_device_state_cancel(void)
{
    LOG_REGISTER_CONTEXT;

    if( dsme_dbus_device_state_query_pc ) {
        dbus_pending_call_cancel(dsme_dbus_device_state_query_pc);
        dbus_pending_call_unref(dsme_dbus_device_state_query_pc),
            dsme_dbus_device_state_query_pc = 0;
    }
}

static void
dsme_dbus_device_state_signal(DBusMessage *msg)
{
    LOG_REGISTER_CONTEXT;

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
        dsme_dbus_device_state_update(dta);
    }
    dbus_error_free(&err);
}

/* ------------------------------------------------------------------------- *
 * dsme name owner tracking
 * ------------------------------------------------------------------------- */

/* Flag for: dsme is available on system bus */
static gchar *dsme_dbus_name_owner_val = 0;

static bool
dsme_dbus_name_owner_available(void)
{
    LOG_REGISTER_CONTEXT;

    return dsme_dbus_name_owner_val != 0;
}

static void
dsme_dbus_name_owner_update(const char *owner)
{
    LOG_REGISTER_CONTEXT;

    if( owner && !*owner )
        owner = 0;

    if( g_strcmp0(dsme_dbus_name_owner_val, owner) ) {
        log_debug("dsme dbus name owner: %s -> %s",
                  dsme_dbus_name_owner_val ?: "none",
                  owner                    ?: "none");

        g_free(dsme_dbus_name_owner_val),
            dsme_dbus_name_owner_val = owner ? g_strdup(owner) : 0;

        /* Query current state on dsme startup and initiate
         * dsmesock connection for process watchdog activity.
         */
        if( dsme_dbus_name_owner_val ) {
            dsme_dbus_device_state_query();
            dsme_socket_connect();
        }
    }
}

static DBusPendingCall *dsme_dbus_name_owner_query_pc = 0;

static void
dsme_dbus_name_owner_query_cb(const char *owner)
{
    LOG_REGISTER_CONTEXT;

    dsme_dbus_name_owner_update(owner);

    dbus_pending_call_unref(dsme_dbus_name_owner_query_pc),
        dsme_dbus_name_owner_query_pc = 0;
}

static void
dsme_dbus_name_owner_query(void)
{
    LOG_REGISTER_CONTEXT;

    dsme_dbus_name_owner_cancel();

    umdbus_get_name_owner_async(DSME_DBUS_SERVICE,
                                dsme_dbus_name_owner_query_cb,
                                &dsme_dbus_name_owner_query_pc);
}

static void
dsme_dbus_name_owner_cancel(void)
{
    LOG_REGISTER_CONTEXT;

    if( dsme_dbus_name_owner_query_pc )
    {
        dbus_pending_call_cancel(dsme_dbus_name_owner_query_pc);
        dbus_pending_call_unref(dsme_dbus_name_owner_query_pc),
            dsme_dbus_name_owner_query_pc = 0;
    }
}

static void
dsme_dbus_name_owner_signal(DBusMessage *msg)
{
    LOG_REGISTER_CONTEXT;

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
        dsme_dbus_name_owner_update(curr);
    }
    dbus_error_free(&err);
}

/* ------------------------------------------------------------------------- *
 * dbus connection management
 * ------------------------------------------------------------------------- */

static DBusHandlerResult
dsme_dbus_filter_cb(DBusConnection *con, DBusMessage *msg, void *user_data)
{
    LOG_REGISTER_CONTEXT;

    (void)con;
    (void)user_data;

    if( dbus_message_is_signal(msg,
                               DSME_DBUS_SIGNAL_IFACE,
                               DSME_STATE_CHANGE_SIG) )
    {
        dsme_dbus_device_state_signal(msg);
    }
    else if( dbus_message_is_signal(msg,
                                    DBUS_INTERFACE_DBUS,
                                    DBUS_NAME_OWNER_CHANGED_SIG) )
    {
        dsme_dbus_name_owner_signal(msg);
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static bool
dsme_dbus_init(void)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    /* Get connection ref */
    if( (dsme_dbus_con = umdbus_get_connection()) == 0 )
    {
        log_err("Could not connect to dbus for dsme\n");
        goto cleanup;
    }

    /* Add filter callback */
    if( !dbus_connection_add_filter(dsme_dbus_con,
                                    dsme_dbus_filter_cb , 0, 0) )
    {
        log_err("adding system dbus filter for dsme failed");
        goto cleanup;
    }

    /* Add matches without blocking / error checking */
    dbus_bus_add_match(dsme_dbus_con, DSME_STATE_CHANGE_MATCH, 0);
    dbus_bus_add_match(dsme_dbus_con, DSME_OWNER_CHANGE_MATCH, 0);

    /* Initiate async dsme name owner query */
    dsme_dbus_name_owner_query();

    ack = true;

cleanup:

    return ack;
}

static void
dsme_dbus_quit(void)
{
    LOG_REGISTER_CONTEXT;

    /* Cancel any pending dbus queries */
    dsme_dbus_name_owner_cancel();
    dsme_dbus_device_state_cancel();

    /* Detach from SystemBus */
    if(dsme_dbus_con)
    {
        /* Remove filter callback */
        dbus_connection_remove_filter(dsme_dbus_con,
                                      dsme_dbus_filter_cb, 0);

        if( dbus_connection_get_is_connected(dsme_dbus_con) ) {
            /* Remove matches without blocking / error checking */
            dbus_bus_remove_match(dsme_dbus_con, DSME_STATE_CHANGE_MATCH, 0);
            dbus_bus_remove_match(dsme_dbus_con, DSME_OWNER_CHANGE_MATCH, 0);
        }

        /* Let go of connection ref */
        dbus_connection_unref(dsme_dbus_con),
            dsme_dbus_con = 0;
    }

    /* Release dynamic resources */
    g_free(dsme_dbus_name_owner_val),
        dsme_dbus_name_owner_val = 0;
}

/* ========================================================================= *
 * MODULE_API
 * ========================================================================= */

gboolean
dsme_start_listener(void)
{
    LOG_REGISTER_CONTEXT;

    return dsme_dbus_init();
}

void
dsme_stop_listener(void)
{
    LOG_REGISTER_CONTEXT;

    dsme_dbus_quit();
    dsme_socket_disconnect();
}

/** Checks if the device is is USER-state.
 *
 * @return 1 if it is in USER-state, 0 for not
 */
gboolean
dsme_in_user_state(void)
{
    LOG_REGISTER_CONTEXT;

    return dsme_state_is_user();
}
