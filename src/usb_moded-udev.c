/**
 * @file usb_moded-udev.c
 *
 * Copyright (c) 2011 Nokia Corporation. All rights reserved.
 * Copyright (c) 2013 - 2021 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Philippe De Swert <phdeswer@lumi.maa>
 * @author Tapio Rantala <ext-tapio.rantala@nokia.com>
 * @author Philippe De Swert <philippedeswert@gmail.com>
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author Jarko Poutiainen <jarko.poutiainen@jollamobile.com>
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

#include "usb_moded-udev.h"

#include "usb_moded.h"
#include "usb_moded-config-private.h"
#include "usb_moded-control.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded-log.h"

#include <libudev.h>

/* ========================================================================= *
 * Constants
 * ========================================================================= */

// Properties present in battery devices
#define PROP_CAPACITY  "POWER_SUPPLY_CAPACITY"
// Properties present in charger devices
#define PROP_ONLINE    "POWER_SUPPLY_ONLINE"
#define PROP_TYPE      "POWER_SUPPLY_TYPE"
#define PROP_REAL_TYPE "POWER_SUPPLY_REAL_TYPE"
// Properties common to both battery and charger devices
#define PROP_STATUS    "POWER_SUPPLY_STATUS"
#define PROP_PRESENT   "POWER_SUPPLY_PRESENT"

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * UTILITY
 * ------------------------------------------------------------------------- */

static const char          *umudev_pretty_string(const char *str);
static bool                 umudev_white_p      (int ch);
static bool                 umudev_black_p      (int ch);
static char                *umudev_strip        (char *str);
static char                *umudev_extract_token(char **ppos);
static gchar               *umudev_read_textfile(const char *dirpath, const char *filename);
static gchar               *umudev_get_config   (const char *key);
static struct udev_device **umudev_get_devices  (const char *subsystem);
static void                 umudev_free_devices (struct udev_device **devices);

/* ------------------------------------------------------------------------- *
 * UMUDEV_CHARGER
 * ------------------------------------------------------------------------- */

static bool     umudev_charger_set_online   (const char *online);
static bool     umudev_charger_set_type     (const char *type);
static void     umudev_charger_update_from  (struct udev_device *dev);
static int      umudev_charger_get_score    (struct udev_device *dev);
static void     umudev_charger_find_device  (void);
static void     umudev_charger_schedule_poll(void);
static void     umudev_charger_cancel_poll  (void);
static gboolean umudev_charger_poll_cb      (gpointer aptr);
static void     umudev_charger_poll_now     (void);

/* ------------------------------------------------------------------------- *
 * UMUDEV_EXTCON
 * ------------------------------------------------------------------------- */

static gchar *umudev_extcon_parse_state(const char *rawstate);
static void   umudev_extcon_set_state  (const char *rawstate);
static void   umudev_extcon_read_from  (const char *syspath);
static void   umudev_extcon_update_from(struct udev_device *dev);
static void   umudev_extcon_find_device(void);

/* ------------------------------------------------------------------------- *
 * UMUDEV_ANDROID
 * ------------------------------------------------------------------------- */

static gchar *umudev_android_parse_state(const char *rawstate);
static void   umudev_android_set_state  (const char *rawstate);
static void   umudev_android_read_from  (const char *syspath);
static void   umudev_android_update_from(struct udev_device *dev);
static void   umudev_android_find_device(void);

/* ------------------------------------------------------------------------- *
 * UMUDEV_CABLE_STATE
 * ------------------------------------------------------------------------- */

static gboolean      umudev_cable_state_timer_cb   (gpointer aptr);
static void          umudev_cable_state_stop_timer (void);
static void          umudev_cable_state_start_timer(gint delay);
static bool          umudev_cable_state_connected  (void);
static cable_state_t umudev_cable_state_get        (void);
static void          umudev_cable_state_set        (cable_state_t state);
static void          umudev_cable_state_changed    (void);
static void          umudev_cable_state_from_udev  (cable_state_t curr);

/* ------------------------------------------------------------------------- *
 * UMUDEV
 * ------------------------------------------------------------------------- */

static void     umudev_io_error_cb   (gpointer data);
static gboolean umudev_io_input_cb   (GIOChannel *iochannel, GIOCondition cond, gpointer data);
static void     umudev_evaluate_state(void);
gboolean        umudev_init          (void);
void            umudev_quit          (void);

