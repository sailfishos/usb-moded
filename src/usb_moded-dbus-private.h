/*
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Copyright (C) 2013-2018 Jolla Ltd.
 *
 * Author: Philippe De Swert <philippe.de-swert@nokia.com>
 * Author: Philippe De Swert <philippe.deswert@jollamobile.com>
 * Author: Vesa Halttunen <vesa.halttunen@jollamobile.com>
 * Author: Martin Jones <martin.jones@jollamobile.com>
 * Author: Simo Piiroinen <simo.piiroinen@jollamobile.com>
 * Author: Andrew den Exter <andrew.den.exter@jolla.com>
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

#ifndef  USB_MODED_DBUS_PRIVATE_H_
# define USB_MODED_DBUS_PRIVATE_H_

# include <dbus/dbus.h>

/* ========================================================================= *
 * Constants
 * ========================================================================= */

/** Logical name for org.freedesktop.DBus.GetNameOwner method */
# define DBUS_GET_NAME_OWNER_REQ         "GetNameOwner"

/** Logical name for org.freedesktop.DBus.NameOwnerChanged signal */
# define DBUS_NAME_OWNER_CHANGED_SIG     "NameOwnerChanged"

/* ========================================================================= *
 * Types
 * ========================================================================= */

/* Callback function type used with umdbus_get_name_owner_async() */
typedef void (*usb_moded_get_name_owner_fn)(const char *owner);

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- umdbus -- */

DBusConnection *umdbus_get_connection               (void);
gboolean        umdbus_init_connection              (void);
gboolean        umdbus_init_service                 (void);
void            umdbus_cleanup                      (void);
int             umdbus_send_state_signal            (const char *state_ind);
int             umdbus_send_error_signal            (const char *error);
int             umdbus_send_supported_modes_signal  (const char *supported_modes);
int             umdbus_send_available_modes_signal  (const char *available_modes);
int             umdbus_send_hidden_modes_signal     (const char *hidden_modes);
int             umdbus_send_whitelisted_modes_signal(const char *whitelist);
gboolean        umdbus_get_name_owner_async         (const char *name, usb_moded_get_name_owner_fn cb, DBusPendingCall **ppc);

#endif /* USB_MODED_DBUS_PRIVATE_H_ */
