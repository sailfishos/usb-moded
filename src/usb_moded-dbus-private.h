/**
 * @file usb_moded-dbus-private.h
 *
 * Copyright (c) 2010 Nokia Corporation. All rights reserved.
 * Copyright (c) 2013 - 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author Vesa Halttunen <vesa.halttunen@jollamobile.com>
 * @author Martin Jones <martin.jones@jollamobile.com>
 * @author Simo Piiroinen <simo.piiroinen@jollamobile.com>
 * @author Andrew den Exter <andrew.den.exter@jolla.com>
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

# include <stdbool.h>

# include "usb_moded-dbus.h" // NOTRIM

# include <dbus/dbus.h>
# include <glib.h>

/* ========================================================================= *
 * Constants
 * ========================================================================= */

/** Logical name for org.freedesktop.DBus.GetNameOwner method */
# define DBUS_GET_NAME_OWNER_REQ         "GetNameOwner"

/** Logical name for org.freedesktop.DBus.NameOwnerChanged signal */
# define DBUS_NAME_OWNER_CHANGED_SIG     "NameOwnerChanged"

/** Logical name for org.freedesktop.DBus.GetNameOwner method */
# define DBUS_GET_CONNECTION_PID_REQ     "GetConnectionUnixProcessID"

/* ========================================================================= *
 * Types
 * ========================================================================= */

/* Callback function type used with umdbus_get_name_owner_async() */
typedef void (*usb_moded_get_name_owner_fn)(const char *owner);

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * UMDBUS
 * ------------------------------------------------------------------------- */

void            umdbus_send_config_signal           (const char *section, const char *key, const char *value);
DBusConnection *umdbus_get_connection               (void);
gboolean        umdbus_init_connection              (void);
gboolean        umdbus_init_service                 (void);
void            umdbus_cleanup                      (void);
void            umdbus_send_current_state_signal    (const char *state_ind);
void            umdbus_send_target_state_signal     (const char *state_ind);
void            umdbus_send_event_signal            (const char *state_ind);
int             umdbus_send_error_signal            (const char *error);
int             umdbus_send_supported_modes_signal  (const char *supported_modes);
int             umdbus_send_available_modes_signal  (const char *available_modes);
int             umdbus_send_hidden_modes_signal     (const char *hidden_modes);
int             umdbus_send_whitelisted_modes_signal(const char *whitelist);
gboolean        umdbus_get_name_owner_async         (const char *name, usb_moded_get_name_owner_fn cb, DBusPendingCall **ppc);
const char     *umdbus_arg_type_repr                (int type);
const char     *umdbus_arg_type_signature           (int type);
const char     *umdbus_msg_type_repr                (int type);
bool            umdbus_parser_init                  (DBusMessageIter *iter, DBusMessage *msg);
int             umdbus_parser_at_type               (DBusMessageIter *iter);
bool            umdbus_parser_at_end                (DBusMessageIter *iter);
bool            umdbus_parser_require_type          (DBusMessageIter *iter, int type, bool strict);
bool            umdbus_parser_get_bool              (DBusMessageIter *iter, bool *pval);
bool            umdbus_parser_get_int               (DBusMessageIter *iter, int *pval);
bool            umdbus_parser_get_string            (DBusMessageIter *iter, const char **pval);
bool            umdbus_parser_get_object            (DBusMessageIter *iter, const char **pval);
bool            umdbus_parser_get_variant           (DBusMessageIter *iter, DBusMessageIter *val);
bool            umdbus_parser_get_array             (DBusMessageIter *iter, DBusMessageIter *val);
bool            umdbus_parser_get_struct            (DBusMessageIter *iter, DBusMessageIter *val);
bool            umdbus_parser_get_entry             (DBusMessageIter *iter, DBusMessageIter *val);
bool            umdbus_append_init                  (DBusMessageIter *iter, DBusMessage *msg);
bool            umdbus_open_container               (DBusMessageIter *iter, DBusMessageIter *sub, int type, const char *sign);
bool            umdbus_close_container              (DBusMessageIter *iter, DBusMessageIter *sub, bool success);
bool            umdbus_append_basic_value           (DBusMessageIter *iter, int type, const DBusBasicValue *val);
bool            umdbus_append_basic_variant         (DBusMessageIter *iter, int type, const DBusBasicValue *val);
bool            umdbus_append_bool                  (DBusMessageIter *iter, bool val);
bool            umdbus_append_int                   (DBusMessageIter *iter, int val);
bool            umdbus_append_string                (DBusMessageIter *iter, const char *val);
bool            umdbus_append_bool_variant          (DBusMessageIter *iter, bool val);
bool            umdbus_append_int_variant           (DBusMessageIter *iter, int val);
bool            umdbus_append_string_variant        (DBusMessageIter *iter, const char *val);
bool            umdbus_append_args_va               (DBusMessageIter *iter, int type, va_list va);
bool            umdbus_append_args                  (DBusMessageIter *iter, int arg_type, ...);
DBusMessage    *umdbus_blocking_call                (DBusConnection *con, const char *dst, const char *obj, const char *iface, const char *meth, DBusError *err, int arg_type, ...);
bool            umdbus_parse_reply                  (DBusMessage *rsp, int arg_type, ...);

#endif /* USB_MODED_DBUS_PRIVATE_H_ */
