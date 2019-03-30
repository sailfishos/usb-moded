/**
 * @file usb_moded-udev.c
 *
 * Copyright (C) 2011 Nokia Corporation. All rights reserved.
 * Copyright (C) 2013-2019 Jolla Ltd.
 *
 * @author: Philippe De Swert <philippe.de-swert@nokia.com>
 * @author: Philippe De Swert <phdeswer@lumi.maa>
 * @author: Tapio Rantala <ext-tapio.rantala@nokia.com>
 * @author: Philippe De Swert <philippedeswert@gmail.com>
 * @author: Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author: Jarko Poutiainen <jarko.poutiainen@jollamobile.com>
 * @author: Simo Piiroinen <simo.piiroinen@jollamobile.com>
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

#include "usb_moded-udev.h"

#include "usb_moded-config-private.h"
#include "usb_moded-control.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded-log.h"

#include <string.h>

#include <libudev.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * UMUDEV
 * ------------------------------------------------------------------------- */

static gboolean      umudev_cable_state_timer_cb   (gpointer aptr);
static void          umudev_cable_state_stop_timer (void);
static void          umudev_cable_state_start_timer(gint delay);
static bool          umudev_cable_state_connected  (void);
static cable_state_t umudev_cable_state_get        (void);
static void          umudev_cable_state_set        (cable_state_t state);
static void          umudev_cable_state_changed    (void);
static void          umudev_cable_state_from_udev  (cable_state_t curr);
static void          umudev_io_error_cb            (gpointer data);
static gboolean      umudev_io_input_cb            (GIOChannel *iochannel, GIOCondition cond, gpointer data);
static void          umudev_parse_properties       (struct udev_device *dev, bool initial);
static int           umudev_score_as_power_supply  (const char *syspath);
gboolean             umudev_init                   (void);
void                 umudev_quit                   (void);

/* ========================================================================= *
 * Data
 * ========================================================================= */

/* global variables */
static struct udev         *umudev_object     = 0;
static struct udev_monitor *umudev_monitor    = 0;
static gchar               *umudev_sysname    = 0;
static guint                umudev_watch_id   = 0;
static bool                 umudev_in_cleanup = false;

/** Cable state as evaluated from udev events */
static cable_state_t umudev_cable_state_current  = CABLE_STATE_UNKNOWN;

/** Cable state considered active by usb-moded */
static cable_state_t umudev_cable_state_active   = CABLE_STATE_UNKNOWN;

/** Previously active cable state */
static cable_state_t umudev_cable_state_previous = CABLE_STATE_UNKNOWN;

/** Timer id for delaying: reported by udev -> active in usb-moded */
static guint umudev_cable_state_timer_id = 0;
static gint  umudev_cable_state_timer_delay = -1;

/* ========================================================================= *
 * cable state
 * ========================================================================= */

static gboolean umudev_cable_state_timer_cb(gpointer aptr)
{
    LOG_REGISTER_CONTEXT;

    (void)aptr;
    umudev_cable_state_timer_id = 0;
    umudev_cable_state_timer_delay = -1;

    log_debug("trigger delayed transfer to: %s",
              cable_state_repr(umudev_cable_state_current));
    umudev_cable_state_set(umudev_cable_state_current);
    return FALSE;
}

static void umudev_cable_state_stop_timer(void)
{
    LOG_REGISTER_CONTEXT;

    if( umudev_cable_state_timer_id ) {
        log_debug("cancel delayed transfer to: %s",
                  cable_state_repr(umudev_cable_state_current));
        g_source_remove(umudev_cable_state_timer_id),
            umudev_cable_state_timer_id = 0;
        umudev_cable_state_timer_delay = -1;
    }
}

static void umudev_cable_state_start_timer(gint delay)
{
    LOG_REGISTER_CONTEXT;

    if( umudev_cable_state_timer_delay != delay ) {
        umudev_cable_state_stop_timer();
    }

    if( !umudev_cable_state_timer_id ) {
        log_debug("schedule delayed transfer to: %s",
                  cable_state_repr(umudev_cable_state_current));
        umudev_cable_state_timer_id =
            g_timeout_add(delay,
                          umudev_cable_state_timer_cb, 0);
        umudev_cable_state_timer_delay = delay;
    }
}

