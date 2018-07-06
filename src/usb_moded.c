/**
 * @file usb_moded.c
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Copyright (C) 2012-2018 Jolla. All rights reserved.
 *
 * @author: Philippe De Swert <philippe.de-swert@nokia.com>
 * @author: Philippe De Swert <phdeswer@lumi.maa>
 * @author: Philippe De Swert <philippedeswert@gmail.com>
 * @author: Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author: Jonni Rainisto <jonni.rainisto@jollamobile.com>
 * @author: Pekka Lundstrom <pekka.lundstrom@jollamobile.com>
 * @author: Vesa Halttunen <vesa.halttunen@jollamobile.com>
 * @author: Simo Piiroinen <simo.piiroinen@jollamobile.com>
 * @author: Thomas Perl <thomas.perl@jolla.com>
 * @author: Matti Lehtimaki <matti.lehtimaki@gmail.com>
 * @author: Thomas Perl <m@thp.io>
 * @author: Martin Jones <martin.jones@jollamobile.com>
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

#include <getopt.h>
#include <stdio.h>

#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

#include <libkmod.h>

#ifdef SYSTEMD
# include <systemd/sd-daemon.h>
#endif

#include "usb_moded.h"
#include "usb_moded-modes.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded-udev.h"
#include "usb_moded-modules.h"
#include "usb_moded-log.h"
#include "usb_moded-devicelock.h"
#include "usb_moded-modesetting.h"
#include "usb_moded-modules.h"
#include "usb_moded-appsync.h"
#include "usb_moded-trigger.h"
#include "usb_moded-config.h"
#include "usb_moded-config-private.h"
#include "usb_moded-network.h"
#include "usb_moded-mac.h"
#include "usb_moded-android.h"
#include "usb_moded-configfs.h"
#include "usb_moded-systemd.h"

#ifdef MEEGOLOCK
# include "usb_moded-dsme.h"
#endif

/* ========================================================================= *
 * Constants
 * ========================================================================= */

/* Wakelogging is noisy, do not log it by default */
#ifndef  VERBOSE_WAKELOCKING
# define VERBOSE_WAKELOCKING 0
#endif

/** Default allowed cable detection delay
 *
 * To comply with USB standards, the delay should be
 * less than 2 seconds to ensure timely enumeration.
 *
 * Any value <= zero means no delay.
 */
#define CABLE_CONNECTION_DELAY_DEFAULT 0

/** Maximum allowed cable detection delay
 *
 * Must be shorter than initial probing delay expected by
 * dsme (currently 5 seconds) to avoid reboot loops in
 * act dead mode.
 *
 * And shorter than USB_MODED_SUSPEND_DELAY_DEFAULT_MS to
 * allow the timer to trigger also in display off scenarios.
 */

#define CABLE_CONNECTION_DELAY_MAXIMUM 4000

/* ========================================================================= *
 * Types
 * ========================================================================= */

/** A struct containing all the usb_moded info needed
 */
typedef struct usb_mode
{
    /** Connection status
     *
     * Access only via:
     * - usbmoded_get_connection_state()
     * - usbmoded_set_connection_state()
     */
    bool connected;

    /** Mount status, true for mounted -UNUSED atm- */
    bool mounted;

    /** Used to keep an active gadget for broken Android kernels */
    bool android_usb_broken;

    /** The logical mode name
     *
     * Full set of valid modes can occur here
     */
    char *internal_mode;

    /* The hardware mode name
     *
     * How the usb hardware has been configured.
     *
     * For example internal_mode=MODE_ASK gets
     * mapped to hardware_mode=MODE_CHARGING */
    char *hardware_mode;

    /* The external mode;
     *
     * What was the last mode signaled over D-Bus.
     */
    char *external_mode;

    /**< The module name for the specific mode */
    char *module;

    /**< Contains the mode data */
    struct mode_list_elem *data;
} usb_mode;

/** Mapping usb mode from internal to hardware/broadcast use */
typedef struct modemapping_t
{
    /** Any valid usb mode */
    const char *internal_mode;

    /** Mode to use for usb configuration, or NULL = internal */
    const char *hardware_mode;

    /** Mode to use for D-Bus broadcast, or NULL = internal */
    const char *external_mode;
} modemapping_t;

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- usbmoded -- */

static const char *usbmoded_map_mode_to_hardware(const char *internal_mode);
static const char *usbmoded_map_mode_to_external(const char *internal_mode);

// ----------------------------------------------------------------
void                   usbmoded_rethink_usb_charging_fallback(void);

static bool            usbmoded_switch_to_charging           (void);
static void            usbmoded_switch_to_mode               (const char *mode);

const char            *usbmoded_get_hardware_mode            (void);
static void            usbmoded_update_hardware_mode         (void);

const char            *usbmoded_get_external_mode            (void);
static void            usbmoded_update_external_mode         (void);

// from here and there
const char            *usbmoded_get_usb_mode                 (void);
void                   usbmoded_set_usb_mode                 (const char *internal_mode);

// from usbmoded_set_usb_connected()
//      usbmoded_set_charger_connected()
bool                   usbmoded_get_connection_state         (void);
static bool            usbmoded_set_connection_state         (bool state);

// from usbmoded_set_usb_connected()
static void            usbmoded_set_usb_disconnected_state_silent(void);
static void            usbmoded_set_usb_disconnected_state   (void);
void                   usbmoded_set_usb_connected_state      (void);

// from udev cable_state_changed()
void                   usbmoded_set_usb_connected            (bool connected);
void                   usbmoded_set_charger_connected        (bool state);

// ----------------------------------------------------------------
// internal movements

static void            usbmoded_set_cable_connection_delay   (int delay_ms);

static bool            usbmoded_mode_in_list                 (const char *mode, char *const *modes);
int                    usbmoded_valid_mode                   (const char *mode);
gchar                 *usbmoded_get_mode_list                (mode_list_type_t type);

const char            *usbmoded_get_usb_module               (void);
void                   usbmoded_set_usb_module               (const char *module);

struct mode_list_elem *usbmoded_get_usb_mode_data            (void);
void                   usbmoded_set_usb_mode_data            (struct mode_list_elem *data);

void                   usbmoded_send_supported_modes_signal  (void);
void                   usbmoded_send_available_modes_signal  (void);
void                   usbmoded_send_hidden_modes_signal     (void);
void                   usbmoded_send_whitelisted_modes_signal(void);

static void            usbmoded_write_to_sysfs_file          (const char *path, const char *text);
void                   usbmoded_acquire_wakelock             (const char *wakelock_name);
void                   usbmoded_release_wakelock             (const char *wakelock_name);

static gboolean        usbmoded_allow_suspend_timer_cb       (gpointer aptr);
void                   usbmoded_allow_suspend                (void);
void                   usbmoded_delay_suspend                (void);

bool                   usbmoded_init_done_p                  (void);
void                   usbmoded_set_init_done                (bool reached);
void                   usbmoded_probe_init_done              (void);

