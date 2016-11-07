/*
  Copyright (C) 2010 Nokia Corporation. All rights reserved.
  Copyright (C) 2013-2016 Jolla Ltd.

  Author: Philippe De Swert <philippe.de-swert@nokia.com>
  Author: Philippe De Swert <philippe.deswert@jollamobile.com>
  Author: Vesa Halttunen <vesa.halttunen@jollamobile.com>
  Author: Martin Jones <martin.jones@jollamobile.com>
  Author: Simo Piiroinen <simo.piiroinen@jollamobile.com>

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

/** Logical name for org.freedesktop.DBus.GetNameOwner method */
#define DBUS_GET_NAME_OWNER_REQ         "GetNameOwner"

/** Logical name for org.freedesktop.DBus.NameOwnerChanged signal */
#define DBUS_NAME_OWNER_CHANGED_SIG     "NameOwnerChanged"

/* Connect to D-Bus System Bus */
gboolean usb_moded_dbus_init_connection(void);

/* Claim D-Bus Service Name */
gboolean usb_moded_dbus_init_service(void);

/* Get current SystemBus connection */
DBusConnection *usb_moded_dbus_get_connection(void);

/* cleanup usb on exit */
void usb_moded_dbus_cleanup(void);

/* send signal on system bus */
int usb_moded_send_signal(const char *state_ind);

/* send error signal system bus */
int usb_moded_send_error_signal(const char *error);

/* send supported modes signal system bus */
int usb_moded_send_supported_modes_signal(const char *supported_modes);

/* send hidden modes signal system bus */
int usb_moded_send_hidden_modes_signal(const char *hidden_modes);

/* Callback function type used with usb_moded_get_name_owner_async() */
typedef void (*usb_moded_get_name_owner_fn)(const char *owner);

/* Asynchronous GetNameOwner query */
gboolean usb_moded_get_name_owner_async(const char *name,
                                        usb_moded_get_name_owner_fn cb,
                                        DBusPendingCall **ppc);