static bool
umudev_cable_state_connected(void)
{
    LOG_REGISTER_CONTEXT;

    bool connected = false;
    switch( umudev_cable_state_get() ) {
    default:
        break;
    case CABLE_STATE_CHARGER_CONNECTED:
    case CABLE_STATE_PC_CONNECTED:
        connected = true;
        break;
    }
    return connected;
}

static cable_state_t umudev_cable_state_get(void)
{
    LOG_REGISTER_CONTEXT;

    return umudev_cable_state_active;
}

static void umudev_cable_state_set(cable_state_t state)
{
    LOG_REGISTER_CONTEXT;

    umudev_cable_state_stop_timer();

    if( umudev_cable_state_active == state )
        goto EXIT;

    umudev_cable_state_previous = umudev_cable_state_active;
    umudev_cable_state_active   = state;

    log_debug("cable_state: %s -> %s",
              cable_state_repr(umudev_cable_state_previous),
              cable_state_repr(umudev_cable_state_active));

    umudev_cable_state_changed();

EXIT:
    return;
}

static void umudev_cable_state_changed(void)
{
    LOG_REGISTER_CONTEXT;

    /* The rest of usb-moded separates charger
     * and pc connection states... make single
     * state tracking compatible with that. */

    /* First handle pc/charger disconnect based
     * on previous state.
     */
    switch( umudev_cable_state_previous ) {
    default:
    case CABLE_STATE_DISCONNECTED:
        /* dontcare */
        break;
    case CABLE_STATE_CHARGER_CONNECTED:
        umdbus_send_event_signal(CHARGER_DISCONNECTED);
        break;
    case CABLE_STATE_PC_CONNECTED:
        umdbus_send_event_signal(USB_DISCONNECTED);
        break;
    }

    /* Then handle pc/charger connect based
     * on current state.
     */

    switch( umudev_cable_state_active ) {
    default:
    case CABLE_STATE_DISCONNECTED:
        /* dontcare */
        break;
    case CABLE_STATE_CHARGER_CONNECTED:
        umdbus_send_event_signal(CHARGER_CONNECTED);
        break;
    case CABLE_STATE_PC_CONNECTED:
        umdbus_send_event_signal(USB_CONNECTED);
        break;
    }

    /* Then act on usb mode */
    control_set_cable_state(umudev_cable_state_active);
}

static void umudev_cable_state_from_udev(cable_state_t curr)
{
    LOG_REGISTER_CONTEXT;

    cable_state_t prev = umudev_cable_state_current;
    umudev_cable_state_current = curr;

    if( prev == curr )
        goto EXIT;

    log_debug("reported cable state: %s -> %s",
              cable_state_repr(prev),
              cable_state_repr(curr));

    /* Because mode transitions are handled synchronously and can thus
     * block the usb-moded mainloop, we might end up receiving a bursts
     * of stale udev events after returning from mode switch - including
     * multiple cable connect / disconnect events due to user replugging
     * the cable in frustration of things taking too long.
     */

    if( curr == CABLE_STATE_DISCONNECTED ) {
        /* If we see any disconnect events, those must be acted on
         * immediately to get the 1st disconnect handled.
         */
        umudev_cable_state_set(curr);
    }
    else {
        /* All other transitions are handled with at least 100 ms delay.
         * This should compress multiple stale disconnect + connect
         * pairs into single action.
         */
        gint delay = 100;

        if( curr == CABLE_STATE_PC_CONNECTED && prev != CABLE_STATE_UNKNOWN ) {
            if( delay < usbmoded_get_cable_connection_delay() )
                delay = usbmoded_get_cable_connection_delay();
        }

        umudev_cable_state_start_timer(delay);
    }

EXIT:
    return;
}

