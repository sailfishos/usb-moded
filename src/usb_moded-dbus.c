/**
 * @file        usb_moded-dbus.c
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Copyright (C) 2012-2019 Jolla. All rights reserved.
 *
 * @author: Philippe De Swert <philippe.de-swert@nokia.com>
 * @author: Philippe De Swert <phdeswer@lumi.maa>
 * @author: Philippe De Swert <philippedeswert@gmail.com>
 * @author: Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author: Vesa Halttunen <vesa.halttunen@jollamobile.com>
 * @author: Slava Monich <slava.monich@jolla.com>
 * @author: Martin Jones <martin.jones@jollamobile.com>
 * @author: Simo Piiroinen <simo.piiroinen@jollamobile.com>
 * @author: Andrew den Exter <andrew.den.exter@jolla.com>
 * @author: Andrew den Exter <andrew.den.exter@jollamobile.com>
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

#include "usb_moded-dbus-private.h"

#include "usb_moded-config-private.h"
#include "usb_moded-control.h"
#include "usb_moded-log.h"
#include "usb_moded-modes.h"
#include "usb_moded-network.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <dbus/dbus-glib-lowlevel.h>

/* ========================================================================= *
 * Constants
 * ========================================================================= */

#define INIT_DONE_INTERFACE "com.nokia.startup.signal"
#define INIT_DONE_SIGNAL    "init_done"
#define INIT_DONE_MATCH     "type='signal',interface='"INIT_DONE_INTERFACE"',member='"INIT_DONE_SIGNAL"'"

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * UMDBUS
 * ------------------------------------------------------------------------- */

void                      umdbus_send_config_signal           (const char *section, const char *key, const char *value);
static DBusHandlerResult  umdbus_msg_handler                  (DBusConnection *const connection, DBusMessage *const msg, gpointer const user_data);
DBusConnection           *umdbus_get_connection               (void);
gboolean                  umdbus_init_connection              (void);
gboolean                  umdbus_init_service                 (void);
static void               umdbus_cleanup_service              (void);
void                      umdbus_cleanup                      (void);
static DBusMessage       *umdbus_new_signal                   (const char *signal_name);
static int                umdbus_send_signal_ex               (const char *signal_name, const char *content);
static void               umdbus_send_legacy_signal           (const char *state_ind);
void                      umdbus_send_current_state_signal    (const char *state_ind);
static bool               umdbus_append_basic_entry           (DBusMessageIter *iter, const char *key, int type, const void *val);
static bool               umdbus_append_int32_entry           (DBusMessageIter *iter, const char *key, int val);
static bool               umdbus_append_string_entry          (DBusMessageIter *iter, const char *key, const char *val);
static bool               umdbus_append_mode_details          (DBusMessage *msg, const char *mode_name);
static void               umdbus_send_mode_details_signal     (const char *mode_name);
void                      umdbus_send_target_state_signal     (const char *state_ind);
void                      umdbus_send_event_signal            (const char *state_ind);
int                       umdbus_send_error_signal            (const char *error);
int                       umdbus_send_supported_modes_signal  (const char *supported_modes);
int                       umdbus_send_available_modes_signal  (const char *available_modes);
int                       umdbus_send_hidden_modes_signal     (const char *hidden_modes);
int                       umdbus_send_whitelisted_modes_signal(const char *whitelist);
static void               umdbus_get_name_owner_cb            (DBusPendingCall *pc, void *aptr);
gboolean                  umdbus_get_name_owner_async         (const char *name, usb_moded_get_name_owner_fn cb, DBusPendingCall **ppc);
uid_t                     umdbus_get_sender_uid               (const char *sender);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static DBusConnection *umdbus_connection = NULL;
static gboolean        umdbus_service_name_acquired   = FALSE;

/** Introspect xml format string for parents of USB_MODE_OBJECT */
static const char umdbus_introspect_template[] =
"<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
"<node name=\"%s\">\n"
"  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
"    <method name=\"Introspect\">\n"
"      <arg direction=\"out\" name=\"data\" type=\"s\"/>\n"
"    </method>\n"
"  </interface>\n"
"  <interface name=\"org.freedesktop.DBus.Peer\">\n"
"    <method name=\"Ping\"/>\n"
"    <method name=\"GetMachineId\">\n"
"      <arg direction=\"out\" name=\"machine_uuid\" type=\"s\" />\n"
"    </method>\n"
"  </interface>\n"
"  <node name=\"%s\"/>\n"
"</node>\n";