void                   usbmoded_exit_mainloop                (int exitcode);
static void            usbmoded_handle_signal                (int signum);

static void            usbmoded_init                         (void);
static void            usbmoded_cleanup                      (void);

int                    usbmoded_system_                      (const char *file, int line, const char *func, const char *command);
FILE                  *usbmoded_popen_                       (const char *file, int line, const char *func, const char *command, const char *type);
void                   usbmoded_usleep_                      (const char *file, int line, const char *func, useconds_t usec);

static void            usbmoded_usage                        (void);

/* -- sigpipe -- */

static gboolean sigpipe_read_signal_cb (GIOChannel *channel, GIOCondition condition, gpointer data);
static void     sigpipe_trap_signal_cb (int sig);
static bool     sigpipe_crate_pipe     (void);
static void     sigpipe_trap_signals   (void);
static bool     sigpipe_init           (void);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static int usb_moded_exitcode = EXIT_FAILURE;
static GMainLoop *usb_moded_mainloop = NULL;

bool usbmoded_rescue_mode = false;
static bool diag_mode = false;
static bool hw_fallback = false;
static bool android_broken_usb = false;
static bool android_ignore_udev_events = false;
static bool android_ignore_next_udev_disconnect_event = false;
#ifdef SYSTEMD
static bool systemd_notify = false;
#endif

/** Currently allowed cable detection delay
 */
int usbmoded_cable_connection_delay = CABLE_CONNECTION_DELAY_DEFAULT;

static struct usb_mode current_mode = {
    .connected = false,
    .mounted = false,
    .android_usb_broken = false,
    .internal_mode = NULL,
    .hardware_mode = NULL,
    .external_mode = NULL,
    .module = NULL,
    .data = NULL,
};

static GList *modelist = 0;

/** Path to init-done flag file */
static const char init_done_flagfile[] = "/run/systemd/boot-status/init-done";

/** cached init-done-reached state */
static bool init_done_reached = false;

/** Flag for: USB_MODED_WAKELOCK_STATE_CHANGE has been acquired */
static bool blocking_suspend = false;

/** Timer for releasing USB_MODED_WAKELOCK_STATE_CHANGE */
static guint allow_suspend_timer_id = 0;

/** Pipe fd for transferring signals to mainloop context */
static int sigpipe_fd = -1;

static const modemapping_t modemapping[] =
{
    {
        .internal_mode = MODE_UNDEFINED,
        .hardware_mode = MODE_CHARGING,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_ASK,
        .hardware_mode = MODE_CHARGING,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_MASS_STORAGE,
        .hardware_mode = 0,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_DEVELOPER,
        .hardware_mode = 0,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_MTP,
        .hardware_mode = 0,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_HOST,
        .hardware_mode = 0,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_CONNECTION_SHARING,
        .hardware_mode = 0,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_DIAG,
        .hardware_mode = 0,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_ADB,
        .hardware_mode = 0,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_PC_SUITE,
        .hardware_mode = 0,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_CHARGING,
        .hardware_mode = MODE_CHARGING,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_CHARGING_FALLBACK,
        .hardware_mode = MODE_CHARGING,
        .external_mode = MODE_ASK,
    },
    {
        .internal_mode = MODE_CHARGER,
        .hardware_mode = MODE_CHARGING,
        .external_mode = 0,
    },
    // sentinel
    {
        .internal_mode = 0,
        .hardware_mode = 0,
        .external_mode = 0,
    }
};

/* ========================================================================= *
 * Functions
 * ========================================================================= */

static const char *
usbmoded_map_mode_to_hardware(const char *internal_mode)
{
    const char *hardware_mode = 0;

    for( size_t i = 0; modemapping[i].internal_mode; ++i ) {
        if( strcmp(modemapping[i].internal_mode, internal_mode) )
            continue;
        hardware_mode = modemapping[i].hardware_mode;
        break;
    }
    return hardware_mode ?: internal_mode;
}

static const char *
usbmoded_map_mode_to_external(const char *internal_mode)
{
    const char *external_mode = 0;

    for( size_t i = 0; modemapping[i].internal_mode; ++i ) {
        if( strcmp(modemapping[i].internal_mode, internal_mode) )
            continue;
        external_mode = modemapping[i].external_mode;
        break;
    }
    return external_mode ?: internal_mode;
}

/** Check if we can/should leave charging fallback mode
 *
 * Called when device lock status, or device status (dsme)
 * changes.
 */
void
usbmoded_rethink_usb_charging_fallback(void)
{
    /* Cable must be connected */
    if( !usbmoded_get_connection_state() )
        goto EXIT;

    /* Suitable usb-mode mode selected */
    const char *usb_mode = usbmoded_get_usb_mode();

    if( strcmp(usb_mode, MODE_UNDEFINED) &&
        strcmp(usb_mode, MODE_CHARGING_FALLBACK) )
        goto EXIT;

#ifdef MEEGOLOCK
    /* If device locking is supported, the device must be in
     * unlocked state (or rescue mode must be active).
     */
    if( !devicelock_have_export_permission() && !usbmoded_rescue_mode ) {
        log_notice("device is locked; stay in %s", usb_mode);
        goto EXIT;
    }

    /* Device must be in USER state or in rescue mode
     */
    if( !dsme_in_user_state() && !usbmoded_rescue_mode ) {
        log_notice("device is not in USER mode; stay in %s", usb_mode);
        goto EXIT;
    }
#endif

    log_debug("attempt to leave %s", usb_mode);
    usbmoded_set_usb_connected_state();

EXIT:
    return;
}

static bool usbmoded_switch_to_charging(void)
{
    bool ack = true;

    modules_check_module_state(MODULE_MASS_STORAGE);

    /* for charging we use a fake file_storage
     * (blame USB certification for this insanity */

    usbmoded_set_usb_module(MODULE_MASS_STORAGE);

    /* MODULE_CHARGING has all the parameters defined,
     * so it will not match the g_file_storage rule in
     * modules_load_module */

    if( modules_load_module(MODULE_CHARGING) == 0 )
        goto SUCCESS;

    /* if charging mode setting did not succeed we
     * might be dealing with android */

    if (android_ignore_udev_events)
        android_ignore_next_udev_disconnect_event = true;

    usbmoded_set_usb_module(MODULE_NONE);

    if( android_set_charging_mode() )
        goto SUCCESS;

    if( configfs_set_charging_mode() )
        goto SUCCESS;

    log_err("switch to charging mode failed");

    ack = false;
SUCCESS:
    return ack;
}

