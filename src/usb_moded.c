/**
 * @file usb_moded.c
 *
 * Copyright (c) 2010 Nokia Corporation. All rights reserved.
 * Copyright (c) 2012 - 2021 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Philippe De Swert <phdeswer@lumi.maa>
 * @author Philippe De Swert <philippedeswert@gmail.com>
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author Jonni Rainisto <jonni.rainisto@jollamobile.com>
 * @author Pekka Lundstrom <pekka.lundstrom@jollamobile.com>
 * @author Vesa Halttunen <vesa.halttunen@jollamobile.com>
 * @author Simo Piiroinen <simo.piiroinen@jollamobile.com>
 * @author Thomas Perl <thomas.perl@jolla.com>
 * @author Matti Lehtimaki <matti.lehtimaki@gmail.com>
 * @author Thomas Perl <m@thp.io>
 * @author Martin Jones <martin.jones@jollamobile.com>
 * @author Andrew den Exter <andrew.den.exter@jolla.com>
 * @author Andrew den Exter <andrew.den.exter@jollamobile.com>
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

#include "usb_moded.h"

#include "usb_moded-android.h"
#include "usb_moded-appsync.h"
#include "usb_moded-config-private.h"
#include "usb_moded-configfs.h"
#include "usb_moded-control.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded-devicelock.h"
#include "usb_moded-log.h"
#include "usb_moded-mac.h"
#include "usb_moded-modesetting.h"
#include "usb_moded-modules.h"
#include "usb_moded-sigpipe.h"
#include "usb_moded-systemd.h"
#include "usb_moded-trigger.h"
#include "usb_moded-udev.h"
#include "usb_moded-worker.h"
#include "usb_moded-modes.h"

#ifdef MEEGOLOCK
# include "usb_moded-dsme.h"
# include "usb_moded-user.h"
#endif

#include <getopt.h>

#ifdef SAILFISH_ACCESS_CONTROL
# include <sailfishaccesscontrol.h>
#endif

#ifdef SYSTEMD
# include <systemd/sd-daemon.h>
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
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * USBMODED
 * ------------------------------------------------------------------------- */

GList            *usbmoded_get_modelist              (void);
void              usbmoded_load_modelist             (void);
void              usbmoded_free_modelist             (void);
const modedata_t *usbmoded_get_modedata              (const char *modename);
modedata_t       *usbmoded_dup_modedata              (const char *modename);
bool              usbmoded_get_rescue_mode           (void);
void              usbmoded_set_rescue_mode           (bool rescue_mode);
bool              usbmoded_get_diag_mode             (void);
void              usbmoded_set_diag_mode             (bool diag_mode);
bool              usbmoded_is_mode_permitted         (const char *modename, uid_t uid);
void              usbmoded_set_cable_connection_delay(int delay_ms);
int               usbmoded_get_cable_connection_delay(void);
static gboolean   usbmoded_allow_suspend_timer_cb    (gpointer aptr);
void              usbmoded_allow_suspend             (void);
void              usbmoded_delay_suspend             (void);
bool              usbmoded_in_usermode               (void);
bool              usbmoded_in_shutdown               (void);
uid_t             usbmoded_get_current_user          (void);
bool              usbmoded_can_export                (void);
bool              usbmoded_init_done_p               (void);
void              usbmoded_set_init_done             (bool reached);
void              usbmoded_probe_init_done           (void);
void              usbmoded_exit_mainloop             (int exitcode);
void              usbmoded_handle_signal             (int signum);
static bool       usbmoded_init                      (void);
static void       usbmoded_cleanup                   (void);
static void       usbmoded_usage                     (void);
static void       usbmoded_parse_options             (int argc, char *argv[]);

/* ------------------------------------------------------------------------- *
 * MAIN
 * ------------------------------------------------------------------------- */

int main(int argc, char *argv[]);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static int        usbmoded_exitcode       = EXIT_FAILURE;
static GMainLoop *usbmoded_mainloop       = NULL;

static bool       usbmoded_hw_fallback    = false;
#ifdef SYSTEMD
static bool       usbmoded_systemd_notify = false;
#endif
static bool       usbmoded_auto_exit      = false;

static pthread_mutex_t  usbmoded_mutex = PTHREAD_MUTEX_INITIALIZER;