/* ========================================================================= *
 * legacy code
 * ========================================================================= */

static void umudev_io_error_cb(gpointer data)
{
    LOG_REGISTER_CONTEXT;

    (void)data;

    /* we do not want to restart when we try to clean up */
    if( !umudev_in_cleanup ) {
        log_debug("USB connection watch destroyed, restarting it\n!");
        /* restart trigger */
        umudev_quit();
        umudev_init();
    }
}

static gboolean umudev_io_input_cb(GIOChannel *iochannel, GIOCondition cond, gpointer data)
{
    LOG_REGISTER_CONTEXT;

    (void)iochannel;
    (void)data;

    gboolean continue_watching = TRUE;

    /* No code paths are allowed to bypass the common_release_wakelock() call below */
    common_acquire_wakelock(USB_MODED_WAKELOCK_PROCESS_INPUT);

    if( cond & G_IO_IN )
    {
        /* This normally blocks but G_IO_IN indicates that we can read */
        struct udev_device *dev = udev_monitor_receive_device(umudev_monitor);
        if( !dev )
        {
            /* if we get something else something bad happened stop watching to avoid busylooping */
            continue_watching = FALSE;
        }
        else
        {
            /* check if it is the actual device we want to check */
            if( !strcmp(umudev_sysname, udev_device_get_sysname(dev)) )
            {
                if( !strcmp(udev_device_get_action(dev), "change") )
                {
                    umudev_parse_properties(dev, false);
                }
            }

            udev_device_unref(dev);
        }
    }

    if( cond & (G_IO_ERR | G_IO_HUP | G_IO_NVAL) )
    {
        /* Unhandled errors turn io watch to virtual busyloop too */
        continue_watching = FALSE;
    }

    if( !continue_watching && umudev_watch_id )
    {
        umudev_watch_id = 0;
        log_crit("udev io watch disabled");
    }

    common_release_wakelock(USB_MODED_WAKELOCK_PROCESS_INPUT);

    return continue_watching;
}