static void usbmoded_switch_to_mode(const char *mode)
{
    /* set return to 1 to be sure to error out if no matching mode is found either */
    int ret=1;

    log_debug("Setting %s\n", mode);

    /* Mode mapping should mean we only see MODE_CHARGING here, but just
     * in case redirect fixed charging related things to charging ... */

    if( !strcmp(mode, MODE_CHARGING) ||
        !strcmp(mode, MODE_CHARGING_FALLBACK) ||
        !strcmp(mode, MODE_CHARGER) ||
        !strcmp(mode, MODE_UNDEFINED) ||
        !strcmp(mode, MODE_ASK)) {
        goto CHARGE;
    }

#ifdef MEEGOLOCK
    /* Potentially data exposing modes are allowed only when
     * device has been booted to user mode and unlocked.
     *
     * Except if rescue mode is still active.
     */
    bool can_export = (dsme_in_user_state() &&
                       devicelock_have_export_permission());

    if( !can_export && !usbmoded_rescue_mode ) {
        log_warning("Policy does not allow mode: %s", mode);
        goto CHARGE;
    }
#endif

    /* go through all the dynamic modes if the modelist exists*/
    for( GList *iter = modelist; iter; iter = g_list_next(iter) )
    {
        struct mode_list_elem *data = iter->data;
        if( strcmp(mode, data->mode_name) )
            continue;

        log_debug("Matching mode %s found.\n", mode);
        modules_check_module_state(data->mode_module);
        usbmoded_set_usb_module(data->mode_module);
        ret = modules_load_module(data->mode_module);

        /* set data before calling any of the dynamic mode functions
         * as they will use the usbmoded_get_usb_mode_data function */
        usbmoded_set_usb_mode_data(data);

        /* check if modules are ok before continuing */
        if( ret == 0 ) {
            if (android_ignore_udev_events) {
                android_ignore_next_udev_disconnect_event = true;
            }
            ret = modesetting_set_dynamic_mode();
            if( ret == 0 )
                goto SUCCESS;
        }
    }

    log_warning("mode setting failed, fall back to charging");

CHARGE:
    if( usbmoded_switch_to_charging() )
        goto SUCCESS;

    log_crit("failed to activate charging, all bets are off");

    /* FIXME: double check this error path */

    /* If we get here then usb_module loading failed,
     * no mode matched, and charging setup failed too.
     */

    usbmoded_set_usb_module(MODULE_NONE);
    mode = MODE_UNDEFINED;
    usbmoded_set_usb_mode_data(NULL);
    log_debug("mode setting failed or device disconnected, mode to set was = %s\n", mode);

SUCCESS:
    return;
}

const char *usbmoded_get_hardware_mode(void)
{
    return current_mode.hardware_mode ?: MODE_UNDEFINED;
}

static void usbmoded_update_hardware_mode(void)
{
    const char *internal_mode = usbmoded_get_usb_mode();
    const char *hardware_mode = usbmoded_map_mode_to_hardware(internal_mode);

    gchar *previous = current_mode.hardware_mode;
    if( !g_strcmp0(previous, hardware_mode) )
        goto EXIT;

    log_debug("hardware_mode: %s -> %s",
              previous, hardware_mode);

    current_mode.hardware_mode = g_strdup(hardware_mode);
    g_free(previous);

    // DO THE MODESWITCH

    usbmoded_switch_to_mode(current_mode.hardware_mode);

EXIT:
    return;
}

const char *usbmoded_get_external_mode(void)
{
    return current_mode.external_mode ?: MODE_UNDEFINED;
}

static void usbmoded_update_external_mode(void)
{
    const char *internal_mode = usbmoded_get_usb_mode();
    const char *external_mode = usbmoded_map_mode_to_external(internal_mode);

    gchar *previous = current_mode.external_mode;
    if( !g_strcmp0(previous, external_mode) )
        goto EXIT;

    log_debug("external_mode: %s -> %s",
              previous, external_mode);

    current_mode.external_mode = g_strdup(external_mode);
    g_free(previous);

    // DO THE DBUS BROADCAST

    umdbus_send_state_signal(current_mode.external_mode);

EXIT:
    return;
}

/** get the usb mode
 *
 * @return the currently set mode
 *
 */
const char * usbmoded_get_usb_mode(void)
{
    return current_mode.internal_mode;
}

/** set the usb mode
 *
 * @param mode The requested USB mode
 *
 */
void usbmoded_set_usb_mode(const char *internal_mode)
{
    gchar *previous = current_mode.internal_mode;
    if( !g_strcmp0(previous, internal_mode) )
        goto EXIT;

    log_debug("internal_mode: %s -> %s",
              previous, internal_mode);

    current_mode.internal_mode = g_strdup(internal_mode);
    g_free(previous);

    // PROPAGATE DOWN TO USB
    usbmoded_update_hardware_mode();

    // PROPAGATE UP DBUS
    usbmoded_update_external_mode();

EXIT:
    return;
}

/** Get if the cable (pc or charger) is connected or not
 *
 * @ return true if connected, false if disconnected
 */
bool usbmoded_get_connection_state(void)
{
    return current_mode.connected;
}

/** Set if the cable (pc or charger) is connected or not
 *
 * @param state true for connected, false for disconnected
 *
 * @return true if value changed, false otherwise
 */
static bool usbmoded_set_connection_state(bool state)
{
    bool changed = false;
    if( current_mode.connected != state ) {
        log_debug("current_mode.connected: %d -> %d",
                  current_mode.connected,  state);
        current_mode.connected = state;
        changed = true;
    }
    return changed;
}

/* set disconnected without sending signals. */
static void usbmoded_set_usb_disconnected_state_silent(void)
{
    if(!usbmoded_get_connection_state())
    {
        log_debug("Resetting connection data after HUP\n");
        /* unload modules and general cleanup if not charging */
        if(strcmp(usbmoded_get_usb_mode(), MODE_CHARGING) ||
           strcmp(usbmoded_get_usb_mode(), MODE_CHARGING_FALLBACK))
            modesetting_cleanup(usbmoded_get_usb_module());
        /* Nothing else as we do not need to do anything for cleaning up charging mode */
        modules_cleanup_module(usbmoded_get_usb_module());
        usbmoded_set_usb_mode(MODE_UNDEFINED);
    }
}

static void usbmoded_set_usb_disconnected_state(void)
{
    /* signal usb disconnected */
    umdbus_send_state_signal(USB_DISCONNECTED);

    /* unload modules and general cleanup if not charging */
    if( strcmp(usbmoded_get_usb_mode(), MODE_CHARGING) &&
        strcmp(usbmoded_get_usb_mode(), MODE_CHARGING_FALLBACK))
        modesetting_cleanup(usbmoded_get_usb_module());

    /* Nothing else as we do not need to do anything for cleaning up charging mode */
    modules_cleanup_module(usbmoded_get_usb_module());
    usbmoded_set_usb_mode(MODE_UNDEFINED);
}

/** set the chosen usb state
 *
 */
