/**
 * @file usb_moded-dbus.c
 *
 * Copyright (c) 2010 Nokia Corporation. All rights reserved.
 * Copyright (c) 2012 - 2021 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Philippe De Swert <phdeswer@lumi.maa>
 * @author Philippe De Swert <philippedeswert@gmail.com>
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author Vesa Halttunen <vesa.halttunen@jollamobile.com>
 * @author Slava Monich <slava.monich@jolla.com>
 * @author Martin Jones <martin.jones@jollamobile.com>
 * @author Simo Piiroinen <simo.piiroinen@jollamobile.com>
 * @author Andrew den Exter <andrew.den.exter@jolla.com>
 * @author Andrew den Exter <andrew.den.exter@jollamobile.com>
 * @author Björn Bidar <bjorn.bidar@jolla.com>
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
#include "usb_moded-dbus.h"

#include "usb_moded.h"
#include "usb_moded-config-private.h"
#include "usb_moded-control.h"
#include "usb_moded-log.h"
#include "usb_moded-modes.h"
#include "usb_moded-network.h"

#include <sys/stat.h>

#include "../dbus-gmain/dbus-gmain.h"

#ifdef SAILFISH_ACCESS_CONTROL
# include <sailfishaccesscontrol.h>
#endif

/* ========================================================================= *
 * Constants
 * ========================================================================= */

#define INIT_DONE_OBJECT    "/com/nokia/startup/signal"
#define INIT_DONE_INTERFACE "com.nokia.startup.signal"
#define INIT_DONE_SIGNAL    "init_done"
#define INIT_DONE_MATCH     "type='signal',interface='"INIT_DONE_INTERFACE"',member='"INIT_DONE_SIGNAL"'"

# define PID_UNKNOWN ((pid_t)-1)

/* ========================================================================= *
 * Types
 * ========================================================================= */

typedef struct umdbus_context_t umdbus_context_t;

/** Introspect / handling details for method call / signal
 *
 * Use ADD_METHOD(), ADD_SIGNAL() and ADD_SENTINEL macros
 * for instantiating these structures.
 */
typedef struct  {
    /** Differentiate between method calls and signals */
    int          type;

    /** Member name, or NULL for sentinel */
    const char  *member;

    /** Handler callback, use NULL for Introspect only  */
    void       (*handler)(umdbus_context_t *);

    /** Argument info for generating introspect XML */
    const char  *args;
} member_info_t;

/** Define incoming method call handler + introspect data
 */
#define ADD_METHOD(NAME, FUNC, ARGS) {\
    .type    = DBUS_MESSAGE_TYPE_METHOD_CALL,\
    .member  = NAME,\
    .handler = FUNC,\
    .args    = ARGS,\
}

/** Define outgoing signal introspect data
 */
#define ADD_SIGNAL(NAME, ARGS) {\
    .type    = DBUS_MESSAGE_TYPE_SIGNAL,\
    .member  = NAME,\
    .handler = 0,\
    .args    = ARGS,\
}

/** Terminate member data array
 */
#define ADD_SENTINEL {\
    .type    = DBUS_MESSAGE_TYPE_INVALID,\
    .member  = 0,\
    .handler = 0,\
    .args    = 0,\
}

/** D-Bus interface details for message handling / introspecting
 */
typedef struct
{
    /** D-Bus interface name */
    const char          *interface;

    /** Array of interface members */
    const member_info_t *members;
} interface_info_t;

/** D-Bus object details for message handling / introspecting
 */
typedef struct
{
    /** D-Bus object path */
    const char              *object;

    /** Array of object interfaces */
    const interface_info_t **interfaces;
} object_info_t;

/** Context info for D-Bus message handling
 *
 * Filled in at message filter, passed to member callbacks.
 */
struct umdbus_context_t {
    /** Incoming message */
    DBusMessage            *msg;

    /** Message type */
    int                     type;

    /** D-Bus name of sender */
    const char             *sender;

    /** Message object path */
    const char             *object;

    /** Message interface name */
    const char             *interface;

    /** Message member name */
    const char             *member;

    /** Information about message object path */
    const object_info_t    *object_info;

    /** Information about message interface name */
    const interface_info_t *interface_info;

    /** Information about message member name */
    const member_info_t    *member_info;

    /** Reply message to send */
    DBusMessage            *rsp;
};

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * MEMBER_INFO
 * ------------------------------------------------------------------------- */

static void member_info_introspect(const member_info_t *self, FILE *file);

/* ------------------------------------------------------------------------- *
 * INTERFACE_INFO
 * ------------------------------------------------------------------------- */

static const member_info_t *interface_info_get_member(const interface_info_t *self, const char *member);
static void                 interface_info_introspect(const interface_info_t *self, FILE *file);

/* ------------------------------------------------------------------------- *
 * OBJECT_INFO
 * ------------------------------------------------------------------------- */

static const interface_info_t *object_info_get_interface     (const object_info_t *self, const char *interface);
static void                    object_info_introspect        (const object_info_t *self, FILE *file, const char *interface);
static char                   *object_info_get_introspect_xml(const object_info_t *self, const char *interface);

/* ------------------------------------------------------------------------- *
 * INTROSPECTABLE
 * ------------------------------------------------------------------------- */

static void introspectable_introspect_cb(umdbus_context_t *context);

/* ------------------------------------------------------------------------- *
 * USB_MODED
 * ------------------------------------------------------------------------- */

static void usb_moded_state_request_cb           (umdbus_context_t *context);
static void usb_moded_target_state_get_cb        (umdbus_context_t *context);
static void usb_moded_target_config_get_cb       (umdbus_context_t *context);
static void usb_moded_state_set_cb               (umdbus_context_t *context);
static void usb_moded_config_set_cb              (umdbus_context_t *context);
static void usb_moded_config_get_cb              (umdbus_context_t *context);
static void usb_moded_mode_list_cb               (umdbus_context_t *context);
static void usb_moded_available_modes_get_cb     (umdbus_context_t *context);
static void usb_moded_available_modes_for_user_cb(umdbus_context_t *context);
static void usb_moded_mode_hide_cb               (umdbus_context_t *context);
static void usb_moded_mode_unhide_cb             (umdbus_context_t *context);
static void usb_moded_hidden_get_cb              (umdbus_context_t *context);
static void usb_moded_whitelisted_modes_get_cb   (umdbus_context_t *context);
static void usb_moded_whitelisted_modes_set_cb   (umdbus_context_t *context);
static void usb_moded_user_config_clear_cb       (umdbus_context_t *context);
static void usb_moded_whitelisted_set_cb         (umdbus_context_t *context);
static void usb_moded_network_set_cb             (umdbus_context_t *context);
static void usb_moded_network_get_cb             (umdbus_context_t *context);
static void usb_moded_rescue_off_cb              (umdbus_context_t *context);

/* ------------------------------------------------------------------------- *
 * UMDBUS
 * ------------------------------------------------------------------------- */

