/**
  @file usb_moded-ssu.c

  Copyright (C) 2016 Jolla. All rights reserved.

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

#include <glib.h>

#include <dbus/dbus.h>

#include "usb_moded-ssu.h"
#include "usb_moded-log.h"

/** SSU D-Bus service name */
#define SSU_DBUS_SERVICE          "org.nemo.ssu"

/** Default SSU D-Bus object path */
#define SSU_DBUS_OBJECT           "/org/nemo/ssu"

/** SSU D-Bus interface */
#define SSU_DBUS_INTERFACE        "org.nemo.ssu"

/** SSU displayName D-Bus method call */
#define SSU_DBUS_GET_DISPLAY_NAME "displayName"

/** SsuDisplayType enum adapted from ssu c++ headers */
typedef enum
{
	/** Manufacturer, like ACME Corp.
	 *  Board mappings key "deviceManufacturer" */
	SsuDeviceManufacturer = 0,

	/** Marketed device name, like Pogoblaster 3000.
	 *  Board mappings key "prettyModel" */
	SsuDeviceModel        = 1,

	/** Type designation, like NCC-1701.
	 *  Beard mappings key "deviceDesignation" */
	SsuDeviceDesignation  = 2,
} SsuDisplayType;

/** Wrapper for making synchronous org.nemo.ssu.displayName() method calls
 *
 * Caller must release non-null return value with g_free().
 *
 * @param type_id display name type to query
 *
 * @return human readable string, or NULL in case of errors
 */
static gchar *
usb_moded_get_ssu_display_name(SsuDisplayType type_id)
{
	gchar          *res = 0;
	DBusConnection *con = 0;
	DBusMessage    *req = 0;
	DBusMessage    *rsp = 0;
	dbus_int32_t    arg = type_id;
	const char     *val = 0;
	DBusError       err = DBUS_ERROR_INIT;

	if( !(con = dbus_bus_get(DBUS_BUS_SYSTEM, &err)) ) {
		log_err("could not connect to system bus: %s: %s",
			err.name, err.message);
		goto EXIT;
	}

	req = dbus_message_new_method_call(SSU_DBUS_SERVICE,
					   SSU_DBUS_OBJECT,
					   SSU_DBUS_INTERFACE,
					   SSU_DBUS_GET_DISPLAY_NAME);

	if( !req ) {
		log_err("could not create method call message");
		goto EXIT;
	}

	if( !dbus_message_append_args(req,
				      DBUS_TYPE_INT32, &arg,
				      DBUS_TYPE_INVALID) ) {
		log_err("could not add method call parameters");
		goto EXIT;
	}

	rsp = dbus_connection_send_with_reply_and_block(con, req, -1, &err);
	if( !rsp ) {
		/* Be less verbose with failures that are expected
		 * if ssu service is not installed on the device. */
		if( !strcmp(err.name, DBUS_ERROR_SERVICE_UNKNOWN) ) {
			log_debug("%s D-Bus service is not available",
				  SSU_DBUS_SERVICE);
		}
		else {
			log_err("did not get reply: %s: %s",
				err.name, err.message);
		}
		goto EXIT;
	}

	if( dbus_set_error_from_message(&err, rsp) ) {
		log_err("got error reply: %s: %s", err.name, err.message);
		goto EXIT;
	}

	if( !dbus_message_get_args(rsp, &err,
				   DBUS_TYPE_STRING, &val,
				   DBUS_TYPE_INVALID) ) {
		log_err("could not parse reply: %s: %s",
			err.name, err.message);
		goto EXIT;
	}

	res = g_strdup(val);

EXIT:

	dbus_error_free(&err);

	if( rsp )
		dbus_message_unref(rsp);
	if( req )
		dbus_message_unref(req);
	if( con )
		dbus_connection_unref(con);

	log_debug("ssu displayName(%d) -> %s", type_id, res ?: "N/A");

	return res;
}

/** Query device manufacturer name from the SSU D-Bus service
 *
 * Caller must release non-null return value with g_free().
 *
 * @return human readable string, or NULL in case of errors
 */
gchar *
ssu_get_manufacturer_name(void)
{
	return usb_moded_get_ssu_display_name(SsuDeviceManufacturer);
}

/** Query device model name from the SSU D-Bus service
 *
 * Caller must release non-null return value with g_free().
 *
 * @return human readable string, or NULL in case of errors
 */
gchar *
ssu_get_product_name(void)
{
	return usb_moded_get_ssu_display_name(SsuDeviceModel);
}