/** Introspect xml data for object path USB_MODE_OBJECT */
static const char umdbus_introspect_usbmoded[] =
"<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
"\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
"<node name=\"" USB_MODE_OBJECT "\">\n"
"  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
"    <method name=\"Introspect\">\n"
"      <arg name=\"xml\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"
"  </interface>\n"
"  <interface name=\"org.freedesktop.DBus.Peer\">\n"
"    <method name=\"Ping\"/>\n"
"    <method name=\"GetMachineId\">\n"
"      <arg direction=\"out\" name=\"machine_uuid\" type=\"s\" />\n"
"    </method>\n"
"  </interface>\n"
"  <interface name=\"" USB_MODE_INTERFACE "\">\n"
"    <method name=\"" USB_MODE_STATE_REQUEST "\">\n"
"      <arg name=\"mode\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\""  USB_MODE_TARGET_STATE_GET "\">\n"
"      <arg name=\"mode\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"" USB_MODE_STATE_SET "\">\n"
"      <arg name=\"mode\" type=\"s\" direction=\"in\"/>\n"
"      <arg name=\"mode\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"" USB_MODE_CONFIG_SET "\">\n"
"      <arg name=\"config\" type=\"s\" direction=\"in\"/>\n"
"      <arg name=\"config\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"" USB_MODE_NETWORK_SET "\">\n"
"      <arg name=\"key\" type=\"s\" direction=\"in\"/>\n"
"      <arg name=\"value\" type=\"s\" direction=\"in\"/>\n"
"      <arg name=\"key\" type=\"s\" direction=\"out\"/>\n"
"      <arg name=\"value\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"" USB_MODE_NETWORK_GET "\">\n"
"      <arg name=\"key\" type=\"s\" direction=\"in\"/>\n"
"      <arg name=\"key\" type=\"s\" direction=\"out\"/>\n"
"      <arg name=\"value\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"" USB_MODE_CONFIG_GET "\">\n"
"      <arg name=\"mode\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"" USB_MODE_LIST "\">\n"
"      <arg name=\"modes\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"" USB_MODE_AVAILABLE_MODES_GET "\">\n"
"      <arg name=\"modes\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"" USB_MODE_HIDE "\">\n"
"      <arg name=\"mode\" type=\"s\" direction=\"in\"/>\n"
"      <arg name=\"mode\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"" USB_MODE_UNHIDE "\">\n"
"      <arg name=\"mode\" type=\"s\" direction=\"in\"/>\n"
"      <arg name=\"mode\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"" USB_MODE_HIDDEN_GET "\">\n"
"      <arg name=\"modes\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"" USB_MODE_WHITELISTED_MODES_GET "\">\n"
"      <arg name=\"modes\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"" USB_MODE_WHITELISTED_MODES_SET "\">\n"
"      <arg name=\"modes\" type=\"s\" direction=\"in\"/>\n"
"    </method>\n"
"    <method name=\"" USB_MODE_WHITELISTED_SET "\">\n"
"      <arg name=\"mode\" type=\"s\" direction=\"in\"/>\n"
"      <arg name=\"whitelisted\" type=\"b\" direction=\"in\"/>\n"
"    </method>\n"
"    <method name=\"" USB_MODE_RESCUE_OFF "\"/>\n"
"    <signal name=\"" USB_MODE_SIGNAL_NAME "\">\n"
"      <arg name=\"mode_or_event\" type=\"s\"/>\n"
"    </signal>\n"
"    <signal name=\"" USB_MODE_CURRENT_STATE_SIGNAL_NAME "\">\n"
"      <arg name=\"mode\" type=\"s\"/>\n"
"    </signal>\n"
"    <signal name=\"" USB_MODE_TARGET_STATE_SIGNAL_NAME "\">\n"
"      <arg name=\"mode\" type=\"s\"/>\n"
"    </signal>\n"
"    <signal name=\"" USB_MODE_EVENT_SIGNAL_NAME "\">\n"
"      <arg name=\"event\" type=\"s\"/>\n"
"    </signal>\n"
"    <signal name=\"" USB_MODE_ERROR_SIGNAL_NAME "\">\n"
"      <arg name=\"error\" type=\"s\"/>\n"
"    </signal>\n"
"    <signal name=\"" USB_MODE_SUPPORTED_MODES_SIGNAL_NAME "\">\n"
"      <arg name=\"modes\" type=\"s\"/>\n"
"    </signal>\n"
"    <signal name=\"" USB_MODE_AVAILABLE_MODES_SIGNAL_NAME "\">\n"
"      <arg name=\"modes\" type=\"s\"/>\n"
"    </signal>\n"
"    <signal name=\"" USB_MODE_CONFIG_SIGNAL_NAME "\">\n"
"      <arg name=\"section\" type=\"s\"/>\n"
"      <arg name=\"key\" type=\"s\"/>\n"
"      <arg name=\"value\" type=\"s\"/>\n"
"    </signal>\n"
"    <signal name=\"" USB_MODE_HIDDEN_MODES_SIGNAL_NAME "\">\n"
"      <arg name=\"modes\" type=\"s\"/>\n"
"    </signal>\n"
"    <signal name=\"" USB_MODE_WHITELISTED_MODES_SIGNAL_NAME "\">\n"
"      <arg name=\"modes\" type=\"s\"/>\n"
"    </signal>\n"
"    <signal name=\"" USB_MODE_TARGET_CONFIG_SIGNAL_NAME "\">\n"
"      <arg name=\"config\" type=\"a{sv}\"/>\n"
"      <annotation name=\"org.qtproject.QtDBus.QtTypeName.In0\" value=\"QVariantMap\"/>\n"
"    </signal>\n"
"    <method name=\"" USB_MODE_TARGET_CONFIG_GET "\">\n"
"      <arg name=\"config\" type=\"a{sv}\" direction=\"out\"/>\n"
"      <annotation name=\"org.qtproject.QtDBus.QtTypeName.Out0\" value=\"QVariantMap\"/>\n"
"    </signal>\n"
"  </interface>\n"
"</node>\n";

/* ========================================================================= *
 * Functions
 * ========================================================================= */

/**
 * Issues "sig_usb_config_ind" signal.
*/
void umdbus_send_config_signal(const char *section, const char *key, const char *value)
{
    LOG_REGISTER_CONTEXT;

    DBusMessage* msg = 0;

    if( !section || !key || !value )  {
        log_err("config notification with NULL %s",
                !section ? "section" : !key ? "key" : value);
        goto EXIT;
    }

    if( !umdbus_service_name_acquired ) {
        log_err("config notification without service: [%s] %s=%s",
                section, key, value);
        goto EXIT;
    }

    if( !umdbus_connection ) {
        log_err("config notification without connection: [%s] %s=%s",
                section, key, value);
        goto EXIT;
    }

    log_debug("broadcast signal %s(%s, %s, %s)\n", USB_MODE_CONFIG_SIGNAL_NAME, section, key, value);

    msg = dbus_message_new_signal(USB_MODE_OBJECT, USB_MODE_INTERFACE, USB_MODE_CONFIG_SIGNAL_NAME);
    if( !msg )
        goto EXIT;

    dbus_message_append_args(msg, DBUS_TYPE_STRING, &section,
                             DBUS_TYPE_STRING, &key,
                             DBUS_TYPE_STRING, &value,
                             DBUS_TYPE_INVALID);
    dbus_connection_send(umdbus_connection, msg, NULL);

EXIT:
    if( msg )
        dbus_message_unref(msg);
}