static const object_info_t *umdbus_get_object_info              (const char *object);
void                        umdbus_dump_introspect_xml          (void);
void                        umdbus_dump_busconfig_xml           (void);
void                        umdbus_send_config_signal           (const char *section, const char *key, const char *value);
static DBusHandlerResult    umdbus_msg_handler                  (DBusConnection *const connection, DBusMessage *const msg, gpointer const user_data);
DBusConnection             *umdbus_get_connection               (void);
gboolean                    umdbus_init_connection              (void);
gboolean                    umdbus_init_service                 (void);
static void                 umdbus_cleanup_service              (void);
void                        umdbus_cleanup                      (void);
static DBusMessage         *umdbus_new_signal                   (const char *signal_name);
static int                  umdbus_send_signal_ex               (const char *signal_name, const char *content);
static void                 umdbus_send_legacy_signal           (const char *state_ind);
void                        umdbus_send_current_state_signal    (const char *state_ind);
static bool                 umdbus_append_basic_entry           (DBusMessageIter *iter, const char *key, int type, const void *val);
static bool                 umdbus_append_int32_entry           (DBusMessageIter *iter, const char *key, int val);
static bool                 umdbus_append_string_entry          (DBusMessageIter *iter, const char *key, const char *val);
static bool                 umdbus_append_mode_details          (DBusMessage *msg, const char *mode_name);
static void                 umdbus_send_mode_details_signal     (const char *mode_name);
void                        umdbus_send_target_state_signal     (const char *state_ind);
void                        umdbus_send_event_signal            (const char *state_ind);
int                         umdbus_send_error_signal            (const char *error);
int                         umdbus_send_supported_modes_signal  (const char *supported_modes);
int                         umdbus_send_available_modes_signal  (const char *available_modes);
int                         umdbus_send_hidden_modes_signal     (const char *hidden_modes);
int                         umdbus_send_whitelisted_modes_signal(const char *whitelist);
static void                 umdbus_get_name_owner_cb            (DBusPendingCall *pc, void *aptr);
gboolean                    umdbus_get_name_owner_async         (const char *name, usb_moded_get_name_owner_fn cb, DBusPendingCall **ppc);
static uid_t                umdbus_get_sender_uid               (const char *name);
const char                 *umdbus_arg_type_repr                (int type);
const char                 *umdbus_arg_type_signature           (int type);
const char                 *umdbus_msg_type_repr                (int type);
bool                        umdbus_parser_init                  (DBusMessageIter *iter, DBusMessage *msg);
int                         umdbus_parser_at_type               (DBusMessageIter *iter);
bool                        umdbus_parser_at_end                (DBusMessageIter *iter);
bool                        umdbus_parser_require_type          (DBusMessageIter *iter, int type, bool strict);
bool                        umdbus_parser_get_bool              (DBusMessageIter *iter, bool *pval);
bool                        umdbus_parser_get_int               (DBusMessageIter *iter, int *pval);
bool                        umdbus_parser_get_string            (DBusMessageIter *iter, const char **pval);
bool                        umdbus_parser_get_object            (DBusMessageIter *iter, const char **pval);
bool                        umdbus_parser_get_variant           (DBusMessageIter *iter, DBusMessageIter *val);
bool                        umdbus_parser_get_array             (DBusMessageIter *iter, DBusMessageIter *val);
bool                        umdbus_parser_get_struct            (DBusMessageIter *iter, DBusMessageIter *val);
bool                        umdbus_parser_get_entry             (DBusMessageIter *iter, DBusMessageIter *val);
bool                        umdbus_append_init                  (DBusMessageIter *iter, DBusMessage *msg);
bool                        umdbus_open_container               (DBusMessageIter *iter, DBusMessageIter *sub, int type, const char *sign);
bool                        umdbus_close_container              (DBusMessageIter *iter, DBusMessageIter *sub, bool success);
bool                        umdbus_append_basic_value           (DBusMessageIter *iter, int type, const DBusBasicValue *val);
bool                        umdbus_append_basic_variant         (DBusMessageIter *iter, int type, const DBusBasicValue *val);
bool                        umdbus_append_bool                  (DBusMessageIter *iter, bool val);
bool                        umdbus_append_int                   (DBusMessageIter *iter, int val);
bool                        umdbus_append_string                (DBusMessageIter *iter, const char *val);
bool                        umdbus_append_bool_variant          (DBusMessageIter *iter, bool val);
bool                        umdbus_append_int_variant           (DBusMessageIter *iter, int val);
bool                        umdbus_append_string_variant        (DBusMessageIter *iter, const char *val);
bool                        umdbus_append_args_va               (DBusMessageIter *iter, int type, va_list va);
bool                        umdbus_append_args                  (DBusMessageIter *iter, int arg_type, ...);
DBusMessage                *umdbus_blocking_call                (DBusConnection *con, const char *dst, const char *obj, const char *iface, const char *meth, DBusError *err, int arg_type, ...);
bool                        umdbus_parse_reply                  (DBusMessage *rsp, int arg_type, ...);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static DBusConnection *umdbus_connection = NULL;
static gboolean        umdbus_service_name_acquired   = FALSE;

/* ========================================================================= *
 * MEMBER_INFO
 * ========================================================================= */

static void
member_info_introspect(const member_info_t *self, FILE *file)
{
    LOG_REGISTER_CONTEXT;

    switch( self->type ) {
    case DBUS_MESSAGE_TYPE_METHOD_CALL:
        /* All method call handlers are Introspectable */
        if( self->args )
            fprintf(file, "    <method name=\"%s\">\n%s    </method>\n", self->member, self->args);
        else
            fprintf(file, "    <method name=\"%s\"/>\n", self->member);
        break;
    case DBUS_MESSAGE_TYPE_SIGNAL:
        /* Only dummy signal handlers are Introspectable */
        if( self->handler )
            break;
        if( self->args )
            fprintf(file, "    <signal name=\"%s\">\n%s    </signal>\n", self->member, self->args);
        else
            fprintf(file, "    <signal name=\"%s\"/>\n", self->member);
        break;
    default:
        break;
    }
}

/* ========================================================================= *
 * INTERFACE_INFO
 * ========================================================================= */

static const member_info_t *
interface_info_get_member(const interface_info_t *self, const char *member)
{
    LOG_REGISTER_CONTEXT;

    const member_info_t *mem = 0;

    if( !self || !member )
        goto EXIT;

    for( size_t i = 0; self->members[i].member; ++i ) {
        if( strcmp(self->members[i].member, member) )
            continue;
        mem = &self->members[i];
        break;
    }
EXIT:
    return mem;
}

static void
interface_info_introspect(const interface_info_t *self, FILE *file)
{
    LOG_REGISTER_CONTEXT;

    fprintf(file, "  <interface name=\"%s\">\n", self->interface);
    for( size_t i = 0; self->members[i].member; ++i )
        member_info_introspect(&self->members[i], file);
    fprintf(file, "  </interface>\n");
}

/* ========================================================================= *
 * OBJECT_INFO
 * ========================================================================= */

static const interface_info_t *
object_info_get_interface(const object_info_t *self, const char *interface)
{
    LOG_REGISTER_CONTEXT;

    const interface_info_t *ifc = 0;

    if( !self || !interface )
        goto EXIT;

    for( size_t i = 0; self->interfaces[i]; ++i ) {
        if( strcmp(self->interfaces[i]->interface, interface) )
            continue;
        ifc = self->interfaces[i];
        break;
    }
EXIT:
    return ifc;
}

static void
object_info_introspect(const object_info_t *self, FILE *file, const char *interface)
{
    LOG_REGISTER_CONTEXT;

    if( !self || !file )
        goto EXIT;

    static const char dtd[] =
        "<!DOCTYPE node PUBLIC\n"
        " \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
        " \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n";

    fprintf(file, "%s\n", dtd);

    fprintf(file, "<node name=\"%s\">\n", self->object);
    for( size_t i = 0; self->interfaces[i]; ++i ) {
        /* Optionally skip all but requested interface */
        if( interface && strcmp(self->interfaces[i]->interface, interface) )
            continue;
        interface_info_introspect(self->interfaces[i], file);
    }

    /* ASSUMED: self is in an statically allocated array where potential
     *          child nodes are located after it.
     */
    const char *parent = self->object;
    if( !strcmp(parent, "/") )
        parent = "";
    size_t n = strlen(parent);
    for( const object_info_t *obj = self + 1; obj->object; ++obj ) {
        const char *child = obj->object;
        if( strncmp(parent, child, n) )
            continue;
        if( child[n] != '/' )
            continue;
        child += n + 1;
        if( strchr(child, '/' ) )
            continue;
        fprintf(file, "  <node name=\"%s\"/>\n", child);
    }

    fprintf(file, "</node>\n");
EXIT:
    return;
}