static void umudev_parse_properties(struct udev_device *dev, bool initial)
{
    LOG_REGISTER_CONTEXT;

    (void)initial;

    /* udev properties we are interested in */
    const char *power_supply_present = 0;
    const char *power_supply_online  = 0;
    const char *power_supply_type    = 0;

    /* Assume there is no usb connection until proven otherwise */
    bool connected  = false;

    /* Unless debug logging has been request via command line,
     * suppress warnings about potential property issues and/or
     * fallback strategies applied (to avoid spamming due to the
     * code below seeing the same property values over and over
     * again also in stable states).
     */
    bool warnings = log_p(LOG_DEBUG);

    /*
     * Check for present first as some drivers use online for when charging
     * is enabled
     */
    power_supply_present = udev_device_get_property_value(dev, "POWER_SUPPLY_PRESENT");
    if( !power_supply_present ) {
        power_supply_present =
            power_supply_online = udev_device_get_property_value(dev, "POWER_SUPPLY_ONLINE");
    }

    if( power_supply_present && !strcmp(power_supply_present, "1") )
        connected = true;

    /* Transition period = Connection status derived from udev
     * events disagrees with usb-moded side bookkeeping. */
    if( connected != control_get_connection_state() ) {
        /* Enable udev property diagnostic logging */
        warnings = true;
        /* Block suspend briefly */
        usbmoded_delay_suspend();
    }

    if( !connected ) {
        /* Handle: Disconnected */

        if( warnings && !power_supply_present )
            log_err("No usable power supply indicator\n");
        umudev_cable_state_from_udev(CABLE_STATE_DISCONNECTED);
    }
    else {
        if( warnings && power_supply_online )
            log_warning("Using online property\n");

        /* At least h4113 i.e. "Xperia XA2 - Dual SIM" seem to have
         * POWER_SUPPLY_REAL_TYPE udev property with information
         * that usb-moded expects to be in POWER_SUPPLY_TYPE prop.
         */
        power_supply_type = udev_device_get_property_value(dev, "POWER_SUPPLY_REAL_TYPE");
        if( !power_supply_type )
            power_supply_type = udev_device_get_property_value(dev, "POWER_SUPPLY_TYPE");
        /*
         * Power supply type might not exist also :(
         * Send connected event but this will not be able
         * to discriminate between charger/cable.
         */
        if( !power_supply_type ) {
            if( warnings )
                log_warning("Fallback since cable detection might not be accurate. "
                            "Will connect on any voltage on charger.\n");
            umudev_cable_state_from_udev(CABLE_STATE_PC_CONNECTED);
            goto cleanup;
        }

        log_debug("CONNECTED - POWER_SUPPLY_TYPE = %s", power_supply_type);

        if( !strcmp(power_supply_type, "USB") ||
            !strcmp(power_supply_type, "USB_CDP") ) {
            umudev_cable_state_from_udev(CABLE_STATE_PC_CONNECTED);
        }
        else if( !strcmp(power_supply_type, "USB_DCP") ||
                 !strcmp(power_supply_type, "USB_HVDCP") ||
                 !strcmp(power_supply_type, "USB_HVDCP_3") ) {
            umudev_cable_state_from_udev(CABLE_STATE_CHARGER_CONNECTED);
        }
        else if( !strcmp(power_supply_type, "USB_FLOAT")) {
            if( !umudev_cable_state_connected() )
                log_warning("connection type detection failed, assuming charger");
            umudev_cable_state_from_udev(CABLE_STATE_CHARGER_CONNECTED);
        }
        else if( !strcmp(power_supply_type, "Unknown")) {
            // nop
            log_warning("unknown connection type reported, assuming disconnected");
            umudev_cable_state_from_udev(CABLE_STATE_DISCONNECTED);
        }
        else {
            if( warnings )
                log_warning("unhandled power supply type: %s", power_supply_type);
            umudev_cable_state_from_udev(CABLE_STATE_DISCONNECTED);
        }
    }

cleanup:
    return;
}

static int umudev_score_as_power_supply(const char *syspath)
{
    LOG_REGISTER_CONTEXT;

    int                 score   = 0;
    struct udev_device *dev     = 0;
    const char         *sysname = 0;

    if( !umudev_object )
        goto EXIT;

    if( !(dev = udev_device_new_from_syspath(umudev_object, syspath)) )
        goto EXIT;

    if( !(sysname = udev_device_get_sysname(dev)) )
        goto EXIT;

    /* try to assign a weighed score */

    /* check that it is not a battery */
    if(strstr(sysname, "battery") || strstr(sysname, "BAT"))
        goto EXIT;

    /* if it contains usb in the name it very likely is good */
    if(strstr(sysname, "usb"))
        score = score + 10;

    /* often charger is also mentioned in the name */
    if(strstr(sysname, "charger"))
        score = score + 5;

    /* present property is used to detect activity, however online is better */
    if(udev_device_get_property_value(dev, "POWER_SUPPLY_PRESENT"))
        score = score + 5;

    if(udev_device_get_property_value(dev, "POWER_SUPPLY_ONLINE"))
        score = score + 10;

    /* type is used to detect if it is a cable or dedicated charger.
     * Bonus points if it is there. */
    if(udev_device_get_property_value(dev, "POWER_SUPPLY_TYPE"))
        score = score + 10;

EXIT:
    /* clean up */
    if( dev )
        udev_device_unref(dev);

    return score;
}