/* ========================================================================= *
 * Data
 * ========================================================================= */

/* global variables */
static struct udev         *umudev_object             = 0;
static struct udev_monitor *umudev_monitor            = 0;

/* Device monitoring: power_supply subsystem */
static gchar               *umudev_charger_syspath    = 0;
static gchar               *umudev_charger_subsystem  = 0;
static gchar               *umudev_charger_online     = 0;
static gchar               *umudev_charger_type       = 0;

/* Delayed charger property refresh / state re-evaluation
 *
 * This is done when extcon / android_usb changes are seen.
 * The delay needs to be long enough to cover transient android_usb
 * DISCONNECTED states related to mode configuration changes, so
 * that they are not interpreted as physical cable disconnects.
 */
static guint                umudev_charger_poll_id    = 0;
static gint                 umudev_charger_poll_delay = 1000;

/* Device monitoring: extcon subsystem */
static gchar               *umudev_extcon_syspath     = 0;
static gchar               *umudev_extcon_subsystem   = 0;
static gchar               *umudev_extcon_state       = NULL;

/* Device monitoring: android_usb subsystem */
static gchar               *umudev_android_syspath    = 0;
static gchar               *umudev_android_subsystem  = 0;
static gchar               *umudev_android_state      = NULL;

static guint                umudev_watch_id           = 0;
static bool                 umudev_in_cleanup         = false;

/** Cable state as evaluated from udev events */
static cable_state_t umudev_cable_state_current  = CABLE_STATE_UNKNOWN;

/** Cable state considered active by usb-moded */
static cable_state_t umudev_cable_state_active   = CABLE_STATE_UNKNOWN;

/** Previously active cable state */
static cable_state_t umudev_cable_state_previous = CABLE_STATE_UNKNOWN;

/** Timer id for delaying: reported by udev -> active in usb-moded */
static guint umudev_cable_state_timer_id    = 0;
static gint  umudev_cable_state_timer_delay = -1;

/* ========================================================================= *
 * UTILITY
 * ========================================================================= */

static const char *umudev_pretty_string(const char *str)
{
    return !str ? "<null>" : !*str ? "<empty>" : str;
}

static bool umudev_white_p(int ch)
{
    return (ch > 0) && (ch <= 32);
}

static bool umudev_black_p(int ch)
{
    return (ch < 0) || (ch > 32);
}

static char *umudev_strip(char *str)
{
    if( str ) {
        char *dst = str;
        char *src = str;
        while( umudev_white_p(*src) )
            ++src;
        for( ;; ) {
            while( umudev_black_p(*src) )
                *dst++ = *src++;
            while( umudev_white_p(*src) )
                ++src;
            if( !*src )
                break;
            *dst++ = ' ';
        }
        *dst = 0;
    }
    return str;
}

static char *umudev_extract_token(char **ppos)
{
    char *beg = *ppos;
    while( umudev_white_p(*beg) )
        ++beg;
    char *end = beg;
    while( umudev_black_p(*end) )
        ++end;
    if( *end )
        *end++ = 0;
    *ppos = end;
    return beg;
}

static gchar *umudev_read_textfile(const char *dirpath, const char *filename)
{
    gchar  *data = NULL;
    if( dirpath && filename ) {
        gchar  *path = g_strdup_printf("%s/%s", dirpath, filename);
        if( !g_file_get_contents(path, &data, NULL, NULL) )
            log_warning("%s: could not read file", path);
        g_free(path);
    }
    return data;
}

static gchar *umudev_get_config(const char *key)
{
    gchar *val = config_get_conf_string(UDEV_ENTRY, key);
    if( val ) {
        if( !*val || !strcmp(val, "none") || !strcmp(val, "null") )
            g_free(val), val = NULL;
    }
    return val;
}

static struct udev_device **umudev_get_devices(const char *subsystem)
{
    struct udev_device **devices = NULL;

    struct udev_enumerate *list;
    if( (list = udev_enumerate_new(umudev_object)) ) {
        udev_enumerate_add_match_subsystem(list, subsystem);
        udev_enumerate_scan_devices(list);
        struct udev_list_entry *iter;
        size_t count = 0;
        udev_list_entry_foreach(iter, udev_enumerate_get_list_entry(list)) {
            ++count;
        }
        devices = g_malloc_n(count + 1, sizeof *devices);
        count = 0;
        udev_list_entry_foreach(iter, udev_enumerate_get_list_entry(list)) {
            const char *syspath = udev_list_entry_get_name(iter);
            struct udev_device *dev =
                udev_device_new_from_syspath(umudev_object, syspath);
            if( dev )
                devices[count++] = dev;
        }
        devices[count] = NULL;
    }
    return devices;
}