static char *
object_info_get_introspect_xml(const object_info_t *self, const char *interface)
{
    LOG_REGISTER_CONTEXT;

    char *text = 0;

    if( self ) {
        size_t  size = 0;
        FILE   *file = open_memstream(&text, &size);
        object_info_introspect(self, file, interface);
        fclose(file);
    }

    return text;
}

/* ========================================================================= *
 * INTROSPECTABLE  --  org.freedesktop.DBus.Introspectable
 * ========================================================================= */

static void
introspectable_introspect_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    char *text = object_info_get_introspect_xml(context->object_info, 0);
    if( (context->rsp = dbus_message_new_method_return(context->msg)) )
        dbus_message_append_args(context->rsp, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
    free(text);
}

static const member_info_t introspectable_members[] =
{
  ADD_METHOD("Introspect",
             introspectable_introspect_cb,
             "      <arg name=\"xml\" type=\"s\" direction=\"out\"/>\n"),
  ADD_SENTINEL
};

static const interface_info_t introspectable_interface = {
    .interface = "org.freedesktop.DBus.Introspectable",
    .members  = introspectable_members
};

/* ========================================================================= *
 * PEER  --  org.freedesktop.DBus.Peer
 * ========================================================================= */

static const member_info_t peer_members[] =
{
  /* Note: Introspect glue only - libdbus handles these internally */
  ADD_METHOD("Ping",
             0,
             0),
  ADD_METHOD("GetMachineId",
             0,
             "      <arg direction=\"out\" name=\"machine_uuid\" type=\"s\"/>\n"),
  ADD_SENTINEL
};

static const interface_info_t peer_interface = {
    .interface = "org.freedesktop.DBus.Peer",
    .members  = peer_members
};

/* ========================================================================= *
 * USB_MODED -- com.meego.usb_moded
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * mode transition
 * ------------------------------------------------------------------------- */

/** Get currently active usb mode
 */
static void
usb_moded_state_request_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    const char *mode = control_get_external_mode();
    /* To the outside we want to keep CHARGING and CHARGING_FALLBACK the same */
    if( !strcmp(MODE_CHARGING_FALLBACK, mode) )
        mode = MODE_CHARGING;
    if( (context->rsp = dbus_message_new_method_return(context->msg)) )
        dbus_message_append_args(context->rsp, DBUS_TYPE_STRING, &mode, DBUS_TYPE_INVALID);
}

/** Get current target usb mode
 */
static void
usb_moded_target_state_get_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    const char *mode = control_get_target_mode();
    if( (context->rsp = dbus_message_new_method_return(context->msg)) )
        dbus_message_append_args(context->rsp, DBUS_TYPE_STRING, &mode, DBUS_TYPE_INVALID);
}

/** Get current target usb mode configuration
 */
static void
usb_moded_target_config_get_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    const char *mode = control_get_target_mode();
    if( (context->rsp = dbus_message_new_method_return(context->msg)) )
        umdbus_append_mode_details(context->rsp, mode);
}

/** Set usb mode
 *
 * When accepted, mode shows up 1st as target mode and then as active mode
 */
static void
usb_moded_state_set_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    const char *mode = control_get_external_mode();
    char       *use  = 0;
    DBusError   err  = DBUS_ERROR_INIT;
    uid_t       uid  = umdbus_get_sender_uid(context->sender);

    if( !dbus_message_get_args(context->msg, &err, DBUS_TYPE_STRING, &use, DBUS_TYPE_INVALID) ) {
        log_err("parse error: %s: %s", err.name, err.message);
        context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, context->member);
    }
    else if( !usbmoded_is_mode_permitted(use, uid) ) {
        /* Insufficient permissions */
        log_warning("Mode '%s' is not allowed for uid %d", use, uid);
        context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_ACCESS_DENIED, context->member);
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
    else if( !control_select_mode(use) ) {
        /* Requested mode could not be activated */
        log_warning("Mode '%s' was rejected", use);
    }
    else {
        /* Mode switch initiated (or requested mode already active) */
        log_debug("Mode '%s' requested", use);

        /* Acknowledge that the mode request was accepted */
        if( (context->rsp = dbus_message_new_method_return(context->msg)) )
            dbus_message_append_args(context->rsp, DBUS_TYPE_STRING, &use, DBUS_TYPE_INVALID);
    }

    /* Default to returning a generic error context->rsp */
    if( !context->rsp )
        context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_FAILED, context->member);

    dbus_error_free(&err);
}

/* ------------------------------------------------------------------------- *
 * default mode
 * ------------------------------------------------------------------------- */

/** Set default mode to use on pc connection
 */
static void
usb_moded_config_set_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    char       *config = 0;
    DBusError   err    = DBUS_ERROR_INIT;
    uid_t       uid    = umdbus_get_sender_uid(context->sender);

    if( !dbus_message_get_args(context->msg, &err, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID) ) {
        context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, context->member);
    }
    else {
        /* error checking is done when setting configuration */
        int ret = config_set_mode_setting(config, uid);
        if( SET_CONFIG_OK(ret) ) {
            if( (context->rsp = dbus_message_new_method_return(context->msg)) )
                dbus_message_append_args(context->rsp, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID);
        }
        else {
            context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, config);
        }
    }
    dbus_error_free(&err);
}

/** Get default mode to use on pc connection
 */
static void
usb_moded_config_get_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    uid_t  uid    = umdbus_get_sender_uid(context->sender);
    char  *config = config_get_mode_setting(uid);

    if( (context->rsp = dbus_message_new_method_return(context->msg)) )
        dbus_message_append_args(context->rsp, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID);
    g_free(config);
}

/* ------------------------------------------------------------------------- *
 * supported modes  --  modes that exist and are not hidden
 * ------------------------------------------------------------------------- */

/** Get comma separated list of supported modes
 */
static void
usb_moded_mode_list_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    gchar *mode_list = common_get_mode_list(SUPPORTED_MODES_LIST, 0);

    if( (context->rsp = dbus_message_new_method_return(context->msg)) )
        dbus_message_append_args(context->rsp, DBUS_TYPE_STRING, &mode_list, DBUS_TYPE_INVALID);
    g_free(mode_list);
}

/* ------------------------------------------------------------------------- *
 * available modes  --  modes that exist and are whitelisted and not hidden
 * ------------------------------------------------------------------------- */

/** Get comma separated list of modes available for selection
 */
static void
usb_moded_available_modes_get_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    gchar *mode_list = common_get_mode_list(AVAILABLE_MODES_LIST, 0);

    if( (context->rsp = dbus_message_new_method_return(context->msg)) )
        dbus_message_append_args(context->rsp, DBUS_TYPE_STRING, &mode_list, DBUS_TYPE_INVALID);
    g_free(mode_list);
}

/** Get comma separated list of modes available for selection by current user
 */
static void
usb_moded_available_modes_for_user_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    uid_t uid = umdbus_get_sender_uid(context->sender);
    gchar *mode_list = common_get_mode_list(AVAILABLE_MODES_LIST, uid);

    if( (context->rsp = dbus_message_new_method_return(context->msg)) )
        dbus_message_append_args(context->rsp, DBUS_TYPE_STRING, &mode_list, DBUS_TYPE_INVALID);
    g_free(mode_list);
}

/* ------------------------------------------------------------------------- *
 * hidden modes  --  one layer of masking modes from settings ui
 * ------------------------------------------------------------------------- */

/** Hide a mode so it is not selectable in settings ui
 */
static void
usb_moded_mode_hide_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    char      *config = 0;
    DBusError  err    = DBUS_ERROR_INIT;

    if( !dbus_message_get_args(context->msg, &err, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID) ) {
        context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, context->member);
    }
#ifdef SAILFISH_ACCESS_CONTROL
    /* do not let non-owner user hide modes */
    else if( !sailfish_access_control_hasgroup(umdbus_get_sender_uid(context->sender), "sailfish-system") ) {
        context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_ACCESS_DENIED, context->member);
    }