static DBusHandlerResult umdbus_msg_handler(DBusConnection *const connection, DBusMessage *const msg, gpointer const user_data)
{
    LOG_REGISTER_CONTEXT;

    DBusHandlerResult   status    = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    DBusMessage        *reply     = 0;
    const char         *interface = dbus_message_get_interface(msg);
    const char         *member    = dbus_message_get_member(msg);
    const char         *object    = dbus_message_get_path(msg);
    int                 type      = dbus_message_get_type(msg);

    (void)user_data;

    if(!interface || !member || !object) goto EXIT;

    log_debug("DBUS %s %s.%s from %s",
              dbus_message_type_to_string(type),
              interface, member,
              dbus_message_get_sender(msg) ?: "N/A");

    if( type == DBUS_MESSAGE_TYPE_SIGNAL )
    {
        if( !strcmp(interface, INIT_DONE_INTERFACE) && !strcmp(member, INIT_DONE_SIGNAL) ) {
            /* Update the cached state value */
            usbmoded_set_init_done(true);

            /* Auto-disable rescue mode when bootup is finished */
            if( usbmoded_get_rescue_mode() ) {
                usbmoded_set_rescue_mode(false);
                log_debug("init done reached - rescue mode disabled");
            }
        }
        goto EXIT;
    }

    if( type == DBUS_MESSAGE_TYPE_METHOD_CALL && !strcmp(interface, USB_MODE_INTERFACE) && !strcmp(object, USB_MODE_OBJECT))
    {
        status = DBUS_HANDLER_RESULT_HANDLED;

        if(!strcmp(member, USB_MODE_STATE_REQUEST))
        {
            const char *mode = control_get_external_mode();

            /* To the outside we want to keep CHARGING and CHARGING_FALLBACK the same */
            if(!strcmp(MODE_CHARGING_FALLBACK, mode))
                mode = MODE_CHARGING;
            if((reply = dbus_message_new_method_return(msg)))
                dbus_message_append_args (reply, DBUS_TYPE_STRING, &mode, DBUS_TYPE_INVALID);
        }
        else if(!strcmp(member, USB_MODE_TARGET_CONFIG_GET))
        {
            const char *mode = control_get_target_mode();
            if((reply = dbus_message_new_method_return(msg)))
                umdbus_append_mode_details(reply, mode);
        }
        else if(!strcmp(member, USB_MODE_TARGET_STATE_GET))
        {
            const char *mode = control_get_target_mode();
            if((reply = dbus_message_new_method_return(msg)))
                dbus_message_append_args (reply, DBUS_TYPE_STRING, &mode, DBUS_TYPE_INVALID);
        }
        else if(!strcmp(member, USB_MODE_STATE_SET))
        {
            const char *mode = control_get_external_mode();
            char       *use  = 0;
            DBusError   err  = DBUS_ERROR_INIT;

            if(!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &use, DBUS_TYPE_INVALID)) {
                log_err("parse error: %s: %s", err.name, err.message);
                reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, member);
            }
            else if( control_get_cable_state() != CABLE_STATE_PC_CONNECTED ) {
                /* Mode change makes no sence unless we have a PC connection */
                log_warning("Mode '%s' requested while not connected to pc", use);
            }
            else if( common_valid_mode(use) ) {
                /* Mode does not exist */
                log_warning("Unknown mode '%s' requested", use);
            }
            else if( !g_strcmp0(mode, MODE_BUSY) ) {
                /* In middle of a pending mode switch */
                log_warning("Mode '%s' requested while busy", use);
            }
            else {
                log_debug("Mode '%s' requested", use);
                /* Initiate mode switch */
                control_set_usb_mode(use);

                /* Acknowledge that the mode request was accepted */
                if((reply = dbus_message_new_method_return(msg)))
                    dbus_message_append_args (reply, DBUS_TYPE_STRING, &use, DBUS_TYPE_INVALID);
            }

            /* Default to returning a generic error reply */
            if( !reply )
                reply = dbus_message_new_error(msg, DBUS_ERROR_FAILED, member);

            dbus_error_free(&err);
        }
        else if(!strcmp(member, USB_MODE_CONFIG_SET))
        {
            char *config = 0;
            DBusError   err = DBUS_ERROR_INIT;

            if(!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID))
                reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, member);
            else
            {
                /* error checking is done when setting configuration */
                int ret = config_set_mode_setting(config);
                if (SET_CONFIG_OK(ret))
                {
                    if((reply = dbus_message_new_method_return(msg)))
                        dbus_message_append_args (reply, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID);
                }
                else
                    reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, config);
            }
            dbus_error_free(&err);
        }
        else if(!strcmp(member, USB_MODE_HIDE))
        {
            char *config = 0;
            DBusError   err = DBUS_ERROR_INIT;

            if(!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID))
                reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, member);
            else
            {
                /* error checking is done when setting configuration */
                int ret = config_set_hide_mode_setting(config);
                if (SET_CONFIG_OK(ret))
                {
                    if((reply = dbus_message_new_method_return(msg)))
                        dbus_message_append_args (reply, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID);
                }
                else
                    reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, config);
            }
            dbus_error_free(&err);
        }
        else if(!strcmp(member, USB_MODE_UNHIDE))
        {
            char *config = 0;
            DBusError   err = DBUS_ERROR_INIT;

            if(!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID))
                reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, member);
            else
            {
                /* error checking is done when setting configuration */
                int ret = config_set_unhide_mode_setting(config);
                if (SET_CONFIG_OK(ret))
                {
                    if((reply = dbus_message_new_method_return(msg)))
                        dbus_message_append_args (reply, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID);
                }
                else
                    reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, config);
            }
            dbus_error_free(&err);
        }
        else if(!strcmp(member, USB_MODE_HIDDEN_GET))
        {
            char *config = config_get_hidden_modes();
            if(!config)
                config = g_strdup("");
            if((reply = dbus_message_new_method_return(msg)))
                dbus_message_append_args (reply, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID);
            g_free(config);
        }
        else if(!strcmp(member, USB_MODE_NETWORK_SET))
        {
            char *config = 0, *setting = 0;
            DBusError   err = DBUS_ERROR_INIT;

            if(!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &config, DBUS_TYPE_STRING, &setting, DBUS_TYPE_INVALID))
                reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, member);
            else
            {
                /* error checking is done when setting configuration */
                int ret = config_set_network_setting(config, setting);
                if (SET_CONFIG_OK(ret))
                {
                    if((reply = dbus_message_new_method_return(msg)))
                        dbus_message_append_args (reply, DBUS_TYPE_STRING, &config, DBUS_TYPE_STRING, &setting, DBUS_TYPE_INVALID);
                    network_update();
                }
                else
                    reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, config);
            }
            dbus_error_free(&err);
        }
        else if(!strcmp(member, USB_MODE_NETWORK_GET))
        {
            char *config = 0;
            char *setting = 0;
            DBusError   err = DBUS_ERROR_INIT;

            if(!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID))
            {
                reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, member);
            }
            else
            {
                setting = config_get_network_setting(config);
                if(setting)
                {
                    if((reply = dbus_message_new_method_return(msg)))
                        dbus_message_append_args (reply, DBUS_TYPE_STRING, &config, DBUS_TYPE_STRING, &setting, DBUS_TYPE_INVALID);
                    free(setting);
                }
                else
                    reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, config);
            }
        }
        else if(!strcmp(member, USB_MODE_CONFIG_GET))
        {
            char *config = config_get_mode_setting();

            if((reply = dbus_message_new_method_return(msg)))
                dbus_message_append_args (reply, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID);
            g_free(config);
        }
        else if(!strcmp(member, USB_MODE_LIST))
        {
            gchar *mode_list = common_get_mode_list(SUPPORTED_MODES_LIST, 0);

            if((reply = dbus_message_new_method_return(msg)))
                dbus_message_append_args (reply, DBUS_TYPE_STRING, (const char *) &mode_list, DBUS_TYPE_INVALID);
            g_free(mode_list);
        }
        else if(!strcmp(member, USB_MODE_AVAILABLE_MODES_GET))
        {
            gchar *mode_list = common_get_mode_list(AVAILABLE_MODES_LIST, 0);

            if((reply = dbus_message_new_method_return(msg)))
                dbus_message_append_args (reply, DBUS_TYPE_STRING, (const char *) &mode_list, DBUS_TYPE_INVALID);
            g_free(mode_list);
        }
        else if(!strcmp(member, USB_MODE_RESCUE_OFF))
        {
            usbmoded_set_rescue_mode(false);
            log_debug("Rescue mode off\n ");
            reply = dbus_message_new_method_return(msg);
        }
        else if(!strcmp(member, USB_MODE_WHITELISTED_MODES_GET))
        {
            gchar *mode_list = config_get_mode_whitelist();

            if(!mode_list)
                mode_list = g_strdup("");

            if((reply = dbus_message_new_method_return(msg)))
                dbus_message_append_args(reply, DBUS_TYPE_STRING, &mode_list, DBUS_TYPE_INVALID);
            g_free(mode_list);
        }
        else if(!strcmp(member, USB_MODE_WHITELISTED_MODES_SET))
        {
            const char *whitelist = 0;
            DBusError err = DBUS_ERROR_INIT;

            if (!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &whitelist, DBUS_TYPE_INVALID))
                reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, member);
            else
            {
                int ret = config_set_mode_whitelist(whitelist);
                if (SET_CONFIG_OK(ret))
                {
                    if ((reply = dbus_message_new_method_return(msg)))
                        dbus_message_append_args(reply, DBUS_TYPE_STRING, &whitelist, DBUS_TYPE_INVALID);
                }
                else
                    reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, whitelist);
            }
            dbus_error_free(&err);
        }
        else if (!strcmp(member, USB_MODE_WHITELISTED_SET))
        {
            const char *mode = 0;
            dbus_bool_t enabled = FALSE;
            DBusError err = DBUS_ERROR_INIT;

            if (!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &mode, DBUS_TYPE_BOOLEAN, &enabled, DBUS_TYPE_INVALID))
                reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, member);
            else
            {
                int ret = config_set_mode_in_whitelist(mode, enabled);
                if (SET_CONFIG_OK(ret))
                    reply = dbus_message_new_method_return(msg);
                else
                    reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, mode);
            }
            dbus_error_free(&err);
        }
        else
        {
            /*unknown methods are handled here */
            reply = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD, member);
        }

        if( !reply )
        {
            reply = dbus_message_new_error(msg, DBUS_ERROR_FAILED, member);
        }
    }
    else if( type == DBUS_MESSAGE_TYPE_METHOD_CALL &&
             !strcmp(interface, "org.freedesktop.DBus.Introspectable") &&
             !strcmp(member, "Introspect"))
    {
        const gchar *xml = 0;
        gchar       *tmp = 0;
        gchar       *err = 0;
        size_t       len = strlen(object);
        const char  *pos = USB_MODE_OBJECT;

        if( !strncmp(object, pos, len) )
        {
            if( pos[len] == 0 )
            {
                /* Full length USB_MODE_OBJECT requested */
                xml = umdbus_introspect_usbmoded;
            }
            else if( pos[len] == '/' )
            {
                /* Leading part of USB_MODE_OBJECT requested */
                gchar *parent = 0;
                gchar *child = 0;
                parent = g_strndup(pos, len);
                pos += len + 1;
                len = strcspn(pos, "/");
                child = g_strndup(pos, len);
                xml = tmp = g_strdup_printf(umdbus_introspect_template,
                                            parent, child);
                g_free(child);
                g_free(parent);
            }
            else if( !strcmp(object, "/") )
            {
                /* Root object needs to be handled separately */
                const char *parent = "/";
                gchar *child = 0;
                pos += 1;
                len = strcspn(pos, "/");
                child = g_strndup(pos, len);
                xml = tmp = g_strdup_printf(umdbus_introspect_template,
                                            parent, child);
                g_free(child);
            }
        }

        if( xml )
        {
            if((reply = dbus_message_new_method_return(msg)))
                dbus_message_append_args (reply,
                                          DBUS_TYPE_STRING, &xml,
                                          DBUS_TYPE_INVALID);
        }
        else
        {
            err = g_strdup_printf("Object '%s' does not exist", object);
            reply = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_OBJECT,
                                           err);
        }

        g_free(err);
        g_free(tmp);

        if( reply )
        {
            status = DBUS_HANDLER_RESULT_HANDLED;
        }
    }

