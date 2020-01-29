/**
 * @file usb_moded-appsync-dbus.c
 *
 * Copyright (c) 2010 Nokia Corporation. All rights reserved.
 * Copyright (c) 2013 - 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Philippe De Swert <phdeswer@lumi.maa>
 * @author Philippe De Swert <philippedeswert@gmail.com>
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
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

#include "usb_moded-appsync-dbus-private.h"

#include "usb_moded-appsync.h"
#include "usb_moded-log.h"

#include <string.h>

#include <dbus/dbus.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * DBUSAPPSYNC
 * ------------------------------------------------------------------------- */

static void              dbusappsync_release_name      (void);
static gboolean          dbusappsync_obtain_name       (void);
static DBusHandlerResult dbusappsync_msg_handler       (DBusConnection *const connection, DBusMessage *const msg, gpointer const user_data);
static DBusHandlerResult dbusappsync_handle_disconnect (DBusConnection *conn, DBusMessage *msg, void *user_data);
static void              dbusappsync_cleanup_connection(void);
gboolean                 dbusappsync_init_connection   (void);
gboolean                 dbusappsync_init              (void);
void                     dbusappsync_cleanup           (void);
int                      dbusappsync_launch_app        (char *launch);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static DBusConnection *dbus_connection_ses  = NULL;  // connection
static gboolean        dbus_connection_name = FALSE; // have name
static gboolean        dbus_connection_disc = FALSE; // got disconnected

/* ========================================================================= *
 * Functions
 * ========================================================================= */

static void dbusappsync_release_name(void)
{
    LOG_REGISTER_CONTEXT;

    /* Drop the service name - if we have it */
    if( dbus_connection_ses && dbus_connection_name )
    {
        DBusError error = DBUS_ERROR_INIT;
        int ret = dbus_bus_release_name(dbus_connection_ses, USB_MODE_SERVICE, &error);

        switch( ret )
        {
        case DBUS_RELEASE_NAME_REPLY_RELEASED:
            // as expected
            log_debug("released name: %s", USB_MODE_SERVICE);
            break;
        case DBUS_RELEASE_NAME_REPLY_NON_EXISTENT:
            // weird, but since nobody owns the name ...
            log_debug("nonexisting name: %s", USB_MODE_SERVICE);
            break;
        case DBUS_RELEASE_NAME_REPLY_NOT_OWNER:
            log_warning("somebody else owns: %s", USB_MODE_SERVICE);
        }

        if( dbus_error_is_set(&error) )
        {
            log_debug("DBUS ERROR: %s, %s \n", error.name, error.message);
            dbus_error_free(&error);
        }
    }

    dbus_connection_name = FALSE;
}

static gboolean dbusappsync_obtain_name(void)
{
    LOG_REGISTER_CONTEXT;

    DBusError error = DBUS_ERROR_INIT;

    int ret;

    if( dbus_connection_name )
    {
        goto EXIT;
    }

    if( dbus_connection_ses == 0 )
    {
        goto EXIT;
    }

    /* Acquire D-Bus service name */
    ret = dbus_bus_request_name(dbus_connection_ses, USB_MODE_SERVICE, DBUS_NAME_FLAG_DO_NOT_QUEUE , &error);

    switch( ret )
    {
    case DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER:
        // expected result
        log_debug("primary owner of: %s", USB_MODE_SERVICE);
        break;

    case DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER:
        // functionally ok, but we do have a logic error somewhere
        log_warning("already owner of: %s", USB_MODE_SERVICE);
        break;

    default:
        // something odd
        log_err("failed to claim: %s", USB_MODE_SERVICE);
        goto EXIT;
    }

    dbus_connection_name = TRUE;

EXIT:

    if( dbus_error_is_set(&error) )
    {
        log_debug("DBUS ERROR: %s, %s \n", error.name, error.message);
        dbus_error_free(&error);
    }

    return dbus_connection_name;
}

/**
 * Handle USB_MODE_INTERFACE method calls
 */

static DBusHandlerResult dbusappsync_msg_handler(DBusConnection *const connection, DBusMessage *const msg, gpointer const user_data)
{
    LOG_REGISTER_CONTEXT;

    DBusHandlerResult   status    = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    int                 type      = dbus_message_get_type(msg);
    const char         *interface = dbus_message_get_interface(msg);
    const char         *member    = dbus_message_get_member(msg);
    const char         *object    = dbus_message_get_path(msg);

    if(!interface || !member || !object) goto IGNORE;

    if( type == DBUS_MESSAGE_TYPE_METHOD_CALL &&
        !strcmp(interface, USB_MODE_INTERFACE) &&
        !strcmp(object, USB_MODE_OBJECT) )

    {
        DBusMessage *reply = 0;

        status = DBUS_HANDLER_RESULT_HANDLED;

        if(!strcmp(member, USB_MODE_APP_STATE))
        {
            char      *use = 0;
            DBusError  err = DBUS_ERROR_INIT;

            if(!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &use, DBUS_TYPE_INVALID))
            {
                // could not parse method call args
                reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, member);
            }
            else if( appsync_mark_active(use, 1) < 0 )
            {
                // name could not be marked active
                reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, member);
            }
            else if((reply = dbus_message_new_method_return(msg)))
            {
                // generate normal reply
                dbus_message_append_args (reply, DBUS_TYPE_STRING, &use, DBUS_TYPE_INVALID);
            }
            dbus_error_free(&err);
        }
        else
        {
            /*unknown methods are handled here */
            reply = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD, member);
        }

        if( !dbus_message_get_no_reply(msg) )
        {
            if( !reply )
            {
                // we failed to generate reply above -> generate one
                reply = dbus_message_new_error(msg, DBUS_ERROR_FAILED, member);
            }
            if( !reply || !dbus_connection_send(connection, reply, 0) )
            {
                log_debug("Failed sending reply. Out Of Memory!\n");
            }
        }

        if( reply ) dbus_message_unref(reply);
    }