static void umudev_free_devices(struct udev_device **devices)
{
    if( devices ) {
        for( size_t i = 0; devices[i]; ++i )
            udev_device_unref(devices[i]);
        g_free(devices);
    }
}

/* ========================================================================= *
 * UMUDEV_CHARGER
 * ========================================================================= */

static bool umudev_charger_set_online(const char *online)
{
    bool changed = false;
    if( g_strcmp0(umudev_charger_online, online) ) {
        log_debug("umudev_charger_online: %s -> %s",
                  umudev_pretty_string(umudev_charger_online),
                  umudev_pretty_string(online));
        g_free(umudev_charger_online),
            umudev_charger_online = g_strdup(online);
        changed = true;
    }
    return changed;
}

static bool umudev_charger_set_type(const char *type)
{
    bool changed = false;
    if( g_strcmp0(umudev_charger_type, type) ) {
        log_debug("umudev_charger_type: %s -> %s",
                  umudev_pretty_string(umudev_charger_type),
                  umudev_pretty_string(type));
        g_free(umudev_charger_type),
            umudev_charger_type = g_strdup(type);
        changed = true;
    }
    return changed;
}

static void umudev_charger_update_from(struct udev_device *dev)
{
    LOG_REGISTER_CONTEXT;

    /* udev properties we are interested in */
    const char *power_supply_online = 0;
    const char *power_supply_type   = 0;

    /*
     * Check for present first as some drivers use online for when charging
     * is enabled
     */
    power_supply_online = udev_device_get_property_value(dev, PROP_PRESENT);
    if( !power_supply_online )
        power_supply_online = udev_device_get_property_value(dev, PROP_ONLINE);

    /* At least h4113 i.e. "Xperia XA2 - Dual SIM" seem to have
     * POWER_SUPPLY_REAL_TYPE udev property with information
     * that usb-moded expects to be in POWER_SUPPLY_TYPE prop.
     */
    power_supply_type = udev_device_get_property_value(dev, PROP_REAL_TYPE);
    if( !power_supply_type )
        power_supply_type = udev_device_get_property_value(dev, PROP_TYPE);

    umudev_charger_set_online(power_supply_online);
    umudev_charger_set_type(power_supply_type);

    umudev_evaluate_state();
}

static int umudev_charger_get_score(struct udev_device *dev)
{
    LOG_REGISTER_CONTEXT;

    int         score   = 0;
    const char *sysname = NULL;

    if( !dev )
        goto EXIT;

    if( !(sysname = udev_device_get_sysname(dev)) )
        goto EXIT;

    /* check that it is not a battery */
    if( udev_device_get_property_value(dev, PROP_CAPACITY) )
        goto EXIT;

    /* check that it can be a charger */
    const char *online  = udev_device_get_property_value(dev, PROP_ONLINE);
    const char *present = udev_device_get_property_value(dev, PROP_PRESENT);
    if( !online && !present )
        goto EXIT;

    /* try to assign a weighed score */

    /* if it contains usb in the name it very likely is good */
    if( strstr(sysname, "usb") )
        score += 10;

    /* often charger is also mentioned in the name */
    if( strstr(sysname, "charger") )
        score += 5;

    /* present property is used to detect activity, however online is better */
    if( online )
        score += 10;

    if( present )
        score += 5;

    /* while usb-moded does not use status, it should be present */
    if( udev_device_get_property_value(dev, PROP_STATUS) )
        score += 5;

    /* type is used to detect if it is a cable or dedicated charger.
     * Bonus points if it is there. */
    if( udev_device_get_property_value(dev, PROP_TYPE) ||
        udev_device_get_property_value(dev, PROP_REAL_TYPE) )
        score += 10;
EXIT:

    log_debug("score: %2d; for: %s", score, umudev_pretty_string(sysname));

    return score;
}