#endif
    else {
        /* error checking is done when setting configuration */
        int ret = config_set_hide_mode_setting(config);
        if( SET_CONFIG_OK(ret) ) {
            if( (context->rsp = dbus_message_new_method_return(context->msg)) )
                dbus_message_append_args(context->rsp, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID);
        }
        else {
            context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, config);
        }
    }
    dbus_error_free(&err);
}

/** Unhide a mode so it is selectable in settings ui
 */
static void
usb_moded_mode_unhide_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    char      *config = 0;
    DBusError  err    = DBUS_ERROR_INIT;

    if( !dbus_message_get_args(context->msg, &err, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID) ) {
        context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, context->member);
    }
#ifdef SAILFISH_ACCESS_CONTROL
    /* do not let non-owner user unhide modes */
    else if( !sailfish_access_control_hasgroup(umdbus_get_sender_uid(context->sender), "sailfish-system") ) {
        context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_ACCESS_DENIED, context->member);
    }
#endif
    else {
        /* error checking is done when setting configuration */
        int ret = config_set_unhide_mode_setting(config);
        if( SET_CONFIG_OK(ret) ) {
            if( (context->rsp = dbus_message_new_method_return(context->msg)) )
                dbus_message_append_args(context->rsp, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID);
        }
        else {
            context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, config);
        }
    }
    dbus_error_free(&err);
}

/** Get a comma separated list of hidden modes
 */
static void
usb_moded_hidden_get_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    char *config = config_get_hidden_modes();
    if( !config )
        config = g_strdup("");
    if( (context->rsp = dbus_message_new_method_return(context->msg)) )
        dbus_message_append_args(context->rsp, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID);
    g_free(config);
}

/* ------------------------------------------------------------------------- *
 * whitelisted modes  --  another layer of masking modes from settings ui
 * ------------------------------------------------------------------------- */

/** Get comma separated list of whitelisted usb modes
 */
static void
usb_moded_whitelisted_modes_get_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    gchar *mode_list = config_get_mode_whitelist();

    if( !mode_list )
        mode_list = g_strdup("");

    if( (context->rsp = dbus_message_new_method_return(context->msg)) )
        dbus_message_append_args(context->rsp, DBUS_TYPE_STRING, &mode_list, DBUS_TYPE_INVALID);
    g_free(mode_list);
}

/** Set comma separated list of whitelisted usb modes
 */
static void
usb_moded_whitelisted_modes_set_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    const char *whitelist = 0;
    DBusError   err       = DBUS_ERROR_INIT;

    if( !dbus_message_get_args(context->msg, &err, DBUS_TYPE_STRING, &whitelist, DBUS_TYPE_INVALID) ) {
        context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, context->member);
    }
    else {
        int ret = config_set_mode_whitelist(whitelist);
        if( SET_CONFIG_OK(ret) ) {
            if( (context->rsp = dbus_message_new_method_return(context->msg)) )
                dbus_message_append_args(context->rsp, DBUS_TYPE_STRING, &whitelist, DBUS_TYPE_INVALID);
        }
        else
            context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, whitelist);
    }
    dbus_error_free(&err);
}

/** Clear user config
 */
static void
usb_moded_user_config_clear_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    dbus_uint32_t uid = 0;
    DBusError   err       = DBUS_ERROR_INIT;

    if( !dbus_message_get_args(context->msg, &err, DBUS_TYPE_UINT32, &uid, DBUS_TYPE_INVALID) ) {
        context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, context->member);
    }
    else {
        if ( !config_user_clear(uid) )
            context->rsp =  dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, context->member);
        else if( (context->rsp = dbus_message_new_method_return(context->msg)) )
            dbus_message_append_args(context->rsp, DBUS_TYPE_UINT32, &uid, DBUS_TYPE_INVALID);
    }
    dbus_error_free(&err);
}

/** Add usb mode to whitelist
 */
static void
usb_moded_whitelisted_set_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    const char   *mode    = 0;
    dbus_bool_t   enabled = FALSE;
    DBusError     err     = DBUS_ERROR_INIT;

    if( !dbus_message_get_args(context->msg, &err, DBUS_TYPE_STRING, &mode, DBUS_TYPE_BOOLEAN, &enabled, DBUS_TYPE_INVALID) )
        context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, context->member);
    else {
        int ret = config_set_mode_in_whitelist(mode, enabled);
        if( SET_CONFIG_OK(ret) )
            context->rsp = dbus_message_new_method_return(context->msg);
        else
            context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, mode);
    }
    dbus_error_free(&err);
}

/* ------------------------------------------------------------------------- *
 * network configuration
 * ------------------------------------------------------------------------- */

/** Set network configuration value
 */
static void
usb_moded_network_set_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    char      *config  = 0;
    char      *setting = 0;
    DBusError  err     = DBUS_ERROR_INIT;

    if( !dbus_message_get_args(context->msg, &err, DBUS_TYPE_STRING, &config, DBUS_TYPE_STRING, &setting, DBUS_TYPE_INVALID) ) {
        context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, context->member);
    }
    else {
        /* error checking is done when setting configuration */
        int ret = config_set_network_setting(config, setting);
        if( SET_CONFIG_OK(ret) ) {
            if( (context->rsp = dbus_message_new_method_return(context->msg)) )
                dbus_message_append_args(context->rsp, DBUS_TYPE_STRING, &config, DBUS_TYPE_STRING, &setting, DBUS_TYPE_INVALID);
            network_update();
        }
        else {
            context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, config);
        }
    }
    dbus_error_free(&err);
}

/** Get network configuration value
 */
static void
usb_moded_network_get_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    char      *config  = 0;
    DBusError  err     = DBUS_ERROR_INIT;

    if( !dbus_message_get_args(context->msg, &err, DBUS_TYPE_STRING, &config, DBUS_TYPE_INVALID) ) {
        context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, context->member);
    }
    else {
        gchar *setting = config_get_network_setting(config);
        if( !setting )
            setting = config_get_network_fallback(config);
        if( setting ) {
            if( (context->rsp = dbus_message_new_method_return(context->msg)) )
                dbus_message_append_args(context->rsp, DBUS_TYPE_STRING, &config, DBUS_TYPE_STRING, &setting, DBUS_TYPE_INVALID);
        }
        else {
            context->rsp = dbus_message_new_error(context->msg, DBUS_ERROR_INVALID_ARGS, config);
        }
        g_free(setting);
    }
    dbus_error_free(&err);
}

/* ------------------------------------------------------------------------- *
 * miscellaneous
 * ------------------------------------------------------------------------- */

/** Turn off rescue mode
 */
static void
usb_moded_rescue_off_cb(umdbus_context_t *context)
{
    LOG_REGISTER_CONTEXT;

    usbmoded_set_rescue_mode(false);
    log_debug("Rescue mode off\n ");
    context->rsp = dbus_message_new_method_return(context->msg);
}