EXIT:

    if(reply)
    {
        if( !dbus_message_get_no_reply(msg) )
        {
            if( !dbus_connection_send(connection, reply, 0) )
                log_debug("Failed sending reply. Out Of Memory!\n");
        }
        dbus_message_unref(reply);
    }

    return status;
}

DBusConnection *umdbus_get_connection(void)
{
    LOG_REGISTER_CONTEXT;

    DBusConnection *connection = 0;
    if( umdbus_connection )
        connection = dbus_connection_ref(umdbus_connection);
    else
        log_err("something asked for connection ref while unconnected");
    return connection;
}

/**
 * Establish D-Bus SystemBus connection
 *
 * @return TRUE when everything went ok
 */
gboolean umdbus_init_connection(void)
{
    LOG_REGISTER_CONTEXT;

    gboolean status = FALSE;
    DBusError error = DBUS_ERROR_INIT;

    /* connect to system bus */
    if ((umdbus_connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error)) == NULL)
    {
        log_debug("Failed to open connection to system message bus; %s\n",  error.message);
        goto EXIT;
    }

    /* Initialise message handlers */
    if (!dbus_connection_add_filter(umdbus_connection, umdbus_msg_handler, NULL, NULL))
        goto EXIT;

    /* Listen to init-done signals */
    dbus_bus_add_match(umdbus_connection, INIT_DONE_MATCH, 0);

    /* Re-check flag file after adding signal listener */
    usbmoded_probe_init_done();

    /* Connect D-Bus to the mainloop */
    dbus_connection_setup_with_g_main(umdbus_connection, NULL);

    /* everything went fine */
    status = TRUE;

