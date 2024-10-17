/**
 * @file usb_moded-control.c
 *
 * Copyright (c) 2013 - 2021 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
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

#include "usb_moded-control.h"

#include "usb_moded.h"
#include "usb_moded-config-private.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded-log.h"
#include "usb_moded-modes.h"
#include "usb_moded-worker.h"

/* Sanity check, configure should take care of this */
#if defined SAILFISH_ACCESS_CONTROL && !defined SYSTEMD
# error if SAILFISH_ACCESS_CONTROL is defined, SYSTEMD must be defined as well
#endif

/* ========================================================================= *
 * Constants
 * ========================================================================= */

/** How long to wait for device lock after user change [ms]
 *
 * If device lock code is in use, the wait time needs to be long enough
 * to cover user session change and subsequent device locking (ballpark
 * 500 ms) and reasonable amount of variance caused by slower devices etc.
 *
 * If device lock code is not in use, the wait is not terminated by device
 * lock status changes and thus the time needs to be short enough not to
 * cause confusion at user end.
 */
#define CONTROL_PENDING_USER_CHANGE_TIMEOUT (3000)

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * CONTROL
 * ------------------------------------------------------------------------- */

uid_t            control_get_user_for_mode        (void);
void             control_set_user_for_mode        (uid_t uid);
const char      *control_get_external_mode        (void);
static void      control_set_external_mode        (const char *mode);
void             control_clear_external_mode      (void);
static void      control_update_external_mode     (void);
const char      *control_get_target_mode          (void);
static void      control_set_target_mode          (const char *mode);
void             control_clear_target_mode        (void);
const char      *control_get_selected_mode        (void);
void             control_set_selected_mode        (const char *mode);
bool             control_select_mode              (const char *mode);
const char      *control_get_usb_mode             (void);
void             control_clear_internal_mode      (void);
static void      control_set_usb_mode             (const char *mode);
void             control_mode_switched            (const char *mode);
static gboolean  control_pending_user_change_cb   (gpointer aptr);
static bool      control_have_pending_user_change (void);
static void      control_begin_pending_user_change(void);
static void      control_end_pending_user_change  (void);
void             control_user_changed             (void);
void             control_device_lock_changed      (void);
void             control_device_state_changed     (void);
void             control_settings_changed         (void);
void             control_init_done_changed        (void);
static bool      control_get_enabled              (void);
void             control_set_enabled              (bool enable);
static bool      control_get_in_rescue_mode       (void);
static void      control_set_in_rescue_mode       (bool in_rescue_mode);
static void      control_rethink_usb_mode         (void);
void             control_set_cable_state          (cable_state_t cable_state);
cable_state_t    control_get_cable_state          (void);
void             control_clear_cable_state        (void);
bool             control_get_connection_state     (void);

/* ========================================================================= *
 * Data
 * ========================================================================= */

/* The external mode;
 *
 * What was the last current mode signaled over D-Bus.
 */
static char *control_external_mode = NULL;

/* The target mode;
 *
 * What was the last target mode signaled over D-Bus.
 */
static char *control_target_mode = NULL;

/** The logical mode name
 *
 * Full set of valid modes can occur here
 */
static char *control_internal_mode = NULL;

/** The mode selected by user / via udev trigger
 */
static char *control_selected_mode = NULL;

/** Connection status
 *
 * Access only via:
 * - control_set_cable_state()
 * - control_get_connection_state()
 */
static cable_state_t control_cable_state = CABLE_STATE_UNKNOWN;

/** Uid of the user that has set current USB mode
 */
static uid_t control_user_for_mode = UID_UNKNOWN;

/** Id for user change delay timer
 */
static guint control_pending_user_change_id = 0;

/** Flag for: Rescue mode has been activated
 */
static bool control_in_rescue_mode = false;

/** Flag for: control functionality has been enabled
 */
static bool control_is_enabled = false;

/* ========================================================================= *
 * Functions
 * ========================================================================= */

