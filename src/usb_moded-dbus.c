/**
  @file	usb_moded-dbus.c

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

#include <stdio.h>
#include <string.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "usb_moded-dbus.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded.h"
#include "usb_moded-modes.h"
#include "usb_moded-modesetting.h"
#include "usb_moded-config.h"
#include "usb_moded-config-private.h"
#include "usb_moded-network.h"
#include "usb_moded-log.h"

#define INIT_DONE_INTERFACE "com.nokia.startup.signal"
#define INIT_DONE_SIGNAL    "init_done"
#define INIT_DONE_MATCH     "type='signal',interface='"INIT_DONE_INTERFACE"',member='"INIT_DONE_SIGNAL"'"

static DBusConnection *dbus_connection_sys = NULL;
static gboolean        have_service_name   = FALSE;

extern gboolean rescue_mode;

/**
 * Issues "sig_usb_config_ind" signal.
*/
static void usb_moded_send_config_signal(const char *section, const char *key, const char *value)
{
  log_debug("broadcast signal %s(%s, %s, %s)\n", USB_MODE_CONFIG_SIGNAL_NAME, section, key, value);

  if( !have_service_name )
  {
	log_err("config notification without service: [%s] %s=%s",
		section, key, value);
  }
  else if (dbus_connection_sys)
  {
	DBusMessage* msg = dbus_message_new_signal(USB_MODE_OBJECT, USB_MODE_INTERFACE, USB_MODE_CONFIG_SIGNAL_NAME);
	if (msg) {
		dbus_message_append_args(msg, DBUS_TYPE_STRING, &section,
		                              DBUS_TYPE_STRING, &key,
		                              DBUS_TYPE_STRING, &value,
		                              DBUS_TYPE_INVALID);
		dbus_connection_send(dbus_connection_sys, msg, NULL);
		dbus_message_unref(msg);
	}
  }
}

/** Introspect xml format string for parents of USB_MODE_OBJECT */
static const char intospect_template[] =
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
static const char introspect_usb_moded[] =
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
"    <method name=\"" USB_MODE_WHITELISTED_MODES_GET "\">\n"
"      <arg name=\"modes\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"   
"    <method name=\"" USB_MODE_WHITELISTED_MODES_SET "\">"
"      <arg name=\"modes\" type=\"s\" direction=\"in\"/>"
"    </method>"
"    <method name=\"" USB_MODE_WHITELISTED_SET "\">"
"      <arg name=\"mode\" type=\"s\" direction=\"in\"/>"
"      <arg name=\"whitelisted\" type=\"b\" direction=\"in\"/>"
"     </method>"
"    <method name=\"" USB_MODE_RESCUE_OFF "\"/>\n"
"    <signal name=\"" USB_MODE_SIGNAL_NAME "\">\n"
"      <arg name=\"mode\" type=\"s\"/>\n"
"    </signal>\n"
"    <signal name=\"" USB_MODE_ERROR_SIGNAL_NAME "\">\n"
"      <arg name=\"error\" type=\"s\"/>\n"
"    </signal>\n"
"    <signal name=\"" USB_MODE_SUPPORTED_MODES_SIGNAL_NAME "\">\n"
"      <arg name=\"modes\" type=\"s\"/>\n"
"    </signal>\n"
"    <signal name=\"" USB_MODE_AVAILABLE_MODES_SIGNAL_NAME "\">\n"
"      <arg name=\"modes\" type=\"s\">\n"
"    </signal>\n"
"    <signal name=\"" USB_MODE_WHITELISTED_MODES_SIGNAL_NAME "\">\n"
"      <arg name=\"modes\" type=\"s\">\n"
"    </signal>\n"
"    <signal name=\"" USB_MODE_CONFIG_SIGNAL_NAME "\">\n"
"      <arg name=\"section\" type=\"s\"/>\n"
"      <arg name=\"key\" type=\"s\"/>\n"
"      <arg name=\"value\" type=\"s\"/>\n"
"    </signal>\n"
"  </interface>\n"
"</node>\n";