EXIT:
    dbus_error_free(&error);
    return status;
}

/**
 * Reserve "com.meego.usb_moded" D-Bus Service Name
 *
 * @return TRUE when everything went ok
 */
gboolean umdbus_init_service(void)
{
    LOG_REGISTER_CONTEXT;

    gboolean status = FALSE;
    DBusError error = DBUS_ERROR_INIT;
    int ret;

    if( !umdbus_connection ) {
        goto EXIT;
    }

    /* Acquire D-Bus service */
    ret = dbus_bus_request_name(umdbus_connection, USB_MODE_SERVICE, DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    {
        log_debug("failed claiming dbus name\n");
        if( dbus_error_is_set(&error) )
            log_debug("DBUS ERROR: %s, %s \n", error.name, error.message);
        goto EXIT;
    }
    log_debug("claimed name %s", USB_MODE_SERVICE);
    umdbus_service_name_acquired = TRUE;
    /* everything went fine */
    status = TRUE;

EXIT:
    dbus_error_free(&error);
    return status;
}

/** Release "com.meego.usb_moded" D-Bus Service Name
 */
static void umdbus_cleanup_service(void)
{
    LOG_REGISTER_CONTEXT;

    if( !umdbus_service_name_acquired )
        goto EXIT;

    umdbus_service_name_acquired = FALSE;
    log_debug("release name %s", USB_MODE_SERVICE);

    if( umdbus_connection &&
        dbus_connection_get_is_connected(umdbus_connection) )
    {
        dbus_bus_release_name(umdbus_connection, USB_MODE_SERVICE, NULL);
    }

EXIT:
    return;
}

/**
 * Clean up the dbus connections on exit
 *
 */
void umdbus_cleanup(void)
{
    LOG_REGISTER_CONTEXT;

    /* clean up system bus connection */
    if (umdbus_connection != NULL)
    {
        umdbus_cleanup_service();

        dbus_connection_remove_filter(umdbus_connection, umdbus_msg_handler, NULL);

        dbus_connection_unref(umdbus_connection),
            umdbus_connection = NULL;
    }
}

/** Helper for allocating usb-moded D-Bus signal
 *
 * @param signal_name Name of the signal to allocate
 *
 * @return dbus message object, or NULL in case of errors
 */
static DBusMessage*
umdbus_new_signal(const char *signal_name)
{
    LOG_REGISTER_CONTEXT;

    DBusMessage *msg = 0;

    if( !umdbus_connection )
    {
        log_err("sending signal %s without dbus connection", signal_name);
        goto EXIT;
    }
    if( !umdbus_service_name_acquired )
    {
        log_err("sending signal %s before acquiring name", signal_name);
        goto EXIT;
    }
    // create a signal and check for errors
    msg = dbus_message_new_signal(USB_MODE_OBJECT, USB_MODE_INTERFACE,
                                  signal_name );
    if( !msg )
    {
        log_err("allocating signal %s failed", signal_name);
        goto EXIT;
    }

EXIT:
    return msg;
}

/**
 * Helper function for sending the different signals
 *
 * @param signal_name  the type of signal (normal, error, ...)
 * @param content      string which can be mode name, error, list of modes, ...
 *
 * @return 0 on success, 1 on failure
 */
static int
umdbus_send_signal_ex(const char *signal_name, const char *content)
{
    LOG_REGISTER_CONTEXT;

    int result = 1;
    DBusMessage* msg = 0;

    /* Assume NULL content equals no value / empty list, and that skipping
     * signal broadcast is never preferable over sending empty string. */
    if( !content )
        content = "";

    log_debug("broadcast signal %s(%s)", signal_name, content);

    if( !(msg = umdbus_new_signal(signal_name)) )
        goto EXIT;

    // append arguments onto signal
    if( !dbus_message_append_args(msg,
                                  DBUS_TYPE_STRING, &content,
                                  DBUS_TYPE_INVALID) )
    {
        log_err("appending arguments to signal %s failed", signal_name);
        goto EXIT;
    }

    // send the message on the correct bus
    if( !dbus_connection_send(umdbus_connection, msg, 0) )
    {
        log_err("sending signal %s failed", signal_name);
        goto EXIT;
    }
    result = 0;

EXIT:
    // free the message
    if(msg != 0)
        dbus_message_unref(msg);

    return result;
}

/** Send legacy usb_moded state_or_event signal
 *
 * The legacy USB_MODE_SIGNAL_NAME signal is used for
 * both mode changes and stateless events.
 *
 * @param state_ind mode name or event name
 */
static void umdbus_send_legacy_signal(const char *state_ind)
{
    LOG_REGISTER_CONTEXT;

    umdbus_send_signal_ex(USB_MODE_SIGNAL_NAME, state_ind);
}

/** Send usb_moded current state signal
 *
 * @param state_ind mode name
 */
void umdbus_send_current_state_signal(const char *state_ind)
{
    LOG_REGISTER_CONTEXT;

    umdbus_send_signal_ex(USB_MODE_CURRENT_STATE_SIGNAL_NAME,
                          state_ind);
    umdbus_send_legacy_signal(state_ind);
}

/** Append string key, variant value dict entry to dbus iterator
 *
 * @param iter   Iterator to append data to
 * @param key    Entry name string
 * @param type   Entry value data tupe
 * @param val    Pointer to basic data (as void pointer)
 *
 * @return true on success, false on failure
 */
static bool
umdbus_append_basic_entry(DBusMessageIter *iter, const char *key,
                           int type, const void *val)
{
    LOG_REGISTER_CONTEXT;

    /* Signature must be provided for variant containers */
    const char *signature = 0;
    switch( type ) {
    case DBUS_TYPE_INT32:  signature = DBUS_TYPE_INT32_AS_STRING;  break;
    case DBUS_TYPE_STRING: signature = DBUS_TYPE_STRING_AS_STRING; break;
    default: break;
    }
    if( !signature ) {
        log_err("unhandled D-Bus type: %d", type);
        goto bailout_message;
    }

    DBusMessageIter entry, variant;

    if( !dbus_message_iter_open_container(iter, DBUS_TYPE_DICT_ENTRY,
                                          0, &entry) )
        goto bailout_message;

    if( !dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key) )
        goto bailout_entry;

    if( !dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
                                          signature, &variant) )
        goto bailout_entry;

    if( !dbus_message_iter_append_basic(&variant, type, val) )
        goto bailout_variant;

    if( !dbus_message_iter_close_container(&entry, &variant) )
        goto bailout_variant;

    if( !dbus_message_iter_close_container(iter, &entry) )
        goto bailout_entry;

    return true;