static void umudev_charger_find_device(void)
{
    LOG_REGISTER_CONTEXT;

    gchar                  *tracking  = NULL;
    gchar                  *syspath   = NULL;
    gchar                  *subsystem = NULL;
    struct udev_device     *dev       = NULL;

    if( !(tracking = umudev_get_config(UDEV_CHARGER_TRACKING_KEY)) )
        tracking = g_strdup(UDEV_CHARGER_TRACKING_FALLBACK);

    log_debug("tracking=%s", umudev_pretty_string(tracking));

    if( !g_strcmp0(tracking, "0") )
        goto EXIT;

    if( !(syspath = umudev_get_config(UDEV_CHARGER_PATH_KEY)) )
        syspath = g_strdup(UDEV_CHARGER_PATH_FALLBACK);

    if( syspath ) {
        if( !(dev = udev_device_new_from_syspath(umudev_object, syspath)) )
            log_warning("Unable to find $charger device '%s'", syspath);
        else
            subsystem = g_strdup(udev_device_get_subsystem(dev));
    }

    if( !subsystem ) {
        if( !(subsystem = umudev_get_config(UDEV_CHARGER_SUBSYSTEM_KEY)) )
            subsystem = g_strdup(UDEV_CHARGER_SUBSYSTEM_FALLBACK);
    }

    if( !subsystem ) {
        log_warning("Unable to determine $charger subsystem.");
        goto EXIT;
    }

    if( udev_monitor_filter_add_match_subsystem_devtype(umudev_monitor,
                                                        subsystem,
                                                        NULL) != 0 ) {
        log_err("Unable to add $charger match");
        goto EXIT;
    }

    umudev_charger_subsystem = g_strdup(subsystem);

    if( !dev ) {
        /* Explicit device was not named or found -> probe all */
        log_debug("Trying to guess $charger device.\n");
        struct udev_device **devices = umudev_get_devices(subsystem);
        if( devices ) {
            int best_score = 0;
            int best_index = -1;
            for( int i = 0; devices[i]; ++i ) {
                int score = umudev_charger_get_score(devices[i]);
                if( best_score < score ) {
                    best_score = score;
                    best_index = i;
                }
            }
            if( best_index != -1 )
                dev = udev_device_ref(devices[best_index]);
            umudev_free_devices(devices);
        }
    }

    if( dev )
        umudev_charger_syspath = g_strdup(udev_device_get_syspath(dev));

EXIT:
    log_debug("charger device: subsystem=%s syspath=%s",
              umudev_pretty_string(umudev_charger_subsystem),
              umudev_pretty_string(umudev_charger_syspath));

    if( dev )
        udev_device_unref(dev);
    g_free(subsystem);
    g_free(syspath);
    g_free(tracking);
}

static void umudev_charger_schedule_poll(void)
{
    LOG_REGISTER_CONTEXT;

    if( !umudev_charger_poll_id ) {
        umudev_charger_poll_id = g_timeout_add(umudev_charger_poll_delay,
                                               umudev_charger_poll_cb,
                                               NULL);
    }
}

static void umudev_charger_cancel_poll(void)
{
    LOG_REGISTER_CONTEXT;

    if( umudev_charger_poll_id ) {
        g_source_remove(umudev_charger_poll_id),
            umudev_charger_poll_id = 0;
    }
}

static gboolean umudev_charger_poll_cb(gpointer aptr)
{
    LOG_REGISTER_CONTEXT;

    umudev_charger_poll_id = 0;
    umudev_charger_poll_now();
    return G_SOURCE_REMOVE;
}

static void umudev_charger_poll_now(void)
{
    LOG_REGISTER_CONTEXT;

    umudev_charger_cancel_poll();

    struct udev_device *dev = NULL;

    if( umudev_charger_syspath )
        dev = udev_device_new_from_syspath(umudev_object,
                                           umudev_charger_syspath);

    if( dev ) {
        umudev_charger_update_from(dev);
        udev_device_unref(dev);
    }
    else {
        umudev_evaluate_state();
    }
}

/* ========================================================================= *
 * UMUDEV_EXTCON
 * ========================================================================= */

static gchar *umudev_extcon_parse_state(const char *rawstate)
{
    /* We only need the "USB=N" part */
    gchar *state = NULL;
    gchar *tmp   = g_strdup(rawstate);
    char  *pos   = tmp;
    while( pos && *pos ) {
        char *tok = umudev_extract_token(&pos);
        if( !strncmp(tok, "USB=", 4) ) {
            state = g_strdup(tok);
            break;
        }
    }
    g_free(tmp);
    return state;
}