static const member_info_t usb_moded_members[] =
{
    ADD_METHOD(USB_MODE_STATE_REQUEST,
               usb_moded_state_request_cb,
               "      <arg name=\"mode\" type=\"s\" direction=\"out\"/>\n"),
    ADD_METHOD(USB_MODE_TARGET_STATE_GET,
               usb_moded_target_state_get_cb,
               "      <arg name=\"mode\" type=\"s\" direction=\"out\"/>\n"),
    ADD_METHOD(USB_MODE_TARGET_CONFIG_GET,
               usb_moded_target_config_get_cb,
               "      <arg name=\"config\" type=\"a{sv}\" direction=\"out\"/>\n"
               "      <annotation name=\"org.qtproject.QtDBus.QtTypeName.Out0\" value=\"QVariantMap\"/>\n"),
    ADD_METHOD(USB_MODE_STATE_SET,
               usb_moded_state_set_cb,
               "      <arg name=\"mode\" type=\"s\" direction=\"in\"/>\n"
               "      <arg name=\"mode\" type=\"s\" direction=\"out\"/>\n"),
    ADD_METHOD(USB_MODE_CONFIG_SET,
               usb_moded_config_set_cb,
               "      <arg name=\"config\" type=\"s\" direction=\"in\"/>\n"
               "      <arg name=\"config\" type=\"s\" direction=\"out\"/>\n"),
    ADD_METHOD(USB_MODE_CONFIG_GET,
               usb_moded_config_get_cb,
               "      <arg name=\"mode\" type=\"s\" direction=\"out\"/>\n"),
    ADD_METHOD(USB_MODE_LIST,
               usb_moded_mode_list_cb,
               "      <arg name=\"modes\" type=\"s\" direction=\"out\"/>\n"),
    ADD_METHOD(USB_MODE_AVAILABLE_MODES_GET,
               usb_moded_available_modes_get_cb,
               "      <arg name=\"modes\" type=\"s\" direction=\"out\"/>\n"),
    ADD_METHOD(USB_MODE_AVAILABLE_MODES_FOR_USER,
               usb_moded_available_modes_for_user_cb,
               "      <arg name=\"modes\" type=\"s\" direction=\"out\"/>\n"),
    ADD_METHOD(USB_MODE_HIDE,
               usb_moded_mode_hide_cb,
               "      <arg name=\"mode\" type=\"s\" direction=\"in\"/>\n"
               "      <arg name=\"mode\" type=\"s\" direction=\"out\"/>\n"),
    ADD_METHOD(USB_MODE_UNHIDE,
               usb_moded_mode_unhide_cb,
               "      <arg name=\"mode\" type=\"s\" direction=\"in\"/>\n"
               "      <arg name=\"mode\" type=\"s\" direction=\"out\"/>\n"),
    ADD_METHOD(USB_MODE_HIDDEN_GET,
               usb_moded_hidden_get_cb,
               "      <arg name=\"modes\" type=\"s\" direction=\"out\"/>\n"),
    ADD_METHOD(USB_MODE_WHITELISTED_MODES_GET,
               usb_moded_whitelisted_modes_get_cb,
               "      <arg name=\"modes\" type=\"s\" direction=\"out\"/>\n"),
    ADD_METHOD(USB_MODE_WHITELISTED_MODES_SET,
               usb_moded_whitelisted_modes_set_cb,
               "      <arg name=\"modes\" type=\"s\" direction=\"in\"/>\n"),
    ADD_METHOD(USB_MODE_WHITELISTED_SET,
               usb_moded_whitelisted_set_cb,
               "      <arg name=\"mode\" type=\"s\" direction=\"in\"/>\n"
               "      <arg name=\"whitelisted\" type=\"b\" direction=\"in\"/>\n"),
    ADD_METHOD(USB_MODE_NETWORK_SET,
               usb_moded_network_set_cb,
               "      <arg name=\"key\" type=\"s\" direction=\"in\"/>\n"
               "      <arg name=\"value\" type=\"s\" direction=\"in\"/>\n"
               "      <arg name=\"key\" type=\"s\" direction=\"out\"/>\n"
               "      <arg name=\"value\" type=\"s\" direction=\"out\"/>\n"),
    ADD_METHOD(USB_MODE_NETWORK_GET,
               usb_moded_network_get_cb,
               "      <arg name=\"key\" type=\"s\" direction=\"in\"/>\n"
               "      <arg name=\"key\" type=\"s\" direction=\"out\"/>\n"
               "      <arg name=\"value\" type=\"s\" direction=\"out\"/>\n"),
    ADD_METHOD(USB_MODE_RESCUE_OFF,
               usb_moded_rescue_off_cb,
               0),
    ADD_METHOD(USB_MODE_USER_CONFIG_CLEAR,
               usb_moded_user_config_clear_cb,
               "      <arg name=\"uid\" type=\"u\" direction=\"in\"/>\n"),
    ADD_SIGNAL(USB_MODE_SIGNAL_NAME,
               "      <arg name=\"mode_or_event\" type=\"s\"/>\n"),
    ADD_SIGNAL(USB_MODE_CURRENT_STATE_SIGNAL_NAME,
               "      <arg name=\"mode\" type=\"s\"/>\n"),
    ADD_SIGNAL(USB_MODE_TARGET_STATE_SIGNAL_NAME,
               "      <arg name=\"mode\" type=\"s\"/>\n"),
    ADD_SIGNAL(USB_MODE_TARGET_CONFIG_SIGNAL_NAME,
               "      <arg name=\"config\" type=\"a{sv}\" direction=\"out\"/>\n"
               "      <annotation name=\"org.qtproject.QtDBus.QtTypeName.Out0\" value=\"QVariantMap\"/>\n"),
    ADD_SIGNAL(USB_MODE_EVENT_SIGNAL_NAME,
               "      <arg name=\"event\" type=\"s\"/>\n"),
    ADD_SIGNAL(USB_MODE_CONFIG_SIGNAL_NAME,
               "      <arg name=\"section\" type=\"s\"/>\n"
               "      <arg name=\"key\" type=\"s\"/>\n"
               "      <arg name=\"value\" type=\"s\"/>\n"),
    ADD_SIGNAL(USB_MODE_SUPPORTED_MODES_SIGNAL_NAME,
               "      <arg name=\"modes\" type=\"s\"/>\n"),
    ADD_SIGNAL(USB_MODE_AVAILABLE_MODES_SIGNAL_NAME,
               "      <arg name=\"modes\" type=\"s\"/>\n"),
    ADD_SIGNAL(USB_MODE_HIDDEN_MODES_SIGNAL_NAME,
               "      <arg name=\"modes\" type=\"s\"/>\n"),
    ADD_SIGNAL(USB_MODE_WHITELISTED_MODES_SIGNAL_NAME,
               "      <arg name=\"modes\" type=\"s\"/>\n"),
    ADD_SIGNAL(USB_MODE_ERROR_SIGNAL_NAME,
               "      <arg name=\"error\" type=\"s\"/>\n"),
    ADD_SENTINEL
};

static const interface_info_t usb_moded_interface = {
    .interface = USB_MODE_INTERFACE,
    .members   = usb_moded_members
};

/* ========================================================================= *
 * Functions
 * ========================================================================= */

/** Standard D-Bus interfaces exposed by all objects
 */
static const interface_info_t *standard_interfaces[] = {
    &introspectable_interface,
    &peer_interface,
    0
};

/** D-Bus interfaces exposed by USB_MODE_OBJECT
 */
static const interface_info_t *usb_moded_interfaces[] = {
    &introspectable_interface,
    &peer_interface,
    &usb_moded_interface,
    0
};

/** Object "tree" usb-moded makes available
 */
static const object_info_t usb_moded_objects[] =
{
    /* NOTE: Parents must be listed before children.
     *       See object_info_introspect().
     */
    {
        .object     = "/",
        .interfaces = standard_interfaces,
    },
    {
        .object     = "/com",
        .interfaces = standard_interfaces,
    },
    {
        .object     = "/com/meego",
        .interfaces = standard_interfaces,
    },
    {
        .object     = USB_MODE_OBJECT, // = "/com/meego/usb_moded"
        .interfaces = usb_moded_interfaces,
    },
    {
        .object     = 0
    },
};

/** Locate info for D-Bus object path
 */
static const object_info_t *
umdbus_get_object_info(const char *object)
{
    LOG_REGISTER_CONTEXT;

    const object_info_t *obj = 0;

    if( !object )
        goto EXIT;

    for( size_t i = 0; usb_moded_objects[i].object; ++i ) {
        if( !strcmp(usb_moded_objects[i].object, object) ) {
            obj = &usb_moded_objects[i];
            break;
        }
    }

EXIT:
    return obj;
}