bailout_variant:
    dbus_message_iter_abandon_container(&entry, &variant);

bailout_entry:
    dbus_message_iter_abandon_container(iter, &entry);

bailout_message:
    return false;
}

/** Append string key, variant:int32 value dict entry to dbus iterator
 *
 * @param iter   Iterator to append data to
 * @param key    Entry name string
 * @param val    Entry value
 *
 * @return true on success, false on failure
 */
static bool
umdbus_append_int32_entry(DBusMessageIter *iter, const char *key, int val)
{
    LOG_REGISTER_CONTEXT;

    dbus_int32_t arg = val;
    return umdbus_append_basic_entry(iter, key, DBUS_TYPE_INT32, &arg);
}

/** Append string key, variant:string value dict entry to dbus iterator
 *
 * @param iter   Iterator to append data to
 * @param key    Entry name string
 * @param val    Entry value
 *
 * @return true on success, false on failure
 */
static bool
umdbus_append_string_entry(DBusMessageIter *iter, const char *key,
                            const char *val)
{
    LOG_REGISTER_CONTEXT;

    if( !val )
        val = "";
    return umdbus_append_basic_entry(iter, key, DBUS_TYPE_STRING, &val);
}

/** Append dynamic mode configuration to dbus message
 *
 * @param msg         D-Bus message object
 * @param mode_name   Name of the mode to use
 *
 * @return true on success, false on failure
 */