static void umudev_extcon_set_state(const char *rawstate)
{
    LOG_REGISTER_CONTEXT;

    gchar *state = umudev_extcon_parse_state(rawstate);
    if( g_strcmp0(umudev_extcon_state, state) ) {
        log_debug("umudev_extcon_state: %s -> %s",
                  umudev_pretty_string(umudev_extcon_state),
                  umudev_pretty_string(state));
        g_free(umudev_extcon_state),
            umudev_extcon_state = state,
            state = NULL;
        umudev_charger_schedule_poll();
    }
    g_free(state);
}

static void umudev_extcon_read_from(const char *syspath)
{
    /* Note: cached state is intentionally left as-it-is if reported
     *       state file does not exist / does not contain "USB=X" entry.
     */
    LOG_REGISTER_CONTEXT;
    gchar *rawstate = umudev_read_textfile(syspath, "state");
    if( rawstate )
        umudev_extcon_set_state(rawstate);
    g_free(rawstate);
}

static void umudev_extcon_update_from(struct udev_device *dev)
{
    const char *state = udev_device_get_property_value(dev, "STATE");
    if( state )
        umudev_extcon_set_state(state);
}

static void umudev_extcon_find_device(void)
{
    LOG_REGISTER_CONTEXT;

    gchar                  *tracking  = NULL;
    gchar                  *syspath   = NULL;
    gchar                  *subsystem = NULL;
    struct udev_device     *dev       = NULL;

    if( !(tracking = umudev_get_config(UDEV_EXTCON_TRACKING_KEY)) )
        tracking = g_strdup(UDEV_EXTCON_TRACKING_FALLBACK);

    log_debug("tracking=%s", umudev_pretty_string(tracking));

    if( !g_strcmp0(tracking, "0") )
        goto EXIT;

    if( !(syspath = umudev_get_config(UDEV_EXTCON_PATH_KEY)) )
        syspath = g_strdup(UDEV_EXTCON_PATH_FALLBACK);

    if( syspath ) {
        if( !(dev = udev_device_new_from_syspath(umudev_object, syspath)) )
            log_warning("Unable to find $extcon device '%s'", syspath);
        else
            subsystem = g_strdup(udev_device_get_subsystem(dev));
    }

    if( !subsystem ) {
        if( !(subsystem = umudev_get_config(UDEV_EXTCON_SUBSYSTEM_KEY)) )
            subsystem = g_strdup(UDEV_EXTCON_SUBSYSTEM_FALLBACK);
    }

    if( !subsystem ) {
        log_warning("Unable to determine $extcon subsystem.");
        goto EXIT;
    }

    if( udev_monitor_filter_add_match_subsystem_devtype(umudev_monitor,
                                                        subsystem,
                                                        NULL) != 0 ) {
        log_err("Unable to add $extcon match");
        goto EXIT;
    }

    umudev_extcon_subsystem = g_strdup(subsystem);

    if( dev ) {
        /* Explicit device was named and found -> probe it */
        umudev_extcon_syspath = g_strdup(udev_device_get_syspath(dev));
        umudev_extcon_read_from(umudev_extcon_syspath);
    }
    else {
        /* Explicit device was not named or found -> probe all */
        struct udev_device **devices = umudev_get_devices(subsystem);
        if( devices ) {
            for( size_t i = 0; devices[i]; ++i )
                umudev_extcon_read_from(udev_device_get_syspath(devices[i]));
            umudev_free_devices(devices);
        }
    }

EXIT:
    log_debug("extcon device: subsystem=%s syspath=%s",
              umudev_pretty_string(umudev_extcon_subsystem),
              umudev_pretty_string(umudev_extcon_syspath));

    if( dev )
        udev_device_unref(dev);
    g_free(subsystem);
    g_free(syspath);
    g_free(tracking);
}

/* ========================================================================= *
 * UMUDEV_ANDROID
 * ========================================================================= */

static gchar *umudev_android_parse_state(const char *rawstate)
{
    /* 'state' file ends with newline, udev properties do not */
    return umudev_strip(g_strdup(rawstate));
}

static void umudev_android_set_state(const char *rawstate)
{
    LOG_REGISTER_CONTEXT;

    gchar *state = umudev_android_parse_state(rawstate);
    if( g_strcmp0(umudev_android_state, state) ) {
        log_debug("umudev_android_state: %s -> %s",
                  umudev_pretty_string(umudev_android_state),
                  umudev_pretty_string(state));
        g_free(umudev_android_state),
            umudev_android_state = state,
            state = NULL;
        umudev_charger_schedule_poll();
    }
    g_free(state);
}