/** Get uid of the user that set the current mode
 *
 *  @return uid of user when current mode was set
 */
uid_t
control_get_user_for_mode(void)
{
    return control_user_for_mode;
}

/** Set the uid of the user that set the current mode
 *
 *  @param uid of current user
 */
void
control_set_user_for_mode(uid_t uid)
{
    LOG_REGISTER_CONTEXT;

    if( control_user_for_mode != uid ) {
        log_debug("control_user_for_mode: %d -> %d",
                  (int)control_user_for_mode, (int)uid);
        control_user_for_mode = uid;
    }
}

const char *control_get_external_mode(void)
{
    LOG_REGISTER_CONTEXT;

    return control_external_mode ?: MODE_UNDEFINED;
}

static void control_set_external_mode(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    gchar *previous = control_external_mode;
    if( !g_strcmp0(previous, mode) )
        goto EXIT;

    log_debug("external_mode: %s -> %s",
              previous, mode);

    control_external_mode = g_strdup(mode);
    g_free(previous);

    // DO THE DBUS BROADCAST

    if( !strcmp(control_external_mode, MODE_ASK) ) {
        /* send signal, mode will be set when the dialog service calls
         * the set_mode method call. */
        umdbus_send_event_signal(USB_CONNECTED_DIALOG_SHOW);
    }

    umdbus_send_current_state_signal(control_external_mode);

    if( strcmp(control_external_mode, MODE_BUSY) ) {
        /* Stable state reached. Synchronize target state.
         *
         * Note that normally this ends up being a nop,
         * but might be needed if the originally scheduled
         * target could not be reached due to errors / user
         * disconnecting the cable.
         */
        control_set_target_mode(control_external_mode);
    }

EXIT:
    return;
}

void control_clear_external_mode(void)
{
    LOG_REGISTER_CONTEXT;

    g_free(control_external_mode),
        control_external_mode = 0;
}

static void control_update_external_mode(void)
{
    LOG_REGISTER_CONTEXT;

    const char *internal_mode = control_get_usb_mode();
    const char *external_mode = common_map_mode_to_external(internal_mode);

    control_set_external_mode(external_mode);
}

const char *control_get_target_mode(void)
{
    LOG_REGISTER_CONTEXT;

    return control_target_mode ?: MODE_UNDEFINED;
}

static void control_set_target_mode(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    gchar *previous = control_target_mode;
    if( !g_strcmp0(previous, mode) )
        goto EXIT;

    log_debug("target_mode: %s -> %s",
              previous, mode);

    control_target_mode = g_strdup(mode);
    g_free(previous);

    /* Cache settings that might be relevant for a dynamic mode, so
     * that the same values are available and used for both entering
     * and leaving the mode.
     */
    usbmoded_refresh_modedata(control_target_mode);

    umdbus_send_target_state_signal(control_target_mode);

EXIT:
    return;
}

void control_clear_target_mode(void)
{
    LOG_REGISTER_CONTEXT;

    g_free(control_target_mode),
        control_target_mode = 0;
}

/** get mode selected by user
 *
 * @return selected mode, or NULL in case nothing is selected
 */
const char *control_get_selected_mode(void)
{
    LOG_REGISTER_CONTEXT;
    return control_selected_mode;
}

/** set mode selected by user
 *
 * @param mode selected mode, or NULL to reset selection
 */
void control_set_selected_mode(const char *mode)
{
    LOG_REGISTER_CONTEXT;
    char *prev = control_selected_mode;
    if( g_strcmp0(prev, mode) ) {
        log_debug("requested: %s -> %s", prev, mode);
        control_selected_mode = mode ? g_strdup(mode) : 0;
        g_free(prev);
    }
}

/** handle mode request from client / udev trigger
 *
 * @param mode The requested USB mode
 * @return true if mode was accepted, false otherwise
 */
bool control_select_mode(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    /* Update selected mode */
    control_set_selected_mode(mode);

    /* Re-evaluate active mode */
    control_rethink_usb_mode();

    /* Return true if active mode matches the requested one */
    return !g_strcmp0(control_get_usb_mode(), mode);
}