static bool
umdbus_append_mode_details(DBusMessage *msg, const char *mode_name)
{
    LOG_REGISTER_CONTEXT;

    const modedata_t *data = usbmoded_get_modedata(mode_name);

    DBusMessageIter body, dict;

    dbus_message_iter_init_append(msg, &body);

    if( !dbus_message_iter_open_container(&body,
                                          DBUS_TYPE_ARRAY,
                                          DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                                          DBUS_TYPE_STRING_AS_STRING
                                          DBUS_TYPE_VARIANT_AS_STRING
                                          DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
                                          &dict) )
        goto bailout_message;

    /* Note: mode_name is special case: It needs to be valid even
     *       if the mode does not have dynamic configuration.
     */
    if( !umdbus_append_string_entry(&dict, "mode_name", mode_name) )
        goto bailout_dict;

    /* For the rest of the mode attrs we use fallback data if there
     * is no dynamic config / dynamic config does not define some value.
     */

#define ADD_STR(name) \
     if( !umdbus_append_string_entry(&dict, #name, data ? data->name : 0) )\
             goto bailout_dict;
#define ADD_INT(name) \
     if( !umdbus_append_int32_entry(&dict, #name, data ? data->name : 0) )\
             goto bailout_dict;

    /* Attributes that we presume to be needed */
    ADD_INT(appsync);
    ADD_INT(network);
    ADD_STR(network_interface);
    ADD_INT(nat);
    ADD_INT(dhcp_server);
#ifdef CONNMAN
    ADD_STR(connman_tethering);
#endif

    /* Attributes that are not exposed for now */
#if 0
    ADD_INT(mass_storage);
    ADD_STR(mode_module);
    ADD_STR(sysfs_path);
    ADD_STR(sysfs_value);
    ADD_STR(sysfs_reset_value);
    ADD_STR(android_extra_sysfs_path);
    ADD_STR(android_extra_sysfs_value);
    ADD_STR(android_extra_sysfs_path2);
    ADD_STR(android_extra_sysfs_value2);
    ADD_STR(android_extra_sysfs_path3);
    ADD_STR(android_extra_sysfs_value3);
    ADD_STR(android_extra_sysfs_path4);
    ADD_STR(android_extra_sysfs_value4);
    ADD_STR(idProduct);
    ADD_STR(idVendorOverride);
#endif

#undef ADD_STR
#undef ADD_INT

    if( !dbus_message_iter_close_container(&body, &dict) )
        goto bailout_dict;

    return true;

bailout_dict:
    dbus_message_iter_abandon_container(&body, &dict);

bailout_message:
    return false;
}

/** Send usb_moded target state configuration signal
 *
 * @param mode_name mode name
 */
static void
umdbus_send_mode_details_signal(const char *mode_name)
{
    DBusMessage* msg = 0;

    if( !(msg = umdbus_new_signal(USB_MODE_TARGET_CONFIG_SIGNAL_NAME)) )
        goto EXIT;

    if( !umdbus_append_mode_details(msg, mode_name) )
        goto EXIT;

    dbus_connection_send(umdbus_connection, msg, 0);

EXIT:
    if(msg != 0)
        dbus_message_unref(msg);
}

/** Send usb_moded target state signal
 *
 * @param state_ind mode name
 */
void umdbus_send_target_state_signal(const char *state_ind)
{
    LOG_REGISTER_CONTEXT;

    /* Send target mode details before claiming intent to
     * do mode transition. This way the clients tracking
     * configuration changes can assume they have valid
     * details immediately when transition begins.
     *
     * If clients for any reason need to pay closer attention
     * to signal timing, the mode_name contained in this broadcast
     * can be checked against current / target mode.
     */
    umdbus_send_mode_details_signal(state_ind);

    umdbus_send_signal_ex(USB_MODE_TARGET_STATE_SIGNAL_NAME,
                          state_ind);
}

/** Send usb_moded event signal
 *
 * @param state_ind event name
 */
void umdbus_send_event_signal(const char *state_ind)
{
    LOG_REGISTER_CONTEXT;

    umdbus_send_signal_ex(USB_MODE_EVENT_SIGNAL_NAME,
                          state_ind);
    umdbus_send_legacy_signal(state_ind);
}

/**
 * Send regular usb_moded error signal
 *
 * @return 0 on success, 1 on failure
 * @param error the error to be signalled
 *
*/
int umdbus_send_error_signal(const char *error)
{
    LOG_REGISTER_CONTEXT;

    return umdbus_send_signal_ex(USB_MODE_ERROR_SIGNAL_NAME, error);
}

/**
 * Send regular usb_moded mode list signal
 *
 * @return 0 on success, 1 on failure
 * @param supported_modes list of supported modes
 *
*/
int umdbus_send_supported_modes_signal(const char *supported_modes)
{
    LOG_REGISTER_CONTEXT;

    return umdbus_send_signal_ex(USB_MODE_SUPPORTED_MODES_SIGNAL_NAME, supported_modes);
}

/**
 * Send regular usb_moded mode list signal
 *
 * @return 0 on success, 1 on failure
 * @param available_modes list of available modes
 *
*/
int umdbus_send_available_modes_signal(const char *available_modes)
{
    LOG_REGISTER_CONTEXT;

    return umdbus_send_signal_ex(USB_MODE_AVAILABLE_MODES_SIGNAL_NAME, available_modes);
}

/**
 * Send regular usb_moded hidden mode list signal
 *
 * @return 0 on success, 1 on failure
 * @param hidden_modes list of supported modes
 *
*/
int umdbus_send_hidden_modes_signal(const char *hidden_modes)
{
    LOG_REGISTER_CONTEXT;

    return umdbus_send_signal_ex(USB_MODE_HIDDEN_MODES_SIGNAL_NAME, hidden_modes);
}

/**
 * Send regular usb_moded whitelisted mode list signal
 *
 * @return 0 on success, 1 on failure
 * @param whitelist list of allowed modes
 */
int umdbus_send_whitelisted_modes_signal(const char *whitelist)
{
    LOG_REGISTER_CONTEXT;

    return umdbus_send_signal_ex(USB_MODE_WHITELISTED_MODES_SIGNAL_NAME, whitelist);
}

/** Async reply handler for umdbus_get_name_owner_async()
 *
 * @param pc    Pending call object pointer
 * @param aptr  Notify function to call (as a void pointer)
 */
static void umdbus_get_name_owner_cb(DBusPendingCall *pc, void *aptr)
{
    LOG_REGISTER_CONTEXT;

    usb_moded_get_name_owner_fn cb = aptr;

    DBusMessage *rsp = 0;
    const char  *dta = 0;
    DBusError    err = DBUS_ERROR_INIT;

    if( !(rsp = dbus_pending_call_steal_reply(pc)) ) {
        log_err("did not get reply");
        goto EXIT;
    }

    if( dbus_set_error_from_message(&err, rsp) )
    {
        if( strcmp(err.name, DBUS_ERROR_NAME_HAS_NO_OWNER) )
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

EXIT:
    /* Allways call the notification function. Equate any error
     * situations with "service does not have an owner". */
    cb(dta ?: "");

    if( rsp ) dbus_message_unref(rsp);

    dbus_error_free(&err);
}

/** Helper function for making async dbus name owner queries
 *
 * @param name  D-Bus name to query
 * @param cb    Function to call when async reply is received
 * @param ppc   Where to store pending call object, or NULL
 *
 * @return TRUE if method call was sent, FALSE otherwise
 */
gboolean umdbus_get_name_owner_async(const char *name,
                                        usb_moded_get_name_owner_fn cb,
                                        DBusPendingCall **ppc)
{
    LOG_REGISTER_CONTEXT;

    gboolean         ack = FALSE;
    DBusMessage     *req = 0;
    DBusPendingCall *pc  = 0;

    if(!umdbus_connection)
        goto EXIT;

    req = dbus_message_new_method_call(DBUS_INTERFACE_DBUS,
                                       DBUS_PATH_DBUS,
                                       DBUS_INTERFACE_DBUS,
                                       DBUS_GET_NAME_OWNER_REQ);
    if( !req ) {
        log_err("could not create method call message");
        goto EXIT;
    }

    if( !dbus_message_append_args(req,
                                  DBUS_TYPE_STRING, &name,
                                  DBUS_TYPE_INVALID) ) {
        log_err("could not add method call parameters");
        goto EXIT;
    }

    if( !dbus_connection_send_with_reply(umdbus_connection, req, &pc, -1) )
        goto EXIT;

    if( !pc )
        goto EXIT;

    if( !dbus_pending_call_set_notify(pc, umdbus_get_name_owner_cb, cb, 0) )
        goto EXIT;

    ack = TRUE;

    if( ppc )
        *ppc = pc, pc = 0;

EXIT:

    if( pc  ) dbus_pending_call_unref(pc);
    if( req ) dbus_message_unref(req);

    return ack;
}

/**
 * Get uid of sender from D-Bus. This makes a synchronous D-Bus call
 *
 * @param name   Name of sender from DBusMessage
 * @return Uid of the sender
 */
uid_t umdbus_get_sender_uid(const char *name)
{
    LOG_REGISTER_CONTEXT;

    pid_t        pid = (pid_t)-1;
    uid_t        uid = (uid_t)-1;
    DBusMessage *req = 0;
    DBusMessage *rsp = 0;
    DBusError    err = DBUS_ERROR_INIT;
    char         path[256];
    struct stat  st;

    if(!umdbus_connection)
        goto EXIT;

    req = dbus_message_new_method_call(DBUS_INTERFACE_DBUS,
                                       DBUS_PATH_DBUS,
                                       DBUS_INTERFACE_DBUS,
                                       DBUS_GET_CONNECTION_PID_REQ);
    if( !req ) {
        log_err("could not create method call message");
        goto EXIT;
    }

    if( !dbus_message_append_args(req,
                                  DBUS_TYPE_STRING, &name,
                                  DBUS_TYPE_INVALID) ) {
        log_err("could not add method call parameters");
        goto EXIT;
    }

    /* Synchronous D-Bus call */
    rsp = dbus_connection_send_with_reply_and_block(umdbus_connection, req, -1, &err);

    if( rsp == NULL && dbus_error_is_set(&err) ) {
        log_err("could not get sender pid for %s: %s: %s", name, err.name, err.message);
        goto EXIT;
    }

    if( !dbus_message_get_args(rsp, &err,
                               DBUS_TYPE_UINT32, &pid,
                               DBUS_TYPE_INVALID) ) {
        log_err("parse error: %s: %s", err.name, err.message);
        goto EXIT;
    }

    snprintf(path, sizeof path, "/proc/%d", (int)pid);
    memset(&st, 0, sizeof st);
    if( stat(path, &st) == 0 ) {
        uid = st.st_uid;
    }

EXIT:

    if( req ) dbus_message_unref(req);
    if( rsp ) dbus_message_unref(rsp);

    dbus_error_free(&err);

    return uid;
}