static void umudev_android_read_from(const char *syspath)
{
    /* Note: cached state is intentionally left as-it-is if
     *       state file does not exist / is not readable.
     */
    LOG_REGISTER_CONTEXT;

    gchar *rawstate = umudev_read_textfile(syspath, "state");
    if( rawstate )
        umudev_android_set_state(rawstate);
    g_free(rawstate);
}

static void umudev_android_update_from(struct udev_device *dev)
{
    const char *state = udev_device_get_property_value(dev, "USB_STATE");
    if( state )
        umudev_android_set_state(state);
}

static void umudev_android_find_device(void)
{
    LOG_REGISTER_CONTEXT;

    gchar                  *tracking  = NULL;
    gchar                  *syspath   = NULL;
    gchar                  *subsystem = NULL;
    struct udev_device     *dev       = NULL;

    if( !(tracking = umudev_get_config(UDEV_ANDROID_TRACKING_KEY)) )
        tracking = g_strdup(UDEV_ANDROID_TRACKING_FALLBACK);

    log_debug("tracking=%s", umudev_pretty_string(tracking));

    if( !g_strcmp0(tracking, "0") )
        goto EXIT;

    if( !(syspath = umudev_get_config(UDEV_ANDROID_PATH_KEY)) )
        syspath = g_strdup(UDEV_ANDROID_PATH_FALLBACK);

    if( syspath ) {
        if( !(dev = udev_device_new_from_syspath(umudev_object, syspath)) )
            log_warning("Unable to find $android device '%s'", syspath);
        else
            subsystem = g_strdup(udev_device_get_subsystem(dev));
    }

    if( !subsystem ) {
        if( !(subsystem = umudev_get_config(UDEV_ANDROID_SUBSYSTEM_KEY)) )
            subsystem = g_strdup(UDEV_ANDROID_SUBSYSTEM_FALLBACK);
    }

    if( !subsystem ) {
        log_warning("Unable to determine $android subsystem.");
        goto EXIT;
    }

    if( udev_monitor_filter_add_match_subsystem_devtype(umudev_monitor,
                                                        subsystem,
                                                        NULL) != 0 ) {
        log_err("Unable to add $android match");
        goto EXIT;
    }

    umudev_android_subsystem = g_strdup(subsystem);

    if( dev ) {
        /* Explicit device was named and found -> probe it */
        umudev_android_syspath = g_strdup(udev_device_get_syspath(dev));
        umudev_android_read_from(umudev_android_syspath);
    }
    else {
        /* Explicit device was not named or found -> probe all */
        struct udev_device **devices = umudev_get_devices(subsystem);
        if( devices ) {
            for( size_t i = 0; devices[i]; ++i )
                umudev_android_read_from(udev_device_get_syspath(devices[i]));
            umudev_free_devices(devices);
        }
    }

EXIT:
    log_debug("android device: subsystem=%s syspath=%s",
              umudev_pretty_string(umudev_android_subsystem),
              umudev_pretty_string(umudev_android_syspath));

    if( dev )
        udev_device_unref(dev);
    g_free(subsystem);
    g_free(syspath);
    g_free(tracking);
}

/* ========================================================================= *
 * UMUDEV_CABLE_STATE
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
 * UMUDEV
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
        if( !dev ) {
            /* if we get something else something bad happened stop watching to avoid busylooping */
            continue_watching = FALSE;
        }
        else {
            const char *syspath   = udev_device_get_syspath(dev);
            const char *subsystem = udev_device_get_subsystem(dev);
            const char *action    = udev_device_get_action(dev);
            log_debug("action=%s subsystem=%s syspath=%s",
                      umudev_pretty_string(action),
                      umudev_pretty_string(subsystem),
                      umudev_pretty_string(syspath));

            if( g_strcmp0(action, "change") ) {
                // ignore add/remove events
            }
            else if( umudev_android_subsystem && !g_strcmp0(umudev_android_subsystem, subsystem) ) {
                if( !umudev_android_syspath || !g_strcmp0(umudev_android_syspath, syspath) )
                    umudev_android_update_from(dev);
            }
            else if( umudev_extcon_subsystem && !g_strcmp0(umudev_extcon_subsystem, subsystem) ) {
                if( !umudev_extcon_syspath || !g_strcmp0(umudev_extcon_syspath, syspath) )
                    umudev_extcon_update_from(dev);
            }
            else if( umudev_charger_subsystem && !g_strcmp0(umudev_charger_subsystem, subsystem) ) {
                if( !umudev_charger_syspath || !g_strcmp0(umudev_charger_syspath, syspath) ) {
                    umudev_charger_cancel_poll();
                    umudev_charger_update_from(dev);
                }
            }
            udev_device_unref(dev);
        }
    }

    if( cond & (G_IO_ERR | G_IO_HUP | G_IO_NVAL) ) {
        /* Unhandled errors turn io watch to virtual busyloop too */
        continue_watching = FALSE;
    }

    if( !continue_watching && umudev_watch_id ) {
        umudev_watch_id = 0;
        log_crit("udev io watch disabled");
    }

    common_release_wakelock(USB_MODED_WAKELOCK_PROCESS_INPUT);

    return continue_watching;
}