#define USBMODED_LOCKED_ENTER do {\
    if( pthread_mutex_lock(&usbmoded_mutex) != 0 ) { \
        log_crit("USBMODED LOCK FAILED");\
        _exit(EXIT_FAILURE);\
    }\
}while(0)

#define USBMODED_LOCKED_LEAVE do {\
    if( pthread_mutex_unlock(&usbmoded_mutex) != 0 ) { \
        log_crit("USBMODED UNLOCK FAILED");\
        _exit(EXIT_FAILURE);\
    }\
}while(0)

/* ========================================================================= *
 * Functions
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * MODELIST
 * ------------------------------------------------------------------------- */

/** List of mode data items read from configuration files
 *
 * Note: Worker thread should access this only via #usbmoded_dup_modedata().
 */
static GList *usbmoded_modelist = 0;

/** Get list of dynamic mode data items
 *
 * Note: This function should be called only from the main thread.
 *
 * @returns List of mode data objects, or NULL
 */
GList *
usbmoded_get_modelist(void)
{
    LOG_REGISTER_CONTEXT;

    return usbmoded_modelist;
}

/** Load dynamic mode data items
 *
 * Note: This function should be called only from the main thread.
 */
void
usbmoded_load_modelist(void)
{
    LOG_REGISTER_CONTEXT;

    USBMODED_LOCKED_ENTER;

    if( !usbmoded_modelist ) {
        log_notice("load modelist");
        usbmoded_modelist = modelist_load(usbmoded_get_diag_mode());
    }

    USBMODED_LOCKED_LEAVE;
}

/** Free dynamic mode data items
 *
 * Note: This function should be called only from the main thread.
 */
void
usbmoded_free_modelist(void)
{
    LOG_REGISTER_CONTEXT;

    USBMODED_LOCKED_ENTER;

    if( usbmoded_modelist ) {
        log_notice("free modelist");
        modelist_free(usbmoded_modelist),
            usbmoded_modelist = 0;
    }

    USBMODED_LOCKED_LEAVE;
}

/** Lookup dynamic mode data by name
 *
 * Note: This function should be called only from the main thread.
 *
 * @param modename  Name of mode to lookup
 *
 * @return Mode data object, or NULL
 */
const modedata_t *
usbmoded_get_modedata(const char *modename)
{
    LOG_REGISTER_CONTEXT;

    modedata_t *modedata = 0;

    for( GList *iter = usbmoded_get_modelist(); iter; iter = g_list_next(iter) ) {
        modedata_t *data = iter->data;
        if( !g_strcmp0(data->mode_name, modename) ) {
            modedata = data;
            break;
        }
    }
    return modedata;
}

/** Lookup and clone dynamic mode data by name
 *
 * Note: This function is safe to call from worker thread too.
 *
 * Caller must release the returned object via #modedata_free().
 *
 * @param modename  Name of mode to lookup
 *
 * @return Mode data object, or NULL
 */
modedata_t *
usbmoded_dup_modedata(const char *modename)
{
    LOG_REGISTER_CONTEXT;

    USBMODED_LOCKED_ENTER;

    modedata_t *modedata = modedata_copy(usbmoded_get_modedata(modename));

    USBMODED_LOCKED_LEAVE;

    return modedata;
}

/* ------------------------------------------------------------------------- *
 * RESCUE_MODE
 * ------------------------------------------------------------------------- */

/** Rescue mode flag
 *
 * When enabled, usb-moded allows developer_mode etc when device is
 * booted up with cable connected without requiring device unlock.
 * Which can be useful if UI for some reason does not come up.
 */
static bool usbmoded_rescue_mode = false;

bool usbmoded_get_rescue_mode(void)
{
    LOG_REGISTER_CONTEXT;

    return usbmoded_rescue_mode;
}

void usbmoded_set_rescue_mode(bool rescue_mode)
{
    LOG_REGISTER_CONTEXT;

    if( usbmoded_rescue_mode != rescue_mode ) {
        log_info("rescue_mode: %d -> %d",  usbmoded_rescue_mode, rescue_mode);
        usbmoded_rescue_mode = rescue_mode;
    }
}

/* ------------------------------------------------------------------------- *
 * DIAG_MODE
 * ------------------------------------------------------------------------- */