gboolean umudev_init(void)
{
    LOG_REGISTER_CONTEXT;

    gboolean                success = FALSE;

    char                   *configured_device = NULL;
    char                   *configured_subsystem = NULL;
    struct udev_device     *dev = 0;
    GIOChannel             *iochannel = 0;

    int ret = 0;

    /* Clear in-cleanup in case of restart */
    umudev_in_cleanup = false;

    /* Create the udev object */
    if( !(umudev_object = udev_new()) ) {
        log_err("Can't create umudev_object\n");
        goto EXIT;
    }

    if( !(configured_device = config_find_udev_path()) )
        configured_device = g_strdup("/sys/class/power_supply/usb");

    if( !(configured_subsystem = config_find_udev_subsystem()) )
        configured_subsystem = g_strdup("power_supply");

    /* Try with configured / default device */
    dev = udev_device_new_from_syspath(umudev_object, configured_device);

    /* If needed, try heuristics */
    if( !dev ) {
        log_debug("Trying to guess $power_supply device.\n");

        int    current_score = 0;
        gchar *current_name  = 0;

        struct udev_enumerate  *list;
        struct udev_list_entry *list_entry;
        struct udev_list_entry *first_entry;

        list = udev_enumerate_new(umudev_object);
        udev_enumerate_add_match_subsystem(list, "power_supply");
        udev_enumerate_scan_devices(list);
        first_entry = udev_enumerate_get_list_entry(list);
        udev_list_entry_foreach(list_entry, first_entry) {
            const char *name = udev_list_entry_get_name(list_entry);
            int score = umudev_score_as_power_supply(name);
            if( current_score < score ) {
                g_free(current_name);
                current_name = g_strdup(name);
                current_score = score;
            }
        }
        /* check if we found anything with some kind of score */
        if(current_score > 0) {
            dev = udev_device_new_from_syspath(umudev_object, current_name);
        }
        g_free(current_name);
    }

    /* Give up if no power supply device was found */
    if( !dev ) {
        log_err("Unable to find $power_supply device.");
        /* communicate failure, mainloop will exit and call appropriate clean-up */
        goto EXIT;
    }

    /* Cache device name */
    umudev_sysname = g_strdup(udev_device_get_sysname(dev));
    log_debug("device name = %s\n", umudev_sysname);

    /* Start monitoring for changes */
    umudev_monitor = udev_monitor_new_from_netlink(umudev_object, "udev");
    if( !umudev_monitor )
    {
        log_err("Unable to monitor the netlink\n");
        /* communicate failure, mainloop will exit and call appropriate clean-up */
        goto EXIT;
    }

    ret = udev_monitor_filter_add_match_subsystem_devtype(umudev_monitor,
                                                          configured_subsystem,
                                                          NULL);
    if(ret != 0)
    {
        log_err("Udev match failed.\n");
        goto EXIT;
    }

    ret = udev_monitor_enable_receiving(umudev_monitor);
    if(ret != 0)
    {
        log_err("Failed to enable monitor recieving.\n");
        goto EXIT;
    }

    iochannel = g_io_channel_unix_new(udev_monitor_get_fd(umudev_monitor));
    if( !iochannel )
        goto EXIT;

    umudev_watch_id = g_io_add_watch_full(iochannel, 0, G_IO_IN, umudev_io_input_cb, NULL, umudev_io_error_cb);
    if( !umudev_watch_id )
        goto EXIT;

    /* everything went well */
    success = TRUE;

    /* check initial status */
    umudev_parse_properties(dev, true);

EXIT:
    /* Cleanup local resources */
    if( iochannel )
        g_io_channel_unref(iochannel);

    if( dev )
        udev_device_unref(dev);

    g_free(configured_subsystem);
    g_free(configured_device);

    /* All or nothing */
    if( !success )
        umudev_quit();

    return success;
}

void umudev_quit(void)
{
    LOG_REGISTER_CONTEXT;

    umudev_in_cleanup = true;

    log_debug("HWhal cleanup\n");

    if( umudev_watch_id )
    {
        g_source_remove(umudev_watch_id),
            umudev_watch_id = 0;
    }

    if( umudev_monitor ) {
        udev_monitor_unref(umudev_monitor),
            umudev_monitor = 0;
    }

    if( umudev_object ) {
        udev_unref(umudev_object),
            umudev_object =0 ;
    }

    g_free(umudev_sysname),
        umudev_sysname = 0;

    umudev_cable_state_stop_timer();
}