void usbmoded_set_usb_connected_state(void)
{
    char *mode_to_set = 0;
    bool can_export = true;

    if(usbmoded_rescue_mode)
    {
        log_debug("Entering rescue mode!\n");
        usbmoded_set_usb_mode(MODE_DEVELOPER);
        goto EXIT;
    }

    if(diag_mode)
    {
        log_debug("Entering diagnostic mode!\n");
        if(modelist)
        {
            GList *iter = modelist;
            struct mode_list_elem *data = iter->data;
            usbmoded_set_usb_mode(data->mode_name);
        }
        goto EXIT;
    }

    mode_to_set = config_get_mode_setting();

#ifdef MEEGOLOCK
    /* Check if we are allowed to export system contents 0 is unlocked.
     * We check also if the device is in user state or not.
     * If not we do not export anything. We presume ACT_DEAD charging
     */
    can_export = (devicelock_have_export_permission()
                  && dsme_in_user_state());
#endif

    if( mode_to_set && can_export )
    {
        /* This is safe to do here as the starting condition is
         * MODE_UNDEFINED, and having a devicelock being activated when
         * a mode is set will not interrupt it */
        if(!strcmp(mode_to_set, usbmoded_get_usb_mode()))
            goto EXIT;

        if (!strcmp(MODE_ASK, mode_to_set))
        {
            /* If charging mode is the only available selection, don't ask
             * just select it */
            gchar *available_modes = usbmoded_get_mode_list(AVAILABLE_MODES_LIST);
            if (!strcmp(MODE_CHARGING, available_modes)) {
                gchar *temp = mode_to_set;
                mode_to_set = available_modes;
                available_modes = temp;
            }
            g_free(available_modes);
        }

        if(!strcmp(MODE_ASK, mode_to_set))
        {
            /* send signal, mode will be set when the dialog service calls
             * the set_mode method call.
             */
            umdbus_send_state_signal(USB_CONNECTED_DIALOG_SHOW);

            /* in case there was nobody listening for the UI, they will know
             * that the UI is needed by requesting the current mode */
            usbmoded_set_usb_mode(MODE_ASK);
        }
        else
            usbmoded_set_usb_mode(mode_to_set);
    }
    else
    {
        /* config is corrupted or we do not have a mode configured, fallback to charging
         * We also fall back here in case the device is locked and we do not
         * export the system contents. Or if we are in acting dead mode.
         */
        usbmoded_set_usb_mode(MODE_CHARGING_FALLBACK);
    }
EXIT:
    free(mode_to_set);
}

/** set the usb connection status
 *
 * @param connected The connection status, true for connected
 *
 */
void usbmoded_set_usb_connected(bool connected)
{
    if( !connected && android_ignore_next_udev_disconnect_event ) {
        /* FIXME: udev event processing is changed so that
         *        disconnect notifications are not repeated
         *        so whatever this is supposed to do - it is
         *        broken.
         */
        android_ignore_next_udev_disconnect_event = false;
        goto EXIT;
    }

    /* Do not go through the routine if already connected to avoid
     * spurious load/unloads due to faulty signalling
     * NOKIA: careful with devicelock
     */
    if( !usbmoded_set_connection_state(connected) )
        goto EXIT;

    if( usbmoded_get_connection_state() ) {
        log_debug("usb connected\n");

        /* signal usb connected */
        umdbus_send_state_signal(USB_CONNECTED);
        usbmoded_set_usb_connected_state();
    }
    else {
        log_debug("usb disconnected\n");
        usbmoded_set_usb_disconnected_state();
        /* Some android kernels check for an active gadget to enable charging and
         * cable detection, meaning USB is completely dead unless we keep the gadget
         * active
         */
        if(current_mode.android_usb_broken) {
            android_set_charging_mode();
            configfs_set_charging_mode();
        }
        if (android_ignore_udev_events) {
            android_ignore_next_udev_disconnect_event = true;
        }
    }
EXIT:
    return;
}

/** set and track charger state
 *
 */
void usbmoded_set_charger_connected(bool state)
{
    /* check if charger is already connected
     * to avoid spamming dbus */
    if( !usbmoded_set_connection_state(state) )
        goto EXIT;

    if( usbmoded_get_connection_state() ) {
        umdbus_send_state_signal(CHARGER_CONNECTED);
        usbmoded_set_usb_mode(MODE_CHARGER);
    }
    else {
        umdbus_send_state_signal(CHARGER_DISCONNECTED);
        usbmoded_set_usb_mode(MODE_UNDEFINED);
    }
EXIT:
    return;
}

/** Helper for setting allowed cable detection delay
 *
 * Used for implementing --max-cable-delay=<ms> option.
 */
static void usbmoded_set_cable_connection_delay(int delay_ms)
{
    if( delay_ms < CABLE_CONNECTION_DELAY_MAXIMUM )
        usbmoded_cable_connection_delay = delay_ms;
    else {
        usbmoded_cable_connection_delay = CABLE_CONNECTION_DELAY_MAXIMUM;
        log_warning("using maximum connection delay: %d ms",
                    usbmoded_cable_connection_delay);
    }
}

/* check if a mode is in a list */
static bool usbmoded_mode_in_list(const char *mode, char * const *modes)
{
    int i;

    if (!modes)
        return false;

    for(i = 0; modes[i] != NULL; i++)
    {
        if(!strcmp(modes[i], mode))
            return true;
    }
    return false;
}

/** check if a given usb_mode exists
 *
 * @param mode The mode to look for
 * @return 0 if mode exists, 1 if it does not exist
 *
 */
int usbmoded_valid_mode(const char *mode)
{
    int valid = 1;
    /* MODE_ASK, MODE_CHARGER and MODE_CHARGING_FALLBACK are not modes that are settable seen their special 'internal' status
     * so we only check the modes that are announed outside. Only exception is the built in MODE_CHARGING */
    if(!strcmp(MODE_CHARGING, mode))
        valid = 0;
    else
    {
        char *whitelist;
        gchar **whitelist_split = NULL;

        whitelist = config_get_mode_whitelist();
        if (whitelist)
        {
            whitelist_split = g_strsplit(whitelist, ",", 0);
            g_free(whitelist);
        }

        /* check dynamic modes */
        if(modelist)
        {
            GList *iter;

            for( iter = modelist; iter; iter = g_list_next(iter) )
            {
                struct mode_list_elem *data = iter->data;
                if(!strcmp(mode, data->mode_name))
                {
                    if (!whitelist_split || usbmoded_mode_in_list(data->mode_name, whitelist_split))
                        valid = 0;
                    break;
                }
            }

            g_strfreev(whitelist_split);
        }
    }
    return valid;

}

/** make a list of all available usb modes
 *
 * @param type The type of list to return. Supported or available.
 * @return a comma-separated list of modes (MODE_ASK not included as it is not a real mode)
 *
 */