/** get the usb mode
 *
 * @return the currently set mode
 *
 */
const char * control_get_usb_mode(void)
{
    LOG_REGISTER_CONTEXT;

    return control_internal_mode;
}

void control_clear_internal_mode(void)
{
    LOG_REGISTER_CONTEXT;

    g_free(control_internal_mode),
        control_internal_mode = 0;
}

/** set the usb mode
 *
 * @param mode The requested USB mode
 */
static void control_set_usb_mode(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    /* Bookkeeping: Who activated this mode */
    control_set_user_for_mode(usbmoded_get_current_user());

    gchar *previous = control_internal_mode;
    if( !g_strcmp0(previous, mode) )
        goto EXIT;

    log_debug("internal_mode: %s -> %s",
              previous, mode);

    control_internal_mode = g_strdup(mode);
    g_free(previous);

    /* Update target mode before declaring busy */
    control_set_target_mode(control_internal_mode);

    /* Invalidate current mode for the duration of mode transition */
    control_set_external_mode(MODE_BUSY);

    /* Propagate down to gadget config */
    if( !worker_request_hardware_mode(control_internal_mode) ) {
        /* No transition work to wait for -> end MODE_BUSY immediately */
        control_update_external_mode();
    }

EXIT:
    return;
}

/* Worker thread has finished mode switch
 *
 * @param mode The activated USB mode
 */
void control_mode_switched(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    /* Update state data - without retriggering the worker thread
     */
    if( g_strcmp0(control_internal_mode, mode) ) {
        log_debug("internal_mode: %s -> %s",
                  control_internal_mode, mode);
        g_free(control_internal_mode),
            control_internal_mode = g_strdup(mode);
    }

    /* Propagate up to D-Bus */
    control_update_external_mode();

    return;
}

/** Timer callback for terminating pending user change
 */
static gboolean control_pending_user_change_cb(gpointer aptr)
{
    (void)aptr;

    if( control_pending_user_change_id ) {
        log_debug("pending user change timeout");
        control_pending_user_change_id = 0;
        control_rethink_usb_mode();
    }

    return G_SOURCE_REMOVE;
}

/** User change in progress predicate
 */
static bool control_have_pending_user_change(void)
{
    return control_pending_user_change_id != 0;
}

/** Start pending user change delay
 */
static void control_begin_pending_user_change(void)
{
    if(  !control_pending_user_change_id ) {
        log_debug("pending user change started");
        control_pending_user_change_id =
            g_timeout_add(CONTROL_PENDING_USER_CHANGE_TIMEOUT,
                          control_pending_user_change_cb, 0);
    }
}

/** End pending user change delay
 */
static void control_end_pending_user_change(void)
{
    if( control_pending_user_change_id ) {
        log_debug("pending user change stopped");
        g_source_remove(control_pending_user_change_id),
            control_pending_user_change_id = 0;
    }
}

/** React to current user change
 */
void control_user_changed(void)
{
    log_debug("user = %d", (int)usbmoded_get_current_user());

    /* We need to mask false positive "user is known and
     * device is unlocked" blib arising from usb-moded
     * getting user change notification before device lock
     * status change -> start timer on user change and
     * act as if device were locked until timer expires
     * or device lock notification is received.
     *
     * But only for user changes that happen after the
     * device bootup has been finished.
     */
    if( usbmoded_init_done_p() )
        control_begin_pending_user_change();
    else
        control_end_pending_user_change();

    /* Clear any mode selection done by the previous user
     */
    control_set_selected_mode(0);

    control_rethink_usb_mode();
}

/** React to device lock state changes
 */
void control_device_lock_changed(void)
{
    log_debug("can_export = %d", usbmoded_can_export());

    /* Device lock status change finalizes user change
     */
    control_end_pending_user_change();

    control_rethink_usb_mode();
}