/** Diagnostic mode active
 *
 * In diag mode usb-moded uses separate mode configuration which
 * should have exactly one mode defined / available.
 */
static int usbmoded_diag_mode = -1;

bool usbmoded_get_diag_mode(void)
{
    LOG_REGISTER_CONTEXT;

    if( usbmoded_diag_mode == -1 ) {
        usbmoded_diag_mode = false;
        log_info("diag_mode: locked to %d", usbmoded_diag_mode);
    }

    return usbmoded_diag_mode;
}

void usbmoded_set_diag_mode(bool diag_mode)
{
    LOG_REGISTER_CONTEXT;

    if( usbmoded_diag_mode != diag_mode ) {
        if( usbmoded_diag_mode == -1 ) {
            usbmoded_diag_mode = diag_mode;
            log_info("diag_mode: set to %d", usbmoded_diag_mode);
        }
        else {
            log_err("dig_mode: already locked to %d", usbmoded_diag_mode);
        }
    }
}

/* ------------------------------------------------------------------------- *
 * ACCESS_CHECKS
 * ------------------------------------------------------------------------- */

bool usbmoded_is_mode_permitted(const char *modename, uid_t uid)
{
#ifdef SAILFISH_ACCESS_CONTROL
    LOG_REGISTER_CONTEXT;

    bool        allowed = true;
    modedata_t *data = 0;
    char       *group = 0;

    /* all modes are allowed for root */
    if( uid == 0 )
        goto EXIT;

    /* non-existing special value, deny everything */
    if( uid == UID_UNKNOWN ) {
        allowed = false;
        goto EXIT;
    }

    /* non-dynamic modes are allowed for all */
    if( !(data = usbmoded_dup_modedata(modename)) )
        goto EXIT;

    /* dynamic modes are allowed based on group,
     * which defaults to sailfish-system meaning device owner only */
    group = config_get_group_for_mode(modename);
    allowed = sailfish_access_control_hasgroup(uid, group);

EXIT:

    g_free(group);
    modedata_free(data);

    return allowed;

#else
    return true;

#endif
}

/* ------------------------------------------------------------------------- *
 * CABLE_CONNECT_DELAY
 * ------------------------------------------------------------------------- */

/** PC connection delay
 *
 * Slow cable insert / similar physical issues can lead to a charger
 * getting initially recognized as a pc connection. This defines how
 * long we should wait and see if pc connection gets corrected to a
 * charger kind.
 */
static int usbmoded_cable_connection_delay = CABLE_CONNECTION_DELAY_DEFAULT;

/** Helper for setting allowed cable detection delay
 *
 * Used for implementing --max-cable-delay=delay_ms option.
 */
void
usbmoded_set_cable_connection_delay(int delay_ms)
{
    LOG_REGISTER_CONTEXT;

    if( delay_ms > CABLE_CONNECTION_DELAY_MAXIMUM )
        delay_ms = CABLE_CONNECTION_DELAY_MAXIMUM;
    if( delay_ms < 0 )
        delay_ms = 0;

    if( usbmoded_cable_connection_delay != delay_ms ) {
        log_info("cable_connection_delay: %d -> %d",
                 usbmoded_cable_connection_delay,
                 delay_ms);
        usbmoded_cable_connection_delay = delay_ms;
    }
}

/** Helper for getting allowed cable detection delay
 */
int
usbmoded_get_cable_connection_delay(void)
{
    LOG_REGISTER_CONTEXT;

    return usbmoded_cable_connection_delay;
}

/* ------------------------------------------------------------------------- *
 * SUSPEND_BLOCKING
 * ------------------------------------------------------------------------- */

/** Flag for: USB_MODED_WAKELOCK_STATE_CHANGE has been acquired */
static bool usbmoded_blocking_suspend = false;

/** Timer for releasing USB_MODED_WAKELOCK_STATE_CHANGE */
static guint usbmoded_allow_suspend_timer_id = 0;

/** Timer callback for releasing wakelock acquired via usbmoded_delay_suspend()
 *
 * @param aptr callback argument (not used)
 */