gchar *usbmoded_get_mode_list(mode_list_type_t type)
{
    GString *modelist_str;

    modelist_str = g_string_new(NULL);

    if(!diag_mode)
    {
        /* check dynamic modes */
        if(modelist)
        {
            GList *iter;
            char *hidden_modes_list, *whitelist;
            gchar **hidden_mode_split = NULL, **whitelist_split = NULL;

            hidden_modes_list = config_get_hidden_modes();
            if(hidden_modes_list)
            {
                hidden_mode_split = g_strsplit(hidden_modes_list, ",", 0);
                g_free(hidden_modes_list);
            }

            if (type == AVAILABLE_MODES_LIST)
            {
                whitelist = config_get_mode_whitelist();
                if (whitelist)
                {
                    whitelist_split = g_strsplit(whitelist, ",", 0);
                    g_free(whitelist);
                }
            }

            for( iter = modelist; iter; iter = g_list_next(iter) )
            {
                struct mode_list_elem *data = iter->data;

                /* skip items in the hidden list */
                if (usbmoded_mode_in_list(data->mode_name, hidden_mode_split))
                    continue;

                /* if there is a whitelist skip items not in the list */
                if (whitelist_split && !usbmoded_mode_in_list(data->mode_name, whitelist_split))
                    continue;

                modelist_str = g_string_append(modelist_str, data->mode_name);
                modelist_str = g_string_append(modelist_str, ", ");
            }

            g_strfreev(hidden_mode_split);
            g_strfreev(whitelist_split);
        }

        /* end with charging mode */
        g_string_append(modelist_str, MODE_CHARGING);
        return g_string_free(modelist_str, false);
    }
    else
    {
        /* diag mode. there is only one active mode */
        g_string_append(modelist_str, MODE_DIAG);
        return g_string_free(modelist_str, false);
    }
}

/** get the supposedly loaded module
 *
 * @return The name of the loaded module
 *
 */
const char * usbmoded_get_usb_module(void)
{
    return current_mode.module;
}

/** set the loaded module
 *
 * @param module The module name for the requested mode
 *
 */
void usbmoded_set_usb_module(const char *module)
{
    char *old = current_mode.module;
    current_mode.module = strdup(module);
    free(old);
}

/** get the usb mode data
 *
 * @return a pointer to the usb mode data
 *
 */
struct mode_list_elem * usbmoded_get_usb_mode_data(void)
{
    return current_mode.data;
}

/** set the mode_list_elem data
 *
 * @param data mode_list_element pointer
 *
 */
void usbmoded_set_usb_mode_data(struct mode_list_elem *data)
{
    current_mode.data = data;
}

/** Send supported modes signal
 */
void usbmoded_send_supported_modes_signal(void)
{
    gchar *mode_list = usbmoded_get_mode_list(SUPPORTED_MODES_LIST);
    umdbus_send_supported_modes_signal(mode_list);
    g_free(mode_list);
}

/** Send available modes signal
 */
void usbmoded_send_available_modes_signal(void)
{
    gchar *mode_list = usbmoded_get_mode_list(AVAILABLE_MODES_LIST);
    umdbus_send_available_modes_signal(mode_list);
    g_free(mode_list);
}

/** Send hidden modes signal
 */
void usbmoded_send_hidden_modes_signal(void)
{
    gchar *mode_list = config_get_hidden_modes();
    if(mode_list) {
        // TODO: cleared list not signaled?
        umdbus_send_hidden_modes_signal(mode_list);
        g_free(mode_list);
    }
}

/** Send whitelisted modes signal
 */
void usbmoded_send_whitelisted_modes_signal(void)
{
    gchar *mode_list = config_get_mode_whitelist();
    if(mode_list) {
        // TODO: cleared list not signaled?
        umdbus_send_whitelisted_modes_signal(mode_list);
        g_free(mode_list);
    }
}

/** Write string to already existing sysfs file
 *
 * Note: Attempts to write to nonexisting files are silently ignored.
 *
 * @param path Where to write
 * @param text What to write
 */
static void usbmoded_write_to_sysfs_file(const char *path, const char *text)
{
    int fd = -1;

    if (!path || !text)
        goto EXIT;

    if ((fd = open(path, O_WRONLY)) == -1) {
        if (errno != ENOENT) {
            log_warning("%s: open for writing failed: %m", path);
        }
        goto EXIT;
    }

    if (write(fd, text, strlen(text)) == -1) {
        log_warning("%s: write failed : %m", path);
        goto EXIT;
    }
EXIT:
    if (fd != -1)
        close(fd);
}

/** Acquire wakelock via sysfs
 *
 * Wakelock must be released via usbmoded_release_wakelock().
 *
 * Automatically terminating wakelock is used, so that we
 * do not block suspend  indefinately in case usb_moded
 * gets stuck or crashes.
 *
 * Note: The name should be unique within the system.
 *
 * @param wakelock_name Wake lock to be acquired
 */
void usbmoded_acquire_wakelock(const char *wakelock_name)
{
    char buff[256];
    snprintf(buff, sizeof buff, "%s %lld",
             wakelock_name,
             USB_MODED_SUSPEND_DELAY_MAXIMUM_MS * 1000000LL);
    usbmoded_write_to_sysfs_file("/sys/power/wake_lock", buff);

#if VERBOSE_WAKELOCKING
    log_debug("usbmoded_acquire_wakelock %s", wakelock_name);
#endif
}

/** Release wakelock via sysfs
 *
 * @param wakelock_name Wake lock to be released
 */
void usbmoded_release_wakelock(const char *wakelock_name)
{
#if VERBOSE_WAKELOCKING
    log_debug("usbmoded_release_wakelock %s", wakelock_name);
#endif

    usbmoded_write_to_sysfs_file("/sys/power/wake_unlock", wakelock_name);
}

/** Timer callback for releasing wakelock acquired via usbmoded_delay_suspend()
 *
 * @param aptr callback argument (not used)
 */
static gboolean usbmoded_allow_suspend_timer_cb(gpointer aptr)
{
    (void)aptr;

    allow_suspend_timer_id = 0;

    usbmoded_allow_suspend();

    return FALSE;
}

/** Release wakelock acquired via usbmoded_delay_suspend()
 *
 * Meant to be called on usb-moded exit so that wakelocks
 * are not left behind.
 */
void usbmoded_allow_suspend(void)
{
    if( allow_suspend_timer_id ) {
        g_source_remove(allow_suspend_timer_id),
            allow_suspend_timer_id = 0;
    }

    if( blocking_suspend ) {
        blocking_suspend = false;
        usbmoded_release_wakelock(USB_MODED_WAKELOCK_STATE_CHANGE);
    }
}

/** Block suspend briefly
 *
 * Meant to be called in situations usb activity might have woken
 * up the device (cable connect while display is off), or could
 * allow device to suspend (cable disconnect while display is off).
 *
 * Allows usb moded some time to finish asynchronous activity and
 * other processes to receive and process state changes broadcast
 * by usb-moded.
 */