/** React to device state changes
 */
void control_device_state_changed(void)
{
    log_debug("in_usermode = %d; in_shutdown = %d",
              usbmoded_in_usermode(), usbmoded_in_shutdown());

    control_rethink_usb_mode();
}

/** React to settings changes
 */
void control_settings_changed(void)
{
    log_debug("settings changed");

    control_rethink_usb_mode();
}

/** React to init-done changes
 */
void control_init_done_changed(void)
{
    log_debug("init_done = %d", usbmoded_init_done_p());

    control_rethink_usb_mode();
}

/** Mode changes allowed predicate
 */
static bool control_get_enabled(void)
{
    return control_is_enabled;
}

/** Enable/disable mode changes
 */
void control_set_enabled(bool enable)
{
    if( control_is_enabled != enable ) {
        control_is_enabled = enable;
        log_debug("control_enabled = %d", control_is_enabled);

        control_rethink_usb_mode();
    }
}

/** Rescue mode is active predicate
 */
static bool control_get_in_rescue_mode(void)
{
    return control_in_rescue_mode;
}

/** Set/clear rescue mode activation state
 */
static void control_set_in_rescue_mode(bool in_rescue_mode)
{
    if( control_in_rescue_mode != in_rescue_mode ) {
        log_debug("in_rescue_mode: %d -> %d",
                  control_in_rescue_mode, in_rescue_mode);
        control_in_rescue_mode = in_rescue_mode;
    }
}

/** set the chosen usb state
 *
 * gauge what mode to enter and then call control_set_usb_mode()
 *
 */