/** Dump D-Bus introspect XML to stdout
 *
 * For implementing --dbus-introspect-xml option
 */
void
umdbus_dump_introspect_xml(void)
{
    LOG_REGISTER_CONTEXT;

    const object_info_t *object_info = umdbus_get_object_info(USB_MODE_OBJECT);
    char *xml = object_info_get_introspect_xml(object_info, USB_MODE_INTERFACE);
    fprintf(stdout, "%s", xml ?: "\n");
    free(xml);
};

/** Dump D-Bus policy configuration XML to stdout
 *
 * For implementing --dbus-busconfig-xml option
 */
void
umdbus_dump_busconfig_xml(void)
{
    LOG_REGISTER_CONTEXT;

    static const char dtd[] =
        "<!DOCTYPE busconfig PUBLIC\n"
        " \"-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN\"\n"
        " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n";

    fprintf(stdout, "%s\n", dtd);
    fprintf(stdout, "<busconfig>\n");

    fprintf(stdout,
            "  <policy user=\"root\">\n"
            "    <allow own=\"" USB_MODE_SERVICE "\"/>\n"
            "    <allow send_destination=\"" USB_MODE_SERVICE "\"\n"
            "           send_interface=\"" USB_MODE_INTERFACE "\"/>\n"
            "  </policy>\n");

    fprintf(stdout,
            "  <policy context=\"default\">\n"
            "    <deny own=\"" USB_MODE_SERVICE "\"/>\n"
            "    <deny send_destination=\"" USB_MODE_SERVICE "\"\n"
            "          send_interface=\"" USB_MODE_INTERFACE "\"/>\n"
            "    <allow send_destination=\"" USB_MODE_SERVICE "\"\n"
            "           send_interface=\"org.freedesktop.DBus.Introspectable\"/>\n");

    for( const member_info_t *mem = usb_moded_members; mem->member; ++mem ) {
        if( mem->type != DBUS_MESSAGE_TYPE_METHOD_CALL )
            continue;
        fprintf(stdout,
                "    <allow send_destination=\"" USB_MODE_SERVICE "\"\n"
                "           send_interface=\"" USB_MODE_INTERFACE "\"\n"
                "           send_member=\"%s\"/>\n",
                mem->member);
    }
    fprintf(stdout, "  </policy>\n");
    fprintf(stdout, "</busconfig>\n");
}

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
    (void)user_data;

    LOG_REGISTER_CONTEXT;

    DBusHandlerResult status = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    umdbus_context_t context = { .msg = msg, };

    /* We are only interested in signals and method calls */
    switch( (context.type = dbus_message_get_type(msg)) ) {
    case DBUS_MESSAGE_TYPE_SIGNAL:
    case DBUS_MESSAGE_TYPE_METHOD_CALL:
        break;
    default:
        goto EXIT;
    }

    /* Parse message basic info */
    if( !(context.sender = dbus_message_get_sender(msg)) )
        goto EXIT;

    if( !(context.object = dbus_message_get_path(msg)) )
        goto EXIT;

    if( !(context.interface = dbus_message_get_interface(msg)) )
        goto EXIT;

    if( !(context.member = dbus_message_get_member(msg)) )
        goto EXIT;

    log_debug("DBUS %s %s.%s from %s",
              dbus_message_type_to_string(context.type),
              context.interface, context.member, context.sender);

    /* Deal with incoming signals */
    if( context.type == DBUS_MESSAGE_TYPE_SIGNAL ) {
        if( !strcmp(context.interface, INIT_DONE_INTERFACE) && !strcmp(context.member, INIT_DONE_SIGNAL) ) {
            /* Update the cached state value */
            usbmoded_set_init_done(true);
        }
        goto EXIT;
    }

    /* Locate and use method call handler */
    context.object_info    = umdbus_get_object_info(context.object);
    context.interface_info = object_info_get_interface(context.object_info,
                                                       context.interface);
    context.member_info    = interface_info_get_member(context.interface_info,
                                                       context.member);

    if( context.member_info && context.member_info->type == context.type ) {
        if( context.member_info->handler )
            context.member_info->handler(&context);
    }
    else if( !context.object_info ) {
        context.rsp = dbus_message_new_error_printf(context.msg,
                                                    DBUS_ERROR_UNKNOWN_OBJECT,
                                                    "Object '%s' does not exist",
                                                    context.object);
    }
    else if( !context.interface_info ) {
        context.rsp = dbus_message_new_error_printf(context.msg,
                                                    DBUS_ERROR_UNKNOWN_INTERFACE,
                                                    "Interface '%s' does not exist",
                                                    context.interface);
    }
    else {
        context.rsp = dbus_message_new_error_printf(context.msg,
                                                    DBUS_ERROR_UNKNOWN_METHOD,
                                                    "Method '%s.%s' does not exist",
                                                    context.interface,
                                                    context.member);
    }