void usbmoded_delay_suspend(void)
{
    /* Use of automatically terminating wakelocks also means we need
     * to renew the wakelock when extending the suspend delay. */
    usbmoded_acquire_wakelock(USB_MODED_WAKELOCK_STATE_CHANGE);

    blocking_suspend = true;

    if( allow_suspend_timer_id )
        g_source_remove(allow_suspend_timer_id);

    allow_suspend_timer_id =
        g_timeout_add(USB_MODED_SUSPEND_DELAY_DEFAULT_MS,
                      usbmoded_allow_suspend_timer_cb, 0);
}

/** Check if system has already been successfully booted up
 *
 * @return true if init-done has been reached, or false otherwise
 */
bool usbmoded_init_done_p(void)
{
    return init_done_reached;
}

/** Update cached init-done-reached state */
void usbmoded_set_init_done(bool reached)
{
    if( init_done_reached != reached ) {
        init_done_reached = reached;
        log_warning("init_done -> %s",
                    init_done_reached ? "reached" : "not reached");
    }
}

/** Check whether init-done flag file exists */
void usbmoded_probe_init_done(void)
{
    usbmoded_set_init_done(access(init_done_flagfile, F_OK) == 0);
}

/** Request orderly exit from mainloop
 */
void usbmoded_exit_mainloop(int exitcode)
{
    /* In case multiple exit request get done, retain the
     * highest exit code used. */
    if( usb_moded_exitcode < exitcode )
        usb_moded_exitcode = exitcode;

    /* If there is no mainloop to exit, terminate immediately */
    if( !usb_moded_mainloop )
    {
        log_warning("exit requested outside mainloop; exit(%d) now",
                    usb_moded_exitcode);
        exit(usb_moded_exitcode);
    }

    log_debug("stopping usb-moded mainloop");
    g_main_loop_quit(usb_moded_mainloop);
}

static void usbmoded_handle_signal(int signum)
{
    log_debug("handle signal: %s\n", strsignal(signum));

    if( signum == SIGTERM )
    {
        /* Assume: Stopped by init process */
        usbmoded_exit_mainloop(EXIT_SUCCESS);
    }
    else if( signum == SIGHUP )
    {
        /* clean up current mode */
        usbmoded_set_usb_disconnected_state_silent();

        /* clear existing data to be sure */
        usbmoded_set_usb_mode_data(NULL);

        /* free and read in modelist again */
        dynconfig_free_mode_list(modelist);

        modelist = dynconfig_read_mode_list(diag_mode);

        usbmoded_send_supported_modes_signal();
        usbmoded_send_available_modes_signal();
    }
    else
    {
        usbmoded_exit_mainloop(EXIT_FAILURE);
    }
}

/* set default values for usb_moded */
static void usbmoded_init(void)
{
    current_mode.connected = false;
    current_mode.mounted  = false;
    current_mode.internal_mode = strdup(MODE_UNDEFINED);
    current_mode.hardware_mode = NULL;
    current_mode.external_mode = NULL;
    current_mode.module = strdup(MODULE_NONE);

    if(android_broken_usb)
        current_mode.android_usb_broken = true;

    /* check config, merge or create if outdated */
    if(config_merge_conf_file() != 0)
    {
        log_err("Cannot create or find a valid configuration. Exiting.\n");
        exit(1);
    }

#ifdef APP_SYNC
    appsync_read_list(diag_mode);
#endif

    /* always read dyn modes even if appsync is not used */
    modelist = dynconfig_read_mode_list(diag_mode);

    if(config_check_trigger())
        trigger_init();

    /* Set-up mac address before kmod */
    if(access("/etc/modprobe.d/g_ether.conf", F_OK) != 0)
    {
        mac_generate_random_mac();
    }

    /* During bootup the sysfs control structures might
     * not be already in there when usb-moded starts up.
     * Retry few times unless init done is / gets reached
     * while waiting.
     */
    for( int i = 10; ; ) {
        if( configfs_init_values() )
            break;

        if( android_init_values() )
            break;

        /* Must probe /pollsince we're not yet running mainloop */
        usbmoded_probe_init_done();

        if( usbmoded_init_done_p() || --i <= 0 ) {
            if( !modules_init() )
                log_crit("No supported usb control mechanisms found");
            break;
        }

        usbmoded_msleep(1000);
    }

    /* TODO: add more start-up clean-up and init here if needed */
}

/** Release resources allocated by usbmoded_init()
 */
static void usbmoded_cleanup(void)
{
    /* Undo modules_init() */
    modules_quit();

    /* Undo trigger_init() */
    trigger_stop();

    /* Undo dynconfig_read_mode_list() */
    dynconfig_free_mode_list(modelist);

#ifdef APP_SYNC
    /* Undo appsync_read_list() */
    appsync_free_appsync_list();
#endif

    /* Release dynamic memory */
    free(current_mode.module),
        current_mode.module = 0;

    free(current_mode.internal_mode),
        current_mode.internal_mode = 0;

    free(current_mode.hardware_mode),
        current_mode.hardware_mode = 0;

    free(current_mode.external_mode),
        current_mode.external_mode = 0;
}

/** Wrapper to give visibility to blocking system() calls usb-moded is making
 */
int
usbmoded_system_(const char *file, int line, const char *func,
                 const char *command)
{
    log_debug("EXEC %s; from %s:%d: %s()",
              command, file, line, func);

    int rc = system(command);

    if( rc != 0 )
        log_warning("EXEC %s; exit code is %d", command, rc);

    return rc;
}

/** Wrapper to give visibility subprocesses usb-moded is invoking via popen()
 */
FILE *
usbmoded_popen_(const char *file, int line, const char *func,
                const char *command, const char *type)
{
    log_debug("EXEC %s; from %s:%d: %s()",
              command, file, line, func);

    return popen(command, type);
}

/** Wrapper to give visibility to blocking sleeps usb-moded is making
 */
void
usbmoded_usleep_(const char *file, int line, const char *func,
                 useconds_t usec)
{
    struct timespec ts = {
        .tv_sec  = (usec / 1000000),
        .tv_nsec = (usec % 1000000) * 1000
    };

    long ms = (ts.tv_nsec + 1000000 - 1) / 1000000;

    if( !ms ) {
        log_debug("SLEEP %ld seconds; from %s:%d: %s()",
                  (long)ts.tv_sec, file, line, func);
    }
    else if( ts.tv_sec ) {
        log_debug("SLEEP %ld.%03ld seconds; from %s:%d: %s()",
                  (long)ts.tv_sec, ms, file, line, func);
    }
    else {
        log_debug("SLEEP %ld milliseconds; from %s:%d: %s()",
                  ms, file, line, func);
    }

    do { } while( nanosleep(&ts, &ts) == -1 && errno != EINTR );
}

/** Glib io watch callback for reading signals from signal pipe
 *
 * @param channel   glib io channel
 * @param condition wakeup reason
 * @param data      user data (unused)
 *
 * @return TRUE to keep the iowatch, or FALSE to disable it
 */