static void umudev_evaluate_state(void)
{
    /* Start from cached charger properties */
    const char *charger_online = umudev_charger_online;
    const char *charger_type   = umudev_charger_type;

    /* Apply heuristics
     *
     * If charger online info is not available or reliable, extcon
     * USB=N can be used as a substitute.
     *
     * In some devices USB=1 means that PC is connected, while in
     * others it could be a charger too...
     *
     * Tracking gadget enumeration / configuration state in android_usb
     * can be used to tell apart pc vs charger in those
     * devices where USB=1 could be either one.
     *
     * Caveat: transient android_usb disconnects do occur also during
     *         gadget configuration changes i.e. due to actions of
     *         usb-moded itself -> processing of extcon/android_usb
     *         changes is delayed in the hope that short lasting
     *         transient states do not get evaluated.
     */

    const char *override_online = NULL;
    const char *override_type   = NULL;

    if( umudev_extcon_state ) {
        if( !strcmp(umudev_extcon_state, "USB=1") ) {
            override_online = "1";
            override_type   = "USB";
        }
        else if( !strcmp(umudev_extcon_state, "USB=0") ) {
            override_type = "USB_DCP";
        }
    }

    if( umudev_android_state ) {
        if( !strcmp(umudev_android_state, "DISCONNECTED") ) {
            override_type = "USB_DCP";
        }
        else {
            override_type   = "USB";
            override_online = "1";
        }
    }

    if( override_type && g_strcmp0(override_type, charger_type) ) {
        log_debug("override charger_type: %s -> %s",
                  umudev_pretty_string(charger_type),
                  umudev_pretty_string(override_type));
        charger_type = override_type;
    }

    if( override_online && g_strcmp0(override_online, charger_online) ) {
        log_debug("override charger_online: %s -> %s",
                  umudev_pretty_string(charger_online),
                  umudev_pretty_string(override_online));
        charger_online = override_online;
    }

    /* Evaluate */

    log_debug("evaluate online=%s type=%s extcon=%s android=%s",
              umudev_pretty_string(charger_online),
              umudev_pretty_string(charger_type),
              umudev_pretty_string(umudev_extcon_state),
              umudev_pretty_string(umudev_android_state));

    bool connected = !g_strcmp0(charger_online, "1");

    /* Unless debug logging has been request via command line,
     * suppress warnings about potential property issues and/or
     * fallback strategies applied (to avoid spamming due to the
     * code below seeing the same property values over and over
     * again also in stable states).
     */
    bool warnings = log_p(LOG_DEBUG);

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

        if( warnings && !charger_online )
            log_err("No usable power supply indicator\n");
        umudev_cable_state_from_udev(CABLE_STATE_DISCONNECTED);
    }
    else {
        /*
         * Power supply type might not exist also :(
         * Send connected event but this will not be able
         * to discriminate between charger/cable.
         */
        if( !charger_type ) {
            if( warnings )
                log_warning("Fallback since cable detection might not be accurate. "
                            "Will connect on any voltage on charger.\n");
            umudev_cable_state_from_udev(CABLE_STATE_PC_CONNECTED);
        }
        else if( !strcmp(charger_type, "USB") ||
                 !strcmp(charger_type, "USB_CDP") ) {
            umudev_cable_state_from_udev(CABLE_STATE_PC_CONNECTED);
        }
        else if( !strcmp(charger_type, "USB_DCP") ||
                 !strcmp(charger_type, "USB_HVDCP") ||
                 !strcmp(charger_type, "USB_HVDCP_3") ) {
            umudev_cable_state_from_udev(CABLE_STATE_CHARGER_CONNECTED);
        }
        else  if( !strcmp(charger_type, "USB_PD") ) {
            /* Looks like it is impossible to tell apart PD connections to
             * pc and chargers based on stable state property values.
             *
             * However, it seems that PD capable power banks and chargers
             * are 1st reported as chargers, then a switch to PD type occurs
             * i.e. we can expect to see sequences like:
             *   Unknown -> USB_DCP -> USB_PD
             *
             * Whereas e.g. a laptop connection is expected to report only
             * non-charger types, or directly:
             *   Unknown -> USB_PD
             *
             * -> Differentiation should be possible by retaining the "this
             *    is a charger" info obtained from transient USB_DCP/similar
             *    states.
             */
            if( umudev_cable_state_current != CABLE_STATE_CHARGER_CONNECTED )
                umudev_cable_state_from_udev(CABLE_STATE_PC_CONNECTED);
        }
        else if( !strcmp(charger_type, "USB_FLOAT") ) {
            if( !umudev_cable_state_connected() )
                log_warning("connection type detection failed, assuming charger");
            umudev_cable_state_from_udev(CABLE_STATE_CHARGER_CONNECTED);
        }
        else if( !strcmp(charger_type, "Unknown") ) {
            log_warning("connection type 'Unknown' reported, assuming disconnected");
            umudev_cable_state_from_udev(CABLE_STATE_DISCONNECTED);
        }
        else {
            if( warnings )
                log_warning("unhandled power supply type: %s", charger_type);
            umudev_cable_state_from_udev(CABLE_STATE_DISCONNECTED);
        }
    }
}