static DBusHandlerResult msg_handler(DBusConnection *const connection, DBusMessage *const msg, gpointer const user_data)
{
  DBusHandlerResult   status    = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  DBusMessage        *reply     = 0;
  const char         *interface = dbus_message_get_interface(msg);
  const char         *member    = dbus_message_get_member(msg);
  const char         *object    = dbus_message_get_path(msg);
  int                 type      = dbus_message_get_type(msg);

  (void)user_data;

  if(!interface || !member || !object) goto EXIT;

  if( type == DBUS_MESSAGE_TYPE_SIGNAL )
  {
	if( !strcmp(interface, INIT_DONE_INTERFACE) && !strcmp(member, INIT_DONE_SIGNAL) ) {
		/* Auto-disable rescue mode when bootup is finished */
		if( rescue_mode ) {
			rescue_mode = FALSE;
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
		const char *mode = get_usb_mode();

		/* To the outside we want to keep CHARGING and CHARGING_FALLBACK the same */
		if(!strcmp(MODE_CHARGING_FALLBACK, mode))
			mode = MODE_CHARGING;
		if((reply = dbus_message_new_method_return(msg)))
			dbus_message_append_args (reply, DBUS_TYPE_STRING, &mode, DBUS_TYPE_INVALID);
	}
	else if(!strcmp(member, USB_MODE_STATE_SET))
	{
		char *use = 0;
		DBusError   err = DBUS_ERROR_INIT;

		if(!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &use, DBUS_TYPE_INVALID))
			reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, member);
		else
		{
				/* check if usb is connected, since it makes no sense to change mode if it isn't */
				if(!get_usb_connection_state())
				{
					log_warning("USB not connected, not changing mode!\n");
					goto error_reply;
				}
				/* check if the mode exists */
				if(valid_mode(use))
					goto error_reply;
				/* do not change mode if the mode requested is the one already set */
				if(strcmp(use, get_usb_mode()) != 0)
				{
					usb_moded_mode_cleanup(get_usb_module());
					set_usb_mode(use);
				}
				if((reply = dbus_message_new_method_return(msg)))
					dbus_message_append_args (reply, DBUS_TYPE_STRING, &use, DBUS_TYPE_INVALID);
				else
error_reply:
					reply = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, member);
		}
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
			int ret = set_mode_setting(config);
			if (ret == SET_CONFIG_UPDATED)
				usb_moded_send_config_signal(MODE_SETTING_ENTRY, MODE_SETTING_KEY, config);
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
			int ret = set_hide_mode_setting(config);
			if (ret == SET_CONFIG_UPDATED)
				usb_moded_send_config_signal(MODE_SETTING_ENTRY, MODE_HIDE_KEY, config);
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
			int ret = set_unhide_mode_setting(config);
			if (ret == SET_CONFIG_UPDATED)
				usb_moded_send_config_signal(MODE_SETTING_ENTRY, MODE_HIDE_KEY, config);
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
                char *config = get_hidden_modes();
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
			int ret = set_network_setting(config, setting);
			if (ret == SET_CONFIG_UPDATED)
				usb_moded_send_config_signal(NETWORK_ENTRY, config, setting);
			if (SET_CONFIG_OK(ret))
			{
				if((reply = dbus_message_new_method_return(msg)))
				dbus_message_append_args (reply, DBUS_TYPE_STRING, &config, DBUS_TYPE_STRING, &setting, DBUS_TYPE_INVALID);
				usb_network_update();
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
			setting = get_network_setting(config);
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
		char *config = get_mode_setting();

		if(!config)
		{
			/* Config is corrupted or we do not have a mode
			 * configured, fallback to undefined. */
			config = g_strdup(MODE_UNDEFINED);
		}

		if((reply = dbus_message_new_method_return(msg)))
			dbus_message_append_args (reply, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID);
		g_free(config);
	}
	else if(!strcmp(member, USB_MODE_LIST))
	{
		gchar *mode_list = get_mode_list(SUPPORTED_MODES_LIST);

                if((reply = dbus_message_new_method_return(msg)))
                        dbus_message_append_args (reply, DBUS_TYPE_STRING, (const char *) &mode_list, DBUS_TYPE_INVALID);
		g_free(mode_list);
	}
	else if(!strcmp(member, USB_MODE_AVAILABLE_MODES_GET))
	{
		gchar *mode_list = get_mode_list(AVAILABLE_MODES_LIST);

		if((reply = dbus_message_new_method_return(msg)))
			dbus_message_append_args (reply, DBUS_TYPE_STRING, (const char *) &mode_list, DBUS_TYPE_INVALID);
		g_free(mode_list);
	}
	else if(!strcmp(member, USB_MODE_RESCUE_OFF))
	{
		rescue_mode = FALSE;
		log_debug("Rescue mode off\n ");
		reply = dbus_message_new_method_return(msg);
	}
	else if(!strcmp(member, USB_MODE_WHITELISTED_MODES_GET))
	{
		gchar *mode_list = get_mode_whitelist();

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
			int ret = set_mode_whitelist(whitelist);
			if (ret == SET_CONFIG_UPDATED)
                                usb_moded_send_config_signal(MODE_SETTING_ENTRY, MODE_WHITELIST_KEY, whitelist);
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
			int ret = set_mode_in_whitelist(mode, enabled);
			if (ret == SET_CONFIG_UPDATED) 
			{
				char *whitelist = get_mode_whitelist(); 
				if (!whitelist)
					whitelist = g_strdup(MODE_UNDEFINED);
				usb_moded_send_config_signal(MODE_SETTING_ENTRY, MODE_WHITELIST_KEY, whitelist);
				g_free(whitelist);
			}
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
			xml = introspect_usb_moded;
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
			xml = tmp = g_strdup_printf(intospect_template,
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
			xml = tmp = g_strdup_printf(intospect_template,
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

DBusConnection *usb_moded_dbus_get_connection(void)
{
    DBusConnection *connection = 0;
    if( dbus_connection_sys )
	connection = dbus_connection_ref(dbus_connection_sys);
    else
	log_err("something asked for connection ref while unconnected");
    return connection;
}

/**
 * Establish D-Bus SystemBus connection
 *
 * @return TRUE when everything went ok
 */
gboolean usb_moded_dbus_init_connection(void)
{
  gboolean status = FALSE;
  DBusError error = DBUS_ERROR_INIT;

  /* connect to system bus */
  if ((dbus_connection_sys = dbus_bus_get(DBUS_BUS_SYSTEM, &error)) == NULL)
  {
	log_debug("Failed to open connection to system message bus; %s\n",  error.message);
        goto EXIT;
  }

  /* Initialise message handlers */
  if (!dbus_connection_add_filter(dbus_connection_sys, msg_handler, NULL, NULL))
	goto EXIT;

  /* Listen to init-done signals */
  dbus_bus_add_match(dbus_connection_sys, INIT_DONE_MATCH, 0);

  /* Connect D-Bus to the mainloop */
  dbus_connection_setup_with_g_main(dbus_connection_sys, NULL);

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
gboolean usb_moded_dbus_init_service(void)
{
  gboolean status = FALSE;
  DBusError error = DBUS_ERROR_INIT;
  int ret;

  if( !dbus_connection_sys ) {
	  goto EXIT;
  }

  /* Acquire D-Bus service */
  ret = dbus_bus_request_name(dbus_connection_sys, USB_MODE_SERVICE, DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
  if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
  {
	log_debug("failed claiming dbus name\n");
	if( dbus_error_is_set(&error) )
		log_debug("DBUS ERROR: %s, %s \n", error.name, error.message);
        goto EXIT;
  }
  log_debug("claimed name %s", USB_MODE_SERVICE);
  have_service_name = TRUE;
  /* everything went fine */
  status = TRUE;

EXIT:
  dbus_error_free(&error);
  return status;
}

/** Release "com.meego.usb_moded" D-Bus Service Name
 */
static void usb_moded_dbus_cleanup_service(void)
{
    if( !have_service_name )
        goto EXIT;

    have_service_name = FALSE;
    log_debug("release name %s", USB_MODE_SERVICE);

    if( dbus_connection_sys &&
        dbus_connection_get_is_connected(dbus_connection_sys) )
    {
        dbus_bus_release_name(dbus_connection_sys, USB_MODE_SERVICE, NULL);
    }

EXIT:
    return;
}

/**
 * Clean up the dbus connections on exit
 *
 */
void usb_moded_dbus_cleanup(void)
{
    /* clean up system bus connection */
    if (dbus_connection_sys != NULL)
    {
	usb_moded_dbus_cleanup_service();

	dbus_connection_remove_filter(dbus_connection_sys, msg_handler, NULL);

	dbus_connection_unref(dbus_connection_sys),
	    dbus_connection_sys = NULL;
    }
}

/**
 * Helper function for sending the different signals
 *
 * @return 0 on success, 1 on failure
 * @param signal_type the type of signal (normal, error, ...)
 * @@param content string which can be mode name, error, list of modes, ...
*/
static int usb_moded_dbus_signal(const char *signal_type, const char *content)
{
  int result = 1;
  DBusMessage* msg = 0;

  log_debug("broadcast signal %s(%s)\n", signal_type, content);

  if( !have_service_name )
  {
	log_err("sending signal without service: %s(%s)",
		signal_type, content);
	goto EXIT;
  }
  if(!dbus_connection_sys)
  {
	log_err("Dbus system connection broken!\n");
	goto EXIT;
  }
  // create a signal and check for errors
  msg = dbus_message_new_signal(USB_MODE_OBJECT, USB_MODE_INTERFACE, signal_type );
  if (NULL == msg)
  {
      log_debug("Message Null\n");
      goto EXIT;
   }

  // append arguments onto signal
  if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &content, DBUS_TYPE_INVALID))
  {
      log_debug("Appending arguments failed. Out Of Memory!\n");
      goto EXIT;
  }

  // send the message on the correct bus  and flush the connection
  if (!dbus_connection_send(dbus_connection_sys, msg, 0))
  {
	log_debug("Failed sending message. Out Of Memory!\n");
	goto EXIT;
  }
  result = 0;

EXIT:
  // free the message
  if(msg != 0)
	  dbus_message_unref(msg);

  return result;
}

/**
 * Send regular usb_moded state signal
 *
 * @return 0 on success, 1 on failure
 * @param state_ind the signal name
 *
*/
int usb_moded_send_signal(const char *state_ind)
{
  return(usb_moded_dbus_signal(USB_MODE_SIGNAL_NAME, state_ind));
}

/**
 * Send regular usb_moded error signal
 *
 * @return 0 on success, 1 on failure
 * @param error the error to be signalled
 *
*/
int usb_moded_send_error_signal(const char *error)
{
  return(usb_moded_dbus_signal(USB_MODE_ERROR_SIGNAL_NAME, error));
}

/**
 * Send regular usb_moded mode list signal
 *
 * @return 0 on success, 1 on failure
 * @param supported_modes list of supported modes
 *
*/
int usb_moded_send_supported_modes_signal(const char *supported_modes)
{
  return(usb_moded_dbus_signal(USB_MODE_SUPPORTED_MODES_SIGNAL_NAME, supported_modes));
}

/**
 * Send regular usb_moded mode list signal
 *
 * @return 0 on success, 1 on failure
 * @param available_modes list of available modes
 *
*/
int usb_moded_send_available_modes_signal(const char *available_modes)
{
  return(usb_moded_dbus_signal(USB_MODE_AVAILABLE_MODES_SIGNAL_NAME, available_modes));
}

/**
 * Send regular usb_moded hidden mode list signal
 *
 * @return 0 on success, 1 on failure
 * @param hidden_modes list of supported modes
 *
*/
int usb_moded_send_hidden_modes_signal(const char *hidden_modes)
{
  return(usb_moded_dbus_signal(USB_MODE_HIDDEN_MODES_SIGNAL_NAME, hidden_modes));
}

/**
 * Send regular usb_moded whitelisted mode list signal
 *
 * @return 0 on success, 1 on failure
 * @param whitelist list of allowed modes
 */
int usb_moded_send_whitelisted_modes_signal(const char *whitelist)
{
  return(usb_moded_dbus_signal(USB_MODE_WHITELISTED_MODES_SIGNAL_NAME, whitelist));
}

/** Async reply handler for usb_moded_get_name_owner_async()
 *
 * @param pc    Pending call object pointer
 * @param aptr  Notify function to call (as a void pointer)
 */
static void usb_moded_get_name_owner_cb(DBusPendingCall *pc, void *aptr)
{
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
gboolean usb_moded_get_name_owner_async(const char *name,
                                        usb_moded_get_name_owner_fn cb,
                                        DBusPendingCall **ppc)
{
    gboolean         ack = FALSE;
    DBusMessage     *req = 0;
    DBusPendingCall *pc  = 0;

    if(!dbus_connection_sys)
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

    if( !dbus_connection_send_with_reply(dbus_connection_sys, req, &pc, -1) )
        goto EXIT;

    if( !pc )
        goto EXIT;

    if( !dbus_pending_call_set_notify(pc, usb_moded_get_name_owner_cb, cb, 0) )
        goto EXIT;

    ack = TRUE;

    if( ppc )
        *ppc = pc, pc = 0;

EXIT:

    if( pc  ) dbus_pending_call_unref(pc);
    if( req ) dbus_message_unref(req);

    return ack;
}