static void control_rethink_usb_mode(void)
{
    LOG_REGISTER_CONTEXT;

    uid_t          current_user = usbmoded_get_current_user();
    const char    *current_mode = control_get_usb_mode();
    cable_state_t  cable_state  = control_get_cable_state();
    const char    *mode_to_use  = 0;
    char          *mode_to_free = 0;

    /* Local setter function, to ease debugging */
    auto const char *use_mode(const char *mode) {
        if( g_strcmp0(mode_to_use, mode) ) {
            log_debug("mode_to_use: %s -> %s",
                      mode_to_use ?: "unset",
                      mode        ?: "unset");
            mode_to_use = mode;
        }
        return mode_to_use;
    }

    log_debug("re-evaluating usb mode ...");

    /* Local setter function, for dynamically allocated mode names */
    auto const char *use_allocated_mode(char *mode) {
        g_free(mode_to_free), mode_to_free = mode;
        return use_mode(mode_to_free);
    }

    /* Defer mode selection until all noise resulting from
     * usb-moded startup is over, we know that a suitable
     * backend has been selected, etc.
     */
    if( !control_get_enabled() ) {
        log_debug("starting up; mode changes blocked");
        goto BAILOUT;
    }

    /* Handle cable disconnect / charger connect
     *
     * Only one mode is applicable regardless of things like current
     * user, device lock status, etc.
     */
    if( cable_state != CABLE_STATE_PC_CONNECTED ) {
        /* Reset bookkeeping that is relevant only for pc connection */
        control_set_selected_mode(0);
        control_set_in_rescue_mode(false);

        if( cable_state == CABLE_STATE_CHARGER_CONNECTED ) {
            /* Charger connected
             * -> CHARGER is the only options */
            use_mode(MODE_CHARGER);
        }
        else {
            /* Disconnected / unknown
             * -> UNDEFINED is the only option */
            use_mode(MODE_UNDEFINED);
        }
        goto MODESET;
    }

    /* Handle rescue mode override
     *
     * When booting up connected to a pc with rescue mode enabled,
     * lock on to rescue mode until something else is explicitly
     * requested / cable is detached.
     */
    if( usbmoded_get_rescue_mode() || control_get_in_rescue_mode() ) {
        if( !control_get_selected_mode() ) {
            /* Rescue mode active
             * -> DEVELOPER is the only option
             *
             */
            use_mode(MODE_DEVELOPER);
            control_set_in_rescue_mode(true);
            goto MODESET;
        }
    }
    control_set_in_rescue_mode(false);

    /* Handle diagnostic mode override
     */
    if( usbmoded_get_diag_mode() ) {
        /* Assumption is that in diag-mode there is only
         * one mode configured i.e. list head is diag-mode. */
        GList *iter = usbmoded_get_modelist();
        if( !iter ) {
            log_err("Diagnostic mode is not configured!");
            use_mode(MODE_CHARGING_FALLBACK);
        }
        else {
            log_debug("Entering diagnostic mode!");
            modedata_t *data = iter->data;
            use_mode(data->mode_name);
        }
        goto MODESET;
    }

    /* Handle bootup override
     *
     * Some modes (e.g. mtp) can require system to be in a
     * state where external services can be started/stopped.
     *
     * Normalize situation by blocking all dynamic modes until
     * bootup has been finished.
     */
    if( !usbmoded_init_done_p() ) {
        log_debug("in bootup; dynamic modes blocked");
        use_mode(MODE_CHARGING_FALLBACK);
        goto MODESET;
    }

    /* Handle shutdown override
     *
     * In general initiating mode changes during shutdown
     * makes little sense.
     *
     * Also, if developer mode is active, we want to keep it
     * working as long as possible for debugging purposes.
     *
     * DSME reports shutdown intent before we are going to
     * see user changes due to user session getting stopped.
     * Once that happens
     * -> ignore all changes and retain current mode
     */
    if( usbmoded_in_shutdown() ) {
        log_debug("in shutdown, retaining '%s' mode", current_mode);
        goto BAILOUT;
    }

    /* The rest of the mode selection logic must be subjected
     * to filtering based on device lock status, current user, etc
     */

    /* By default use whatever user has selected
     */
    if( use_mode(control_get_selected_mode()) ) {
        if( common_valid_mode(mode_to_use) ) {
            /* Mode does not exist
             * -> try setting */
            log_debug("mode '%s' is not valid", mode_to_use);
            use_mode(0);
        }
        else if( !usbmoded_is_mode_permitted(mode_to_use, current_user) ) {
            /* Mode is not allowed
             * -> try setting */
            log_debug("mode '%s' is not permitted", mode_to_use);
            use_mode(0);
        }
    }

    /* If user has not selected anything, apply setting value */
    if( !mode_to_use ) {
        /* If current user is not determined, assume that device is
         * booting up or in between two user sessions. Therefore we
         * either must use whatever is configured as global default
         * mode or let device lock to prevent the mode so that it can
         * be set again once the device is unlocked */
        uid_t uid = (current_user == UID_UNKNOWN) ? 0 : current_user;
        use_allocated_mode(config_get_mode_setting(uid));
    }

    /* In case of ASK and only one mode from which to select,
     * apply the only possibility available without prompting
     * user.
     */
    if( !g_strcmp0(mode_to_use, MODE_ASK) ) {
        if( current_user == UID_UNKNOWN ) {
            /* ASK is valid only when there is user
             * -> use fallback charging when user is not known */
            log_debug("mode '%s' is not applicable", mode_to_use);
            use_mode(MODE_CHARGING_FALLBACK);
        } else {
            // FIXME free() vs g_free() conflict
            gchar *available = common_get_mode_list(AVAILABLE_MODES_LIST, current_user);
            if( *available && !strchr(available, ',') ) {
                use_allocated_mode(available), available = 0;
            }
            g_free(available);
        }
    }

    /* After dealing with user selection and settings, check
     * that we have mode that user is permitted to activate.
     */
    if( !mode_to_use ) {
        /* Nothing selected -> silently choose fallback charging */
        use_mode(MODE_CHARGING_FALLBACK);
    }
    else if( !strcmp(mode_to_use, MODE_CHARGING_FALLBACK) ) {
        /* Fallback charging is not user selectable mode.
         * As it is still expected to occur here, we need to skip
         * the permission checks below to avoid logging noise.
         */
    }
    else if( !usbmoded_is_mode_permitted(mode_to_use, current_user) ) {
        log_debug("mode '%s' is not permitted", mode_to_use);
        use_mode(MODE_CHARGING_FALLBACK);
    }

    /* Handle user change without mode change
     *
     * For example in case of mtp mode: we must terminate ongoing
     * mtp session that exposes home directory of the previously
     * active user -> activating fallback charging takes care of that.
     *
     * Assumption is that if we ever hit this condition, it will be
     * followed by device lock state changes that will trigger exit
     * from fallback charging.
     */
    if( control_get_user_for_mode() != current_user ) {
        /* User did change */
        if( !g_strcmp0(current_mode, mode_to_use) ) {
            /* Mode to select did not change */
            if( !common_modename_is_static(mode_to_use) ) {
                /* Selected mode is dynamic
                 * -> redirect to fallback charging */
                log_debug("mode '%s' must be terminated", mode_to_use);
                use_mode(MODE_CHARGING_FALLBACK);
            }
        }
    }

    /* Blocking activation of dynamic modes
     *
     * Mode that is alreay active must be retained, but activating
     * new dynamic modes while e.g. device is locked is not allowed.
     */
    if( control_have_pending_user_change() || !usbmoded_can_export() ) {
        /* Device is locked / in ACT_DEAD / similar */
        if( !g_strcmp0(mode_to_use, MODE_ASK) ) {
            /* ASK is not valid while device is locked
             * -> redirect to fallback charging */
            log_debug("mode '%s' is not applicable", mode_to_use);
            use_mode(MODE_CHARGING_FALLBACK);
        }
        else if( g_strcmp0(current_mode, mode_to_use) ) {
            /* Mode to select did change */
            if( !common_modename_is_static(mode_to_use) ) {
                /* Selected mode is dynamic
                 * -> redirect to fallback charging */
                log_debug("mode '%s' is not applicable", mode_to_use);
                use_mode(MODE_CHARGING_FALLBACK);
            }
        }
    }

MODESET:
    /* If no mode was selected, opt for fallback charging */
    if( !mode_to_use )
        use_mode(MODE_CHARGING_FALLBACK);

    /* Activate the mode */
    log_debug("selected mode = %s", mode_to_use);
    control_set_usb_mode(mode_to_use);

    /* Forget client request once it can't be honored */
    if( g_strcmp0(control_get_selected_mode(), mode_to_use) )
        control_set_selected_mode(0);

BAILOUT:
    g_free(mode_to_free);
}