gboolean umudev_init(void)
{
    LOG_REGISTER_CONTEXT;

    gboolean                success = FALSE;

    GIOChannel             *iochannel = 0;

    /* Clear in-cleanup in case of restart */
    umudev_in_cleanup = false;

    /* Create the udev object */
    if( !(umudev_object = udev_new()) ) {
        log_err("Can't create umudev_object\n");
        goto EXIT;
    }

    /* Start monitoring for changes */
    umudev_monitor = udev_monitor_new_from_netlink(umudev_object, "udev");
    if( !umudev_monitor )
    {
        log_err("Unable to monitor the netlink\n");
        /* communicate failure, mainloop will exit and call appropriate clean-up */
        goto EXIT;
    }

    /* Locate relevant devices */
    umudev_charger_find_device();
    umudev_extcon_find_device();
    umudev_android_find_device();

    if( !umudev_charger_syspath ) {
        if( !umudev_extcon_subsystem && !umudev_android_subsystem ) {
            log_warning("no charger device found, bailing out");
            goto EXIT;
        }
        log_debug("no charger device found, using alternative sources");
    }

    if( udev_monitor_enable_receiving(umudev_monitor) != 0 ) {
        log_err("Failed to enable monitor recieving.\n");
        goto EXIT;
    }

    iochannel = g_io_channel_unix_new(udev_monitor_get_fd(umudev_monitor));
    if( !iochannel )
        goto EXIT;

    umudev_watch_id = g_io_add_watch_full(iochannel, 0, G_IO_IN,
                                          umudev_io_input_cb, NULL,
                                          umudev_io_error_cb);
    if( !umudev_watch_id )
        goto EXIT;

    /* everything went well */
    success = TRUE;

    /* check initial status */
    umudev_charger_poll_now();

EXIT:
    /* Cleanup local resources */
    if( iochannel )
        g_io_channel_unref(iochannel);

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

    g_free(umudev_charger_syspath),
        umudev_charger_syspath = 0;
    g_free(umudev_charger_subsystem),
        umudev_charger_subsystem = 0;

    g_free(umudev_extcon_syspath),
        umudev_extcon_syspath = 0;
    g_free(umudev_extcon_subsystem),
        umudev_extcon_subsystem = 0;

    g_free(umudev_android_syspath),
        umudev_android_syspath = 0;
    g_free(umudev_android_subsystem),
        umudev_android_subsystem = 0;

    umudev_cable_state_stop_timer();

    umudev_extcon_set_state(NULL);
    umudev_android_set_state(NULL);
    umudev_charger_set_online(NULL);
    umudev_charger_set_type(NULL);
    umudev_charger_cancel_poll();
}