EXIT:
    if( context.rsp ) {
        status = DBUS_HANDLER_RESULT_HANDLED;
        if( !dbus_message_get_no_reply(context.msg) ) {
            if( !dbus_connection_send(connection, context.rsp, 0) )
                log_debug("Failed sending reply. Out Of Memory!\n");
        }
        dbus_message_unref(context.rsp);
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
    dbus_gmain_set_up_connection(umdbus_connection, NULL);

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
            log_debug("DBUS ERROR: %s, %s", error.name, error.message);
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

#define ADD_STR_EX(name, memb) \
     if( !umdbus_append_string_entry(&dict, #name, data ? data->memb : 0) )\
             goto bailout_dict;
#define ADD_STR(name) \
     if( !umdbus_append_string_entry(&dict, #name, data ? data->name : 0) )\
             goto bailout_dict;
#define ADD_INT(name) \
     if( !umdbus_append_int32_entry(&dict, #name, data ? data->name : 0) )\
             goto bailout_dict;

    /* Attributes that we presume to be needed */
    ADD_INT(appsync);
    ADD_INT(network);
    ADD_STR_EX(network_interface, cached_interface);
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
 * @return Uid of the sender or UID_UNKNOWN if it can not be determined
 */
static uid_t
umdbus_get_sender_uid(const char *name)
{
    LOG_REGISTER_CONTEXT;

    pid_t        pid = PID_UNKNOWN;
    uid_t        uid = UID_UNKNOWN;
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

    if( !rsp && dbus_error_is_set(&err) ) {
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
    if( stat(path, &st) != -1 ) {
        uid = st.st_uid;
    }

EXIT:

    if( req ) dbus_message_unref(req);
    if( rsp ) dbus_message_unref(rsp);

    dbus_error_free(&err);

    return uid;
}

const char *
umdbus_arg_type_repr(int type)
{
    const char *repr = "UNKNOWN";
    switch( type ) {
    case DBUS_TYPE_INVALID:     repr = "INVALID";     break;
    case DBUS_TYPE_BYTE:        repr = "BYTE";        break;
    case DBUS_TYPE_BOOLEAN:     repr = "BOOLEAN";     break;
    case DBUS_TYPE_INT16:       repr = "INT16";       break;
    case DBUS_TYPE_UINT16:      repr = "UINT16";      break;
    case DBUS_TYPE_INT32:       repr = "INT32";       break;
    case DBUS_TYPE_UINT32:      repr = "UINT32";      break;
    case DBUS_TYPE_INT64:       repr = "INT64";       break;
    case DBUS_TYPE_UINT64:      repr = "UINT64";      break;
    case DBUS_TYPE_DOUBLE:      repr = "DOUBLE";      break;
    case DBUS_TYPE_STRING:      repr = "STRING";      break;
    case DBUS_TYPE_OBJECT_PATH: repr = "OBJECT_PATH"; break;
    case DBUS_TYPE_SIGNATURE:   repr = "SIGNATURE";   break;
    case DBUS_TYPE_UNIX_FD:     repr = "UNIX_FD";     break;
    case DBUS_TYPE_ARRAY:       repr = "ARRAY";       break;
    case DBUS_TYPE_VARIANT:     repr = "VARIANT";     break;
    case DBUS_TYPE_STRUCT:      repr = "STRUCT";      break;
    case DBUS_TYPE_DICT_ENTRY:  repr = "DICT_ENTRY";  break;
    default: break;
    }
    return repr;
}

const char *
umdbus_arg_type_signature(int type)
{
    const char *sign = 0;
    switch( type ) {
    case DBUS_TYPE_INVALID:      sign = DBUS_TYPE_INVALID_AS_STRING;      break;
    case DBUS_TYPE_BYTE:         sign = DBUS_TYPE_BYTE_AS_STRING;         break;
    case DBUS_TYPE_BOOLEAN:      sign = DBUS_TYPE_BOOLEAN_AS_STRING;      break;
    case DBUS_TYPE_INT16:        sign = DBUS_TYPE_INT16_AS_STRING;        break;
    case DBUS_TYPE_UINT16:       sign = DBUS_TYPE_UINT16_AS_STRING;       break;
    case DBUS_TYPE_INT32:        sign = DBUS_TYPE_INT32_AS_STRING;        break;
    case DBUS_TYPE_UINT32:       sign = DBUS_TYPE_UINT32_AS_STRING;       break;
    case DBUS_TYPE_INT64:        sign = DBUS_TYPE_INT64_AS_STRING;        break;
    case DBUS_TYPE_UINT64:       sign = DBUS_TYPE_UINT64_AS_STRING;       break;
    case DBUS_TYPE_DOUBLE:       sign = DBUS_TYPE_DOUBLE_AS_STRING;       break;
    case DBUS_TYPE_STRING:       sign = DBUS_TYPE_STRING_AS_STRING;       break;
    case DBUS_TYPE_OBJECT_PATH:  sign = DBUS_TYPE_OBJECT_PATH_AS_STRING;  break;
    case DBUS_TYPE_SIGNATURE:    sign = DBUS_TYPE_SIGNATURE_AS_STRING;    break;
    case DBUS_TYPE_UNIX_FD:      sign = DBUS_TYPE_UNIX_FD_AS_STRING;      break;
    case DBUS_TYPE_ARRAY:        sign = DBUS_TYPE_ARRAY_AS_STRING;        break;
    case DBUS_TYPE_VARIANT:      sign = DBUS_TYPE_VARIANT_AS_STRING;      break;
    case DBUS_TYPE_STRUCT:       sign = DBUS_TYPE_STRUCT_AS_STRING;       break;
    case DBUS_TYPE_DICT_ENTRY:   sign = DBUS_TYPE_DICT_ENTRY_AS_STRING;   break;
    default: break;
    }
    return sign;
}

const char *
umdbus_msg_type_repr(int type)
{
    return dbus_message_type_to_string(type);
}

bool
umdbus_parser_init(DBusMessageIter *iter, DBusMessage *msg)
{
    return iter && msg && dbus_message_iter_init(msg, iter);
}

int
umdbus_parser_at_type(DBusMessageIter *iter)
{
    return iter ? dbus_message_iter_get_arg_type(iter) : DBUS_TYPE_INVALID;
}

bool
umdbus_parser_at_end(DBusMessageIter *iter)
{
    return umdbus_parser_at_type(iter) == DBUS_TYPE_INVALID;
}

bool
umdbus_parser_require_type(DBusMessageIter *iter, int type, bool strict)
{
    int have = umdbus_parser_at_type(iter);

    if( have == type )
        return true;

    if( strict || have != DBUS_TYPE_INVALID )
        log_warning("expected %s, got %s",
                    umdbus_arg_type_repr(type),
                    umdbus_arg_type_repr(have));
    return false;
}

bool
umdbus_parser_get_bool(DBusMessageIter *iter, bool *pval)
{
    dbus_bool_t val = 0;
    bool ack = umdbus_parser_require_type(iter, DBUS_TYPE_BOOLEAN, true);
    if( ack ) {
        dbus_message_iter_get_basic(iter, &val);
        dbus_message_iter_next(iter);
    }
    return *pval = val, ack;
}

bool
umdbus_parser_get_int(DBusMessageIter *iter, int *pval)
{
    dbus_int32_t val = 0;
    bool ack = umdbus_parser_require_type(iter, DBUS_TYPE_INT32, true);
    if( ack ) {
        dbus_message_iter_get_basic(iter, &val);
        dbus_message_iter_next(iter);
    }
    return *pval = (int)val, ack;
}

bool
umdbus_parser_get_string(DBusMessageIter *iter, const char **pval)
{
    const char *val = 0;
    bool ack = umdbus_parser_require_type(iter, DBUS_TYPE_STRING, true);
    if( ack ) {
        dbus_message_iter_get_basic(iter, &val);
        dbus_message_iter_next(iter);
    }
    return *pval = val, ack;
}

bool
umdbus_parser_get_object(DBusMessageIter *iter, const char **pval)
{
    const char *val = 0;
    bool ack = umdbus_parser_require_type(iter, DBUS_TYPE_OBJECT_PATH, true);
    if( ack ) {
        dbus_message_iter_get_basic(iter, &val);
        dbus_message_iter_next(iter);
    }
    return *pval = val, ack;
}

bool
umdbus_parser_get_variant(DBusMessageIter *iter, DBusMessageIter *val)
{
    bool ack = umdbus_parser_require_type(iter, DBUS_TYPE_VARIANT, true);
    if( ack ) {
        dbus_message_iter_recurse(iter, val);
        dbus_message_iter_next(iter);
    }
    return ack;
}

bool
umdbus_parser_get_array(DBusMessageIter *iter, DBusMessageIter *val)
{
    bool ack = umdbus_parser_require_type(iter, DBUS_TYPE_ARRAY, true);
    if( ack ) {
        dbus_message_iter_recurse(iter, val);
        dbus_message_iter_next(iter);
    }
    return ack;
}

bool
umdbus_parser_get_struct(DBusMessageIter *iter, DBusMessageIter *val)
{
    bool ack = umdbus_parser_require_type(iter, DBUS_TYPE_STRUCT, false);
    if( ack ) {
        dbus_message_iter_recurse(iter, val);
        dbus_message_iter_next(iter);
    }
    return ack;
}

bool
umdbus_parser_get_entry(DBusMessageIter *iter, DBusMessageIter *val)
{
    bool ack = umdbus_parser_require_type(iter, DBUS_TYPE_DICT_ENTRY, false);
    if( ack ) {
        dbus_message_iter_recurse(iter, val);
        dbus_message_iter_next(iter);
    }
    return ack;
}

bool
umdbus_append_init(DBusMessageIter *iter, DBusMessage *msg)
{
    bool ack = iter && msg && (dbus_message_iter_init_append(msg, iter), true);
    return ack;
}

bool
umdbus_open_container(DBusMessageIter *iter, DBusMessageIter *sub, int type, const char *sign)
{
    bool ack = dbus_message_iter_open_container(iter, type, sign, sub);
    if( ack ) {
        /* Caller must make call umdbus_close_container() */
    }
    else {
        /* Caller must not call umdbus_close_container() */
        log_warning("failed to open %s container with signature: %s",
                    umdbus_arg_type_repr(type), sign ?: "");
    }
    return ack;
}

bool
umdbus_close_container(DBusMessageIter *iter, DBusMessageIter *sub, bool success)
{
    if( success ) {
        if( !(success = dbus_message_iter_close_container(iter, sub)) ) {
            log_warning("failed to close container");
        }
    }
    else {
        log_warning("abandoning container");
        dbus_message_iter_abandon_container(iter, sub);
    }
    return success;
}

bool
umdbus_append_basic_value(DBusMessageIter *iter, int type, const DBusBasicValue *val)
{
    if( log_p(LOG_DEBUG) ) {
        char buff[64] = "";
        const char *repr = buff;
        switch( type ) {
        case DBUS_TYPE_BYTE:
            snprintf(buff, sizeof buff, "%u", val->byt);
            break;
        case DBUS_TYPE_BOOLEAN:
            repr = val->bool_val ? "true" : "false";
            break;
        case DBUS_TYPE_INT16:
            snprintf(buff, sizeof buff, "%d", val->i16);
            break;
        case DBUS_TYPE_UINT16:
            snprintf(buff, sizeof buff, "%u", val->u16);
            break;
        case DBUS_TYPE_INT32:
            snprintf(buff, sizeof buff, "%d", val->i32);
            break;
        case DBUS_TYPE_UINT32:
            snprintf(buff, sizeof buff, "%u", val->u32);
            break;
        case DBUS_TYPE_INT64:
            snprintf(buff, sizeof buff, "%lld", (long long)val->i64);
            break;
        case DBUS_TYPE_UINT64:
            snprintf(buff, sizeof buff, "%llu", (unsigned long long)val->u64);
            break;
        case DBUS_TYPE_DOUBLE:
            snprintf(buff, sizeof buff, "%g", val->dbl);
            break;
        case DBUS_TYPE_STRING:
        case DBUS_TYPE_OBJECT_PATH:
        case DBUS_TYPE_SIGNATURE:
            repr = (const char *)val->str;
            break;
        case DBUS_TYPE_UNIX_FD:
            snprintf(buff, sizeof buff, "%d", val->fd);
            break;
        default:
        case DBUS_TYPE_INVALID:
        case DBUS_TYPE_ARRAY:
        case DBUS_TYPE_VARIANT:
        case DBUS_TYPE_STRUCT:
        case DBUS_TYPE_DICT_ENTRY:
          // not expected
          break;
        }
        log_debug("append %s value %s", umdbus_arg_type_repr(type), repr);
    }

    if (dbus_message_iter_append_basic(iter, type, val))
        return true;

    log_warning("failed to append %s argument", umdbus_arg_type_repr(type));
    return false;
}

bool
umdbus_append_basic_variant(DBusMessageIter *iter, int type, const DBusBasicValue *val)
{
    bool ack = false;
    const char *sign = 0;

    log_debug("append %s variant", umdbus_arg_type_repr(type));

    if( !dbus_type_is_basic(type) )
        goto EXIT;

    if( !(sign = umdbus_arg_type_signature(type)) )
        goto EXIT;

    DBusMessageIter var;

    if( umdbus_open_container(iter, &var, DBUS_TYPE_VARIANT, sign) ) {
        ack = umdbus_append_basic_value(&var, DBUS_TYPE_BOOLEAN, val);
        ack = umdbus_close_container(iter, &var, ack);
    }

EXIT:

    if( !ack )
        log_warning("failed to append %s variant", umdbus_arg_type_repr(type));

    return ack;
}

bool
umdbus_append_bool(DBusMessageIter *iter, bool val)
{
    DBusBasicValue dta = { .bool_val = (dbus_bool_t)val };
    return umdbus_append_basic_value(iter, DBUS_TYPE_BOOLEAN, &dta);
}

bool
umdbus_append_int(DBusMessageIter *iter, int val)
{
    DBusBasicValue dta = { .i32 = (dbus_int32_t)val };
    return umdbus_append_basic_value(iter, DBUS_TYPE_INT32, &dta);
}

bool
umdbus_append_string(DBusMessageIter *iter, const char *val)
{
    DBusBasicValue dta = { .str = (char *)val };
    return umdbus_append_basic_value(iter, DBUS_TYPE_STRING, &dta);
}

bool
umdbus_append_bool_variant(DBusMessageIter *iter, bool val)
{
    DBusBasicValue dta = { .bool_val = val };
    return umdbus_append_basic_variant(iter, DBUS_TYPE_BOOLEAN, &dta);
}

bool
umdbus_append_int_variant(DBusMessageIter *iter, int val)
{
    DBusBasicValue dta = { .i32 = val };
    return umdbus_append_basic_variant(iter, DBUS_TYPE_INT32, &dta);
}

bool
umdbus_append_string_variant(DBusMessageIter *iter, const char *val)
{
    DBusBasicValue dta = { .str = (char *)val };
    return umdbus_append_basic_variant(iter, DBUS_TYPE_STRING, &dta);
}

bool
umdbus_append_args_va(DBusMessageIter *iter, int type, va_list va)
{
    bool ack = false;

    DBusBasicValue *arg;

    while( type != DBUS_TYPE_INVALID ) {
        switch( type ) {
        case DBUS_TYPE_VARIANT:
            type = va_arg(va, int);
            if( !dbus_type_is_basic(type) ) {
                log_err("variant type %s is not supported",
                        umdbus_arg_type_repr(type));
                goto EXIT;
            }
            arg = va_arg(va, DBusBasicValue *);
            if( !umdbus_append_basic_variant(iter, type, arg) )
                goto EXIT;

            break;

        case DBUS_TYPE_ARRAY:
        case DBUS_TYPE_STRUCT:
            /* Not supported yet - fall through */
        default:
            if( !dbus_type_is_basic(type) ) {
                log_err("value type %s is not supported",
                        umdbus_arg_type_repr(type));
                goto EXIT;
            }
            arg = va_arg(va, DBusBasicValue *);
            if( !umdbus_append_basic_value(iter, type, arg) )
                goto EXIT;
            break;
        }
        type = va_arg(va, int);
    }
    ack = true;
EXIT:
    return ack;
}

bool
umdbus_append_args(DBusMessageIter *iter, int arg_type, ...)
{
    va_list va;
    va_start(va, arg_type);
    bool ack = umdbus_append_args_va(iter, arg_type, va);
    va_end(va);
    return ack;
}

DBusMessage *
umdbus_blocking_call(DBusConnection *con,
                     const char     *dst,
                     const char     *obj,
                     const char     *iface,
                     const char     *meth,
                     DBusError      *err,
                     int            arg_type, ...)
{
    DBusMessage *rsp = 0;
    DBusMessage *req = 0;
    va_list      va;

    va_start(va, arg_type);

    if( !(req = dbus_message_new_method_call(dst, obj, iface, meth)) )
        goto EXIT;

    DBusMessageIter body;
    if( !umdbus_append_init(&body, req) )
        goto EXIT;

    /* Note: Unlike dbus_message_append_args_valist():
     * - simple variants are supported
     * - arrays are not (yet)
     */
    if( !umdbus_append_args_va(&body, arg_type, va) )
        goto EXIT;

    if( !(rsp = dbus_connection_send_with_reply_and_block(con, req, -1, err)) ) {
        log_warning("no reply to %s.%s(): %s: %s",
                    iface, meth, err->name, err->message);
        goto EXIT;
    }

    if( dbus_set_error_from_message(err, rsp) ) {
        log_warning("error reply to %s.%s(): %s: %s",
                    iface, meth, err->name, err->message);
        dbus_message_unref(rsp), rsp = 0;
        goto EXIT;
    }

    log_debug("blocking %s.%s() call succeeded", iface, meth);

EXIT:
    if( req )
        dbus_message_unref(req);

    va_end(va);

    return rsp;
}

bool
umdbus_parse_reply(DBusMessage *rsp, int arg_type, ...)
{
    bool      ack = false;
    DBusError err = DBUS_ERROR_INIT;
    va_list   va;

    va_start(va, arg_type);

    if( !rsp )
        goto EXIT;

    /* Note: It is assumed that differentiation between replies and
     *       error replies is done elsewhere.
     */

    if( !dbus_message_get_args_valist(rsp, &err, arg_type, va) ) {
        log_warning("parse error: %s: %s", err.name, err.message);
        goto EXIT;
    }

    ack = true;

EXIT:
    dbus_error_free(&err);

    va_end(va);

    return ack;
}