IGNORE:

    return status;
}

/**
 * Handle disconnect signals
 */
static DBusHandlerResult dbusappsync_handle_disconnect(DBusConnection *conn, DBusMessage *msg, void *user_data)
{
    LOG_REGISTER_CONTEXT;

    if( dbus_message_is_signal(msg, DBUS_INTERFACE_LOCAL, "Disconnected") )
    {
        log_warning("disconnected from session bus - expecting restart/stop soon\n");
        dbus_connection_disc = TRUE;
        dbusappsync_cleanup_connection();
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/**
 * Detach from session bus
 */
static void dbusappsync_cleanup_connection(void)
{
    LOG_REGISTER_CONTEXT;

    if( dbus_connection_ses != 0 )
    {
        /* Remove message filters */
        dbus_connection_remove_filter(dbus_connection_ses, dbusappsync_msg_handler, 0);
        dbus_connection_remove_filter(dbus_connection_ses, dbusappsync_handle_disconnect, 0);

        /* Release name, but only if we can still talk to dbus daemon */
        if( !dbus_connection_disc )
        {
            dbusappsync_release_name();
        }

        dbus_connection_unref(dbus_connection_ses);
        dbus_connection_ses = NULL;
        //dbus_connection_disc = FALSE;
    }
    log_debug("succesfully cleaned up appsync dbus\n");
}

/**
 * Attach to session bus
 */
gboolean dbusappsync_init_connection(void)
{
    LOG_REGISTER_CONTEXT;

    gboolean  result = FALSE;
    DBusError error  = DBUS_ERROR_INIT;

    if( dbus_connection_ses != 0 )
    {
        result = TRUE;
        goto EXIT;
    }

    if( dbus_connection_disc )
    {
        // we've already observed death of session
        goto EXIT;
    }

    /* Connect to session bus */
    if ((dbus_connection_ses = dbus_bus_get(DBUS_BUS_SESSION, &error)) == NULL)
    {
        log_err("Failed to open connection to session message bus; %s\n",  error.message);
        goto EXIT;
    }

    /* Add disconnect handler */
    dbus_connection_add_filter(dbus_connection_ses, dbusappsync_handle_disconnect, 0, 0);

    /* Add method call handler */
    dbus_connection_add_filter(dbus_connection_ses, dbusappsync_msg_handler, 0, 0);

    /* Make sure we do not get forced to exit if dbus session dies or stops */
    dbus_connection_set_exit_on_disconnect(dbus_connection_ses, FALSE);

    /* Connect D-Bus to the mainloop (Seems it is only needed once and is done at the main
     * D-Bus init
     * dbus_connection_setup_with_g_main(dbus_connection_ses, NULL);
     */

    /* Request service name */
    if( !dbusappsync_obtain_name() )
    {
        goto EXIT;
    }

    /* everything went fine */
    result = TRUE;

EXIT:
    dbus_error_free(&error);
    return result;
}

/**
 * Init dbus for usb_moded application synchronisation
 *
 * @return TRUE when everything went ok
 */
gboolean dbusappsync_init(void)
{
    LOG_REGISTER_CONTEXT;

    gboolean status = FALSE;

    if( !dbusappsync_init_connection() )
    {
        goto EXIT;
    }

    /* everything went fine */
    status = TRUE;

EXIT:
    return status;
}

/**
 * Clean up the dbus connections for the application
 * synchronisation after sync is done
 */
void dbusappsync_cleanup(void)
{
    LOG_REGISTER_CONTEXT;

    dbusappsync_cleanup_connection();
    // NOP
}

/**
 * Launch applications over dbus that need to be synchronized
 */
int dbusappsync_launch_app(char *launch)
{
    LOG_REGISTER_CONTEXT;

    int ret = -1; // assume failure

    if( dbus_connection_ses == 0 )
    {
        log_err("could not start '%s': no session bus connection", launch);
    }
    else
    {
        DBusError error = DBUS_ERROR_INIT;
        if( !dbus_bus_start_service_by_name(dbus_connection_ses, launch, 0, NULL, &error) )
        {
            log_err("could not start '%s': %s: %s", launch, error.name, error.message);
            dbus_error_free(&error);
        }
        else
        {
            ret = 0; // success
        }
    }
    return ret;
}