static gboolean
sigpipe_read_signal_cb(GIOChannel *channel,
                       GIOCondition condition,
                       gpointer data)
{
    gboolean keep_watch = FALSE;

    int fd, rc, sig;

    (void)data;

    /* Should never happen, but we must disable the io watch
     * if the pipe fd still goes into unexpected state ... */
    if( condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL) )
        goto EXIT;

    if( (fd = g_io_channel_unix_get_fd(channel)) == -1 )
        goto EXIT;

    /* If the actual read fails, terminate with core dump */
    rc = TEMP_FAILURE_RETRY(read(fd, &sig, sizeof sig));
    if( rc != (int)sizeof sig )
        abort();

    /* handle the signal */
    usbmoded_handle_signal(sig);

    keep_watch = TRUE;

EXIT:
    if( !keep_watch )
        log_crit("disabled signal handler io watch\n");

    return keep_watch;
}

/** Async signal handler for writing signals to signal pipe
 *
 * @param sig the signal number to pass to mainloop via pipe
 */
static void
sigpipe_trap_signal_cb(int sig)
{
    /* NOTE: This function *MUST* be kept async-signal-safe! */

    static volatile int exit_tries = 0;

    int rc;

    /* Restore signal handler */
    signal(sig, sigpipe_trap_signal_cb);

    switch( sig )
    {
    case SIGINT:
    case SIGQUIT:
    case SIGTERM:
        /* If we receive multiple signals that should have
         * caused the process to exit, assume that mainloop
         * is stuck and terminate with core dump. */
        if( ++exit_tries >= 2 )
            abort();
        break;

    default:
        break;
    }

    /* Transfer the signal to mainloop via pipe ... */
    rc = TEMP_FAILURE_RETRY(write(sigpipe_fd, &sig, sizeof sig));

    /* ... or terminate with core dump in case of failures */
    if( rc != (int)sizeof sig )
        abort();
}

/** Create a pipe and io watch for handling signal from glib mainloop
 *
 * @return true on success, or false in case of errors
 */
static bool
sigpipe_crate_pipe(void)
{
    bool        res    = false;
    GIOChannel *chn    = 0;
    int         pfd[2] = { -1, -1 };

    if( pipe2(pfd, O_CLOEXEC) == -1 )
        goto EXIT;

    if( (chn = g_io_channel_unix_new(pfd[0])) == 0 )
        goto EXIT;

    if( !g_io_add_watch(chn, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                        sigpipe_read_signal_cb, 0) )
        goto EXIT;

    g_io_channel_set_close_on_unref(chn, true), pfd[0] = -1;
    sigpipe_fd = pfd[1], pfd[1] = -1;

    res = true;

EXIT:
    if( chn ) g_io_channel_unref(chn);
    if( pfd[0] != -1 ) close(pfd[0]);
    if( pfd[1] != -1 ) close(pfd[1]);

    return res;
}

/** Install async signal handlers
 */
static void
sigpipe_trap_signals(void)
{
    static const int sig[] =
    {
        SIGINT,
        SIGQUIT,
        SIGTERM,
        SIGHUP,
        -1
    };

    for( size_t i = 0; sig[i] != -1; ++i )
    {
        signal(sig[i], sigpipe_trap_signal_cb);
    }
}

/** Initialize signal trapping
 *
 * @return true on success, or false in case of errors
 */
static bool
sigpipe_init(void)
{
    bool success = false;

    if( !sigpipe_crate_pipe() )
        goto EXIT;

    sigpipe_trap_signals();

    success = true;

EXIT:
    return success;
}

/* ========================================================================= *
 * MAIN ENTRY
 * ========================================================================= */

/* Display usbmoded_usage information */
static void usbmoded_usage(void)
{
    fprintf(stdout,
            "Usage: usb_moded [OPTION]...\n"
            "USB mode daemon\n"
            "\n"
            "  -a,  --android_usb_broken\n"
            "      keep gadget active on broken android kernels\n"
            "  -i,  --android_usb_broken_udev_events\n"
            "      ignore incorrect disconnect events after mode setting\n"
            "  -f,  --fallback       \n"
            "      assume always connected\n"
            "  -s,  --force-syslog \n"
            "      log to syslog\n"
            "  -T,  --force-stderr \n"
            "      log to stderr\n"
            "  -l,  --log-line-info\n"
            "      log to stderr and show origin of logging\n"
            "  -D,  --debug  \n"
            "      turn on debug printing\n"
            "  -d,  --diag   \n"
            "      turn on diag mode\n"
            "  -h,  --help         \n"
            "      display this help and exit\n"
            "  -r,  --rescue         \n"
            "      rescue mode\n"
#ifdef SYSTEMD
            "  -n,  --systemd      \n"
            "      notify systemd when started up\n"
#endif
            "  -v,  --version      \n"
            "      output version information and exit\n"
            "  -m,  --max-cable-delay=<ms>\n"
            "      maximum delay before accepting cable connection\n"
            "  -b,  --android-bootup-function=<function>\n"
            "      Setup given function during bootup. Might be required\n"
            "      on some devices to make enumeration work on the 1st\n"
            "      cable connect.\n"
            "\n");
}