/** set the usb connection status
 *
 * @param cable_state CABLE_STATE_DISCONNECTED, ...
 */
void control_set_cable_state(cable_state_t cable_state)
{
    LOG_REGISTER_CONTEXT;

    cable_state_t prev = control_cable_state;
    control_cable_state = cable_state;

    if( control_cable_state == prev )
        goto EXIT;

    log_debug("control_cable_state: %s -> %s",
              cable_state_repr(prev),
              cable_state_repr(control_cable_state));

    control_rethink_usb_mode();

EXIT:
    return;
}

/** get the usb connection status
 *
 * @return CABLE_STATE_DISCONNECTED, ...
 */
cable_state_t control_get_cable_state(void)
{
    LOG_REGISTER_CONTEXT;

    return control_cable_state;
}

void control_clear_cable_state(void)
{
    LOG_REGISTER_CONTEXT;

    control_cable_state = CABLE_STATE_UNKNOWN;
}

/** Get if the cable (pc or charger) is connected or not
 *
 * @ return true if connected, false if disconnected
 */
bool control_get_connection_state(void)
{
    LOG_REGISTER_CONTEXT;

    bool connected = false;
    switch( control_get_cable_state() ) {
    case CABLE_STATE_CHARGER_CONNECTED:
    case CABLE_STATE_PC_CONNECTED:
        connected = true;
        break;
    default:
        break;
    }
    return connected;
}