static gboolean usbmoded_allow_suspend_timer_cb(gpointer aptr)
{
    LOG_REGISTER_CONTEXT;

    (void)aptr;

    usbmoded_allow_suspend_timer_id = 0;

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
    LOG_REGISTER_CONTEXT;

    if( usbmoded_allow_suspend_timer_id ) {
        g_source_remove(usbmoded_allow_suspend_timer_id),
            usbmoded_allow_suspend_timer_id = 0;
    }

    if( usbmoded_blocking_suspend ) {
        usbmoded_blocking_suspend = false;
        common_release_wakelock(USB_MODED_WAKELOCK_STATE_CHANGE);
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
    LOG_REGISTER_CONTEXT;

    /* Use of automatically terminating wakelocks also means we need
     * to renew the wakelock when extending the suspend delay. */
    common_acquire_wakelock(USB_MODED_WAKELOCK_STATE_CHANGE);

    usbmoded_blocking_suspend = true;

    if( usbmoded_allow_suspend_timer_id )
        g_source_remove(usbmoded_allow_suspend_timer_id);

    usbmoded_allow_suspend_timer_id =
        g_timeout_add(USB_MODED_SUSPEND_DELAY_DEFAULT_MS,
                      usbmoded_allow_suspend_timer_cb, 0);
}

/* ------------------------------------------------------------------------- *
 * DEVICE_STATE
 * ------------------------------------------------------------------------- */

/** Checks if the device is in USER state
 *
 * @return true if device is in USER state, false otherwise
 */
bool
usbmoded_in_usermode(void)
{
    LOG_REGISTER_CONTEXT;

#ifdef MEEGOLOCK
    return dsme_state_is_user();
#else
    return true;
#endif
}

/** Checks if the device is shutting down
 *
 * @return true if device is in SHUTDOWN or REBOOT state, false otherwise
 */
bool
usbmoded_in_shutdown(void)
{
    LOG_REGISTER_CONTEXT;

#ifdef MEEGOLOCK
    return dsme_state_is_shutdown();
#else
    return false;
#endif
}

/* ------------------------------------------------------------------------- *
 * CURRENT_USER
 * ------------------------------------------------------------------------- */

/** Return current user id
 *
 * If MEEGOLOCK and SAILFISH_ACCESS_CONTROL are selected,
 * returns uid of user session at seat0 / UID_UNKNOWN.
 *
 * Otherwise always returns 0 (root user).
 */
uid_t
usbmoded_get_current_user(void)
{
    LOG_REGISTER_CONTEXT;

#ifdef MEEGOLOCK
    return user_get_current_user();
#else
    return 0;
#endif
}

/* ------------------------------------------------------------------------- *
 * CAN_EXPORT
 * ------------------------------------------------------------------------- */

/** Check if exposing device data is currently allowed
 *
 * @return true exposing data is ok, or false otherwise
 */
bool usbmoded_can_export(void)
{
    LOG_REGISTER_CONTEXT;

  bool can_export = true;

#ifdef MEEGOLOCK
    /* Modes that potentially expose data are allowed only when
     * device is running in user mode and device is unlocked */
    can_export = (usbmoded_in_usermode() &&
                  devicelock_have_export_permission());

    /* Having bootup rescue mode active is an exception */
    if( usbmoded_get_rescue_mode() )
        can_export = true;
#endif

    return can_export;
}

/* ------------------------------------------------------------------------- *
 * INIT_DONE
 * ------------------------------------------------------------------------- */

/** Path to init-done flag file */
static const char usbmoded_init_done_flagfile[] = "/run/systemd/boot-status/init-done";

/** cached init-done-reached state */
static bool usbmoded_init_done_reached = false;

/** Check if system has already been successfully booted up
 *
 * @return true if init-done has been reached, or false otherwise
 */
bool usbmoded_init_done_p(void)
{
    LOG_REGISTER_CONTEXT;

    return usbmoded_init_done_reached;
}

/** Update cached init-done-reached state */
void usbmoded_set_init_done(bool reached)
{
    LOG_REGISTER_CONTEXT;

    if( usbmoded_init_done_reached != reached ) {
        usbmoded_init_done_reached = reached;
        log_warning("init_done -> %s",
                    usbmoded_init_done_reached ? "reached" : "not reached");

        /* Auto-disable rescue mode when bootup is finished */
        if( usbmoded_init_done_reached )
            usbmoded_set_rescue_mode(false);

        control_init_done_changed();
    }
}

/** Check whether init-done flag file exists */
void usbmoded_probe_init_done(void)
{
    LOG_REGISTER_CONTEXT;

    usbmoded_set_init_done(access(usbmoded_init_done_flagfile, F_OK) == 0);
}

/* ------------------------------------------------------------------------- *
 * MAINLOOP
 * ------------------------------------------------------------------------- */

/** Request orderly exit from mainloop
 */
void usbmoded_exit_mainloop(int exitcode)
{
    LOG_REGISTER_CONTEXT;

    /* In case multiple exit request get done, retain the
     * highest exit code used. */
    if( usbmoded_exitcode < exitcode )
        usbmoded_exitcode = exitcode;

    /* If there is no mainloop to exit, terminate immediately */
    if( !usbmoded_mainloop )
    {
        log_warning("exit requested outside mainloop; exit(%d) now",
                    usbmoded_exitcode);
        exit(usbmoded_exitcode);
    }

    log_debug("stopping usb-moded mainloop");
    g_main_loop_quit(usbmoded_mainloop);
}

void usbmoded_handle_signal(int signum)
{
    LOG_REGISTER_CONTEXT;

    log_debug("handle signal: %s\n", strsignal(signum));

    if( signum == SIGTERM )
    {
        /* Assume: Stopped by init process */
        usbmoded_exit_mainloop(EXIT_SUCCESS);
    }
    else if( signum == SIGHUP )
    {
        /* Reload mode list
         *
         * Note that copy of mode data related to the current
         * mode is stored separately and that copy is used
         * when making exit from current mode.
         */
        log_debug("reloading dynamic mode configuration");
        usbmoded_free_modelist();
        usbmoded_load_modelist();

        /* Reload appsync configuration files
         *
         * Updated configuration is loaded and set aside.
         *
         * Switch happens when applications started based
         * on currently active configuration have been
         * stopped.
         */
#ifdef APP_SYNC
        log_debug("reloading appsync configuration");
        appsync_load_configuration();
#endif
        /* If default mode selection became invalid,
         * revert setting to "ask" */
        uid_t current_user = usbmoded_get_current_user();
        gchar *config = config_get_mode_setting(current_user);
        if( g_strcmp0(config, MODE_ASK) &&
            common_valid_mode(config) ) {
            log_warning("default mode '%s' is not valid, reset to '%s'",
                        config, MODE_ASK);
            config_set_mode_setting(MODE_ASK, current_user);
        }
        else {
            log_debug("default mode '%s' is still valid", config);
        }
        g_free(config);

        /* If current mode became invalid, select appropriate mode.
         *
         * Use target mode so that we catch also situations where
         * we are making transition to invalid state.
         */
        const char *current = control_get_target_mode();
        if( common_modename_is_internal(current) ) {
            /* Internal modes are not affected by configuration
             * file changes - no changes required. */
            log_debug("current mode '%s' is internal", current);
        }
        else if( common_valid_mode(current) ) {
            /* Dynamic mode that is no longer valid - choose
             * something else. */
            log_warning("current mode '%s' is not valid, re-evaluating",
                        current);
            control_settings_changed();
        }
        else {
            /* Dynamic mode that is still valid - do nothing.
             *
             * Note: While the mode details /might/ have changed,
             * skipping immediate usb reprogramming is assumed to
             * be less harmful than potentially cutting developer
             * mode connection during upgrade, etc. */
            log_debug("current mode '%s' is still valid", current);
        }

        /* Signal availability */
        log_debug("broadcast mode availability lists");
        common_send_supported_modes_signal();
        common_send_available_modes_signal();
    }
    else
    {
        usbmoded_exit_mainloop(EXIT_FAILURE);
    }
}

/* Prepare usb-moded for running the mainloop */
static bool usbmoded_init(void)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    /* Check if we are in mid-bootup */
    usbmoded_probe_init_done();

    if( !worker_init() ) {
        log_crit("worker thread init failed");
      goto EXIT;
    }

    if( !sigpipe_init() ) {
        log_crit("signal handler init failed");
        goto EXIT;
    }

    if( usbmoded_get_rescue_mode() && usbmoded_init_done_p() ) {
        usbmoded_set_rescue_mode(false);
        log_warning("init done passed; rescue mode ignored");
    }

    /* Connect to SystemBus */
    if( !umdbus_init_connection() ) {
        log_crit("dbus systembus connection failed");
        goto EXIT;
    }

    /* Start DBus trackers that do async initialization
     * so that initial method calls are on the way while
     * we do initialization actions that might block. */

    /* DSME listener maintains in-user-mode state and is relevant
     * only when MEEGOLOCK configure option has been chosen. */
#ifdef MEEGOLOCK
    if( !dsme_start_listener() ) {
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

    /* check config, merge or create if outdated */
    if( !config_init() ) {
        log_crit("Cannot create or find a valid configuration");
        goto EXIT;
    }

#ifdef APP_SYNC
    appsync_load_configuration();
#endif

    /* always read dyn modes even if appsync is not used */
    usbmoded_load_modelist();

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
     *
     * Note that waiting here delays also systemd notification
     * -> changes in wait time might require adjustemnts to
     *    startup timeout value in usb-moded.service file.
     */
    for( int i = 10; ; ) {
        if( configfs_init() )
            break;

        if( android_init() )
            break;

        /* Must probe / poll since we're not yet running mainloop */
        usbmoded_probe_init_done();

        if( usbmoded_init_done_p() || --i <= 0 ) {
            if( !modules_init() )
                log_crit("No supported usb control mechanisms found");
            break;
        }

        common_msleep(2000);
    }

    /* Allow making systemd control ipc */
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
    if( usbmoded_init_done_p() ) {
        log_warning("usb-moded started after init-done; "
                    "forcing appsync stop");
        appsync_deactivate_all(true);
    }
#endif

    /* Claim D-Bus service name before proceeding with things that
     * could result in dbus signal broadcasts from usb-moded interface.
     */
    if( !umdbus_init_service() ) {
        log_crit("usb-moded dbus service init failed");
        goto EXIT;
    }

    /* Initialize udev listener. Can cause mode changes.
     *
     * Failing here is allowed if '--fallback' commandline option is used.
     */
    if( !umudev_init() && !usbmoded_hw_fallback ) {
        log_crit("hwal init failed");
        goto EXIT;
    }

#ifdef MEEGOLOCK
    /* Initialize current user tracking. Can cause mode changes */
    if ( !user_watch_init() ) {
        log_crit("user watch init failed");
        goto EXIT;
    }
#endif

    /* Broadcast supported / hidden modes */
    // TODO: should this happen before umudev_init()?
    common_send_supported_modes_signal();
    common_send_available_modes_signal();
    common_send_hidden_modes_signal();
    common_send_whitelisted_modes_signal();

    /* Act on '--fallback' commandline option */
    if( usbmoded_hw_fallback ) {
        log_warning("Forcing USB state to connected always. ASK mode non functional!");
        /* Since there will be no disconnect signals coming from hw the state should not change */
        control_set_cable_state(CABLE_STATE_PC_CONNECTED);
    }

    ack = true;

EXIT:
    return ack;
}

/** Release resources allocated by usbmoded_init()
 */
static void usbmoded_cleanup(void)
{
    LOG_REGISTER_CONTEXT;

    /* Stop user change listener */
#ifdef MEEGOLOCK
    user_watch_stop();
#endif

    /* Stop the worker thread first to avoid confusion about shared
     * resources we are just about to release. */
    worker_quit();

    /* Detach from SystemBus. Components that hold reference to the
     * shared bus connection can still perform cleanup tasks, but new
     * references can't be obtained anymore and usb-moded myethod call
     * processing no longer occurs. */
    umdbus_cleanup();

    /* Stop appsync processes that have been started by usb-moded */
#ifdef APP_SYNC
    appsync_deactivate_all(false);
#endif

    /* Deny making systemd control ipc */
    systemd_control_stop();

    /* Stop tracking devicelock status */
#ifdef MEEGOLOCK
    devicelock_stop_listener();
#endif

    /* Stop tracking device state */
#ifdef MEEGOLOCK
    dsme_stop_listener();
#endif

    /* Stop udev listener */
    umudev_quit();

    /* Do backend specific cleanup */
    modules_quit();
    android_quit();
    configfs_quit();

    /* Undo trigger_init() */
    trigger_stop();

    /* Undo usbmoded_load_modelist() */
    usbmoded_free_modelist();

#ifdef APP_SYNC
    /* Undo appsync_load_configuration() */
    appsync_free_configuration();
#endif

    /* Release dynamic memory */
    worker_clear_kernel_module();
    worker_clear_hardware_mode();
    control_clear_cable_state();
    control_clear_internal_mode();
    control_clear_external_mode();
    control_clear_target_mode();

    modesetting_quit();

    /* Detach from SessionBus connection used for APP_SYNC_DBUS.
     *
     * Can be handled separately from SystemBus side wind down. */
#ifdef APP_SYNC
# ifdef APP_SYNC_DBUS
    dbusappsync_cleanup();
# endif
#endif
}

/* ========================================================================= *
 * MAIN ENTRY
 * ========================================================================= */

static const char usbmoded_usage_info[] =
"Usage: usb_moded [OPTION]...\n"
"USB mode daemon\n"
"\n"
"  -a,  --android_usb_broken\n"
"      keep gadget active on broken android kernels\n"
"  -i,  --android_usb_broken_udev_events\n"
"      ignore incorrect disconnect events after mode setting\n"
"  -f,  --fallback\n"
"      assume always connected\n"
"  -s,  --force-syslog\n"
"      log to syslog\n"
"  -T,  --force-stderr\n"
"      log to stderr\n"
"  -l,  --log-line-info\n"
"      log to stderr and show origin of logging\n"
"  -D,  --debug\n"
"      turn on debug printing\n"
"  -d,  --diag\n"
"      turn on diag mode\n"
"  -h,  --help\n"
"      display this help and exit\n"
"  -r,  --rescue\n"
"      rescue mode\n"
#ifdef SYSTEMD
"  -n,  --systemd\n"
"      notify systemd when started up\n"
#endif
"  -v,  --version\n"
"      output version information and exit\n"
"  -m,  --max-cable-delay=<ms>\n"
"      maximum delay before accepting cable connection\n"
"  -b,  --android-bootup-function=<function>\n"
"      Setup given function during bootup. Might be required\n"
"      on some devices to make enumeration work on the 1st\n"
"      cable connect.\n"
"  -I --dbus-introspect-xml\n"
"      Dump usb-moded D-Bus introspect data to stdout.\n"
"  -B --dbus-busconfig-xml\n"
"      Dump usb-moded D-Bus busconfig data to stdout.\n"
"\n";

static const struct option usbmoded_long_options[] =
{
    { "android_usb_broken",             no_argument,       0, 'a' },
    { "android_usb_broken_udev_events", no_argument,       0, 'i' },
    { "fallback",                       no_argument,       0, 'd' },
    { "force-syslog",                   no_argument,       0, 's' },
    { "force-stderr",                   no_argument,       0, 'T' },
    { "log-line-info",                  no_argument,       0, 'l' },
    { "debug",                          no_argument,       0, 'D' },
    { "diag",                           no_argument,       0, 'd' },
    { "help",                           no_argument,       0, 'h' },
    { "rescue",                         no_argument,       0, 'r' },
    { "systemd",                        no_argument,       0, 'n' },
    { "version",                        no_argument,       0, 'v' },
    { "max-cable-delay",                required_argument, 0, 'm' },
    { "android-bootup-function",        required_argument, 0, 'b' },
    { "auto-exit",                      no_argument,       0, 'Q' },
    { "dbus-introspect-xml",            no_argument,       0, 'I' },
    { "dbus-busconfig-xml",             no_argument,       0, 'B' },
    { 0, 0, 0, 0 }
};

static const char usbmoded_short_options[] = "aifsTlDdhrnvm:b:QIB";

/* Display usbmoded_usage information */
static void usbmoded_usage(void)
{
    LOG_REGISTER_CONTEXT;

    fprintf(stdout, "%s", usbmoded_usage_info);
}

static void usbmoded_parse_options(int argc, char* argv[])
{
    LOG_REGISTER_CONTEXT;

    /* Parse the command-line options */
    for( ;; ) {
        int opt = getopt_long(argc, argv,
                              usbmoded_short_options,
                              usbmoded_long_options,
                              0);
        if( opt == -1 )
            break;

        switch (opt)
        {
        case 'a':
            log_warning("Deprecated option: --android_usb_broken");
            break;
        case 'i':
            log_warning("Deprecated option: --android_usb_broken_udev_events");
            break;
        case 'f':
            usbmoded_hw_fallback = true;
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
            usbmoded_set_diag_mode(true);
            break;

        case 'h':
            usbmoded_usage();
            exit(EXIT_SUCCESS);

        case 'r':
            usbmoded_set_rescue_mode(true);
            break;
#ifdef SYSTEMD
        case 'n':
            usbmoded_systemd_notify = true;
            break;
#endif
        case 'v':
            printf("USB mode daemon version: %s\n", VERSION);
            exit(EXIT_SUCCESS);

        case 'm':
            usbmoded_set_cable_connection_delay(strtol(optarg, 0, 0));
            break;

        case 'b':
            log_warning("Deprecated option: --android-bootup-function");
            break;

        case 'Q':
          usbmoded_auto_exit = true;
          break;

        case 'I':
            umdbus_dump_introspect_xml();
            exit(EXIT_SUCCESS);

        case 'B':
            umdbus_dump_busconfig_xml();
            exit(EXIT_SUCCESS);

        default:
            usbmoded_usage();
            exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char* argv[])
{
    LOG_REGISTER_CONTEXT;

    /* Library init calls that should be made before
     * using library functionality.
     */
#if !GLIB_CHECK_VERSION(2, 36, 0)
    g_type_init();
#endif
#if !GLIB_CHECK_VERSION(2, 31, 0)
    g_thread_init(NULL);
#endif
    dbus_threads_init_default();

    /* - - - - - - - - - - - - - - - - - - - *
     * OPTIONS
     * - - - - - - - - - - - - - - - - - - - */

    /* Set logging defaults */
    log_init();
    log_set_name(basename(*argv));

    /* Parse command line options */
    usbmoded_parse_options(argc, argv);

    fprintf(stderr, "usb_moded %s starting\n", VERSION);
    fflush(stderr);

    /* Silence common_system() calls */
    if( log_get_type() != LOG_TO_STDERR && log_get_level() != LOG_DEBUG )
    {
        if( !freopen("/dev/null", "a", stdout) ) {
            log_err("can't redirect stdout: %m");
        }
        if( !freopen("/dev/null", "a", stderr) ) {
            log_err("can't redirect stderr: %m");
        }
    }

    /* - - - - - - - - - - - - - - - - - - - *
     * INITIALIZE
     * - - - - - - - - - - - - - - - - - - - */

    if( !usbmoded_init() )
        goto EXIT;

    /* - - - - - - - - - - - - - - - - - - - *
     * EXECUTE
     * - - - - - - - - - - - - - - - - - - - */

    /* Tell systemd that we have started up */
#ifdef SYSTEMD
    if( usbmoded_systemd_notify ) {
        log_debug("notifying systemd");
        sd_notify(0, "READY=1");
    }
#endif

    /* init succesful, run main loop */
    usbmoded_exitcode = EXIT_SUCCESS;

    control_set_enabled(true);

    if( usbmoded_auto_exit )
        goto EXIT;

    usbmoded_mainloop = g_main_loop_new(NULL, FALSE);

    log_debug("enter usb-moded mainloop");
    g_main_loop_run(usbmoded_mainloop);
    log_debug("leave usb-moded mainloop");

    g_main_loop_unref(usbmoded_mainloop),
        usbmoded_mainloop = 0;

    /* - - - - - - - - - - - - - - - - - - - *
     * CLEANUP
     * - - - - - - - - - - - - - - - - - - - */
EXIT:
    usbmoded_cleanup();

    /* Memory leak debugging - instruct libdbus to flush resources. */
#if 0
    dbus_shutdown();
#endif

    /* - - - - - - - - - - - - - - - - - - - *
     * EXIT
     * - - - - - - - - - - - - - - - - - - - */

    /* Must be done just before exit to make sure no more wakelocks
     * are taken and left behind on exit path */
    usbmoded_allow_suspend();

    log_debug("usb-moded return from main, with exit code %d",
              usbmoded_exitcode);
    return usbmoded_exitcode;
}