int main(int argc, char* argv[])
{
    int opt = 0, opt_idx = 0;

    struct option const options[] = {
        { "android_usb_broken", no_argument, 0, 'a' },
        { "android_usb_broken_udev_events", no_argument, 0, 'i' },
        { "fallback", no_argument, 0, 'd' },
        { "force-syslog", no_argument, 0, 's' },
        { "force-stderr", no_argument, 0, 'T' },
        { "log-line-info", no_argument, 0, 'l' },
        { "debug", no_argument, 0, 'D' },
        { "diag", no_argument, 0, 'd' },
        { "help", no_argument, 0, 'h' },
        { "rescue", no_argument, 0, 'r' },
        { "systemd", no_argument, 0, 'n' },
        { "version", no_argument, 0, 'v' },
        { "max-cable-delay", required_argument, 0, 'm' },
        { "android-bootup-function", required_argument, 0, 'b' },
        { 0, 0, 0, 0 }
    };

    log_init();
    log_set_name(basename(*argv));

    /* - - - - - - - - - - - - - - - - - - - *
     * OPTIONS
     * - - - - - - - - - - - - - - - - - - - */

    /* Parse the command-line options */
    while ((opt = getopt_long(argc, argv, "aifsTlDdhrnvm:b:", options, &opt_idx)) != -1)
    {
        switch (opt)
        {
        case 'a':
            android_broken_usb = true;
            break;
        case 'i':
            android_ignore_udev_events = true;
            break;
        case 'f':
            hw_fallback = true;
            break;
        case 's':
            log_set_type(LOG_TO_SYSLOG);
            break;

        case 'T':
            log_set_type(LOG_TO_STDERR);
            break;

        case 'D':
            log_set_level(LOG_DEBUG);
            break;

        case 'l':
            log_set_type(LOG_TO_STDERR);
            log_set_lineinfo(true);
            break;

        case 'd':
            diag_mode = true;
            break;

        case 'h':
            usbmoded_usage();
            exit(0);

        case 'r':
            usbmoded_rescue_mode = true;
            break;
#ifdef SYSTEMD
        case 'n':
            systemd_notify = true;
            break;
#endif
        case 'v':
            printf("USB mode daemon version: %s\n", VERSION);
            exit(0);

        case 'm':
            usbmoded_set_cable_connection_delay(strtol(optarg, 0, 0));
            break;

        case 'b':
            log_warning("Deprecated option: --android-bootup-function");
            break;

        default:
            usbmoded_usage();
            exit(0);
        }
    }

    fprintf(stderr, "usb_moded %s starting\n", VERSION);
    fflush(stderr);

    /* - - - - - - - - - - - - - - - - - - - *
     * INITIALIZE
     * - - - - - - - - - - - - - - - - - - - */

    /* silence usbmoded_system() calls */
    if( log_get_type() != LOG_TO_STDERR && log_get_level() != LOG_DEBUG )
    {
        if( !freopen("/dev/null", "a", stdout) ) {
            log_err("can't redirect stdout: %m");
        }
        if( !freopen("/dev/null", "a", stderr) ) {
            log_err("can't redirect stderr: %m");
        }
    }

#if !GLIB_CHECK_VERSION(2, 36, 0)
    g_type_init();
#endif
#if !GLIB_CHECK_VERSION(2, 31, 0)
    g_thread_init(NULL);
#endif

    /* Check if we are in mid-bootup */
    usbmoded_probe_init_done();

    /* Must be the 1st libdbus call that is made */
    dbus_threads_init_default();

    /* signal handling */
    if( !sigpipe_init() )
    {
        log_crit("signal handler init failed\n");
        goto EXIT;
    }

    if (usbmoded_rescue_mode && usbmoded_init_done_p())
    {
        usbmoded_rescue_mode = false;
        log_warning("init done passed; rescue mode ignored");
    }

    /* Connect to SystemBus */
    if( !umdbus_init_connection() )
    {
        log_crit("dbus systembus connection failed\n");
        goto EXIT;
    }

    /* Start DBus trackers that do async initialization
     * so that initial method calls are on the way while
     * we do initialization actions that might block. */

    /* DSME listener maintains in-user-mode state and is relevant
     * only when MEEGOLOCK configure option has been chosen. */
#ifdef MEEGOLOCK
    if( !dsme_listener_start() ) {
        log_crit("dsme tracking could not be started");
        goto EXIT;
    }
#endif
    /* Devicelock listener maintains devicelock state and is relevant
     * only when MEEGOLOCK configure option has been chosen. */
#ifdef MEEGOLOCK
    if( !devicelock_start_listener() ) {
        log_crit("devicelock tracking could not be started");
        goto EXIT;
    }
#endif

    /* Set daemon config/state data to sane state */
    modesetting_init();
    usbmoded_init();

    /* Allos making systemd control ipc */
    if( !systemd_control_start() ) {
        log_crit("systemd control could not be started");
        goto EXIT;
    }

    /* If usb-moded happens to crash, it could leave appsync processes
     * running. To make sure things are in the order expected by usb-moded
     * force stopping of appsync processes during usb-moded startup.
     *
     * The exception is: When usb-moded starts as a part of bootup. Then
     * we can be relatively sure that usb-moded has not been running yet
     * and therefore no appsync processes have been started and we can
     * skip the blocking ipc required to stop the appsync systemd units. */
#ifdef APP_SYNC
    if( usbmoded_init_done_p() )
    {
        log_warning("usb-moded started after init-done; "
                    "forcing appsync stop");
        appsync_stop(true);
    }
#endif

    /* Claim D-Bus service name before proceeding with things that
     * could result in dbus signals from usb-moded interfaces to
     * be broadcast */
    if( !umdbus_init_service() )
    {
        log_crit("usb-moded dbus service init failed\n");
        goto EXIT;
    }

    /* Initialize udev listener. Can cause mode changes.
     *
     * Failing here is allowed if '--fallback' commandline option is used. */
    if( !umudev_init() && !hw_fallback )
    {
        log_crit("hwal init failed\n");
        goto EXIT;
    }

    /* Broadcast supported / hidden modes */
    // TODO: should this happen before umudev_init()?
    usbmoded_send_supported_modes_signal();
    usbmoded_send_available_modes_signal();
    usbmoded_send_hidden_modes_signal();
    usbmoded_send_whitelisted_modes_signal();

    /* Act on '--fallback' commandline option */
    if(hw_fallback)
    {
        log_warning("Forcing USB state to connected always. ASK mode non functional!\n");
        /* Since there will be no disconnect signals coming from hw the state should not change */
        usbmoded_set_usb_connected(true);
    }

    /* - - - - - - - - - - - - - - - - - - - *
     * EXECUTE
     * - - - - - - - - - - - - - - - - - - - */

    /* Tell systemd that we have started up */
#ifdef SYSTEMD
    if( systemd_notify )
    {
        log_debug("notifying systemd\n");
        sd_notify(0, "READY=1");
    }
#endif

    /* init succesful, run main loop */
    usb_moded_exitcode = EXIT_SUCCESS;
    usb_moded_mainloop = g_main_loop_new(NULL, FALSE);

    log_debug("enter usb-moded mainloop");
    g_main_loop_run(usb_moded_mainloop);
    log_debug("leave usb-moded mainloop");

    g_main_loop_unref(usb_moded_mainloop),
        usb_moded_mainloop = 0;

    /* - - - - - - - - - - - - - - - - - - - *
     * CLEANUP
     * - - - - - - - - - - - - - - - - - - - */
EXIT:
    /* Detach from SystemBus. Components that hold reference to the
     * shared bus connection can still perform cleanup tasks, but new
     * references can't be obtained anymore and usb-moded method call
     * processing no longer occurs. */
    umdbus_cleanup();

    /* Stop appsync processes that have been started by usb-moded */
#ifdef APP_SYNC
    appsync_stop(false);
#endif

    /* Deny making systemd control ipc */
    systemd_control_stop();

    /* Stop tracking devicelock status */
#ifdef MEEGOLOCK
    devicelock_stop_listener();
#endif
    /* Stop tracking device state */
#ifdef MEEGOLOCK
    dsme_listener_stop();
#endif

    /* Stop udev listener */
    umudev_quit();

    /* Release dynamically allocated config/state data */
    usbmoded_cleanup();
    modesetting_quit();

    /* Detach from SessionBus connection used for APP_SYNC_DBUS.
     *
     * Can be handled separately from SystemBus side wind down. */
#ifdef APP_SYNC
# ifdef APP_SYNC_DBUS
    dbusappsync_cleanup();
# endif
#endif

    /* Must be done just before exit to make sure no more wakelocks
     * are taken and left behind on exit path */
    usbmoded_allow_suspend();

    log_debug("usb-moded return from main, with exit code %d",
              usb_moded_exitcode);
    return usb_moded_exitcode;
}
