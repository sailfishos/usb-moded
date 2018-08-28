/**
 * @file usb_moded-control.c
 *
 * Copyright (C) 2013-2018 Jolla. All rights reserved.
 *
 * @author: Philippe De Swert <philippe.deswert@jollamobile.com>
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

#include "usb_moded-control.h"

#include "usb_moded-config-private.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded-dyn-config.h"
#include "usb_moded-log.h"
#include "usb_moded-modes.h"
#include "usb_moded-worker.h"

#include <string.h>
#include <stdlib.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- usbmoded -- */

void           control_rethink_usb_charging_fallback(void);
const char    *control_get_external_mode            (void);
static void    control_set_external_mode            (const char *mode);
void           control_clear_external_mode          (void);
static void    control_update_external_mode         (void);
const char    *control_get_usb_mode                 (void);
void           control_clear_internal_mode          (void);
void           control_set_usb_mode                 (const char *mode);
void           control_mode_switched                (const char *override);
void           control_select_usb_mode              (void);
void           control_set_cable_state              (cable_state_t cable_state);
cable_state_t  control_get_cable_state              (void);
void           control_clear_cable_state            (void);
bool           control_get_connection_state         (void);

/* ========================================================================= *
 * Data
 * ========================================================================= */

/* The external mode;
 *
 * What was the last mode signaled over D-Bus.
 */
static char *control_external_mode = NULL;

/** The logical mode name
 *
 * Full set of valid modes can occur here
 */
static char *control_internal_mode = NULL;

/** Connection status
 *
 * Access only via:
 * - control_set_cable_state()
 * - control_get_connection_state()
 */
static cable_state_t control_cable_state = CABLE_STATE_UNKNOWN;

/* ========================================================================= *
 * Functions
 * ========================================================================= */

/** Check if we can/should leave charging fallback mode
 *
 * Called when device lock status, or device status (dsme)
 * changes.
 */
void
control_rethink_usb_charging_fallback(void)
{
    /* Cable must be connected to a pc */
    if( control_get_cable_state() != CABLE_STATE_PC_CONNECTED )
        goto EXIT;

    /* Switching can happen only from MODE_UNDEFINED
     * or MODE_CHARGING_FALLBACK */
    const char *usb_mode = control_get_usb_mode();

    if( strcmp(usb_mode, MODE_UNDEFINED) &&
        strcmp(usb_mode, MODE_CHARGING_FALLBACK) )
        goto EXIT;

    if( !usbmoded_can_export() ) {
        log_notice("exporting data not allowed; stay in %s", usb_mode);
        goto EXIT;
    }

    log_debug("attempt to leave %s", usb_mode);
    control_select_usb_mode();

EXIT:
    return;
}

const char *control_get_external_mode(void)
{
    return control_external_mode ?: MODE_UNDEFINED;
}

static void control_set_external_mode(const char *mode)
{
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
        umdbus_send_state_signal(USB_CONNECTED_DIALOG_SHOW);
    }

    umdbus_send_state_signal(control_external_mode);

EXIT:
    return;
}

void control_clear_external_mode(void)
{
    g_free(control_external_mode),
        control_external_mode = 0;
}

static void control_update_external_mode(void)
{
    const char *internal_mode = control_get_usb_mode();
    const char *external_mode = common_map_mode_to_external(internal_mode);

    control_set_external_mode(external_mode);
}

/** get the usb mode
 *
 * @return the currently set mode
 *
 */
const char * control_get_usb_mode(void)
{
    return control_internal_mode;
}

void control_clear_internal_mode(void)
{
    g_free(control_internal_mode),
        control_internal_mode = 0;
}

/** set the usb mode
 *
 * @param mode The requested USB mode
 */
void control_set_usb_mode(const char *mode)
{
    gchar *previous = control_internal_mode;
    if( !g_strcmp0(previous, mode) )
        goto EXIT;

    log_debug("internal_mode: %s -> %s",
              previous, mode);

    control_internal_mode = g_strdup(mode);
    g_free(previous);

    /* Invalidate current mode for the duration of mode transition */
    control_set_external_mode(MODE_BUSY);

    /* Propagate down to gadget config */
    worker_request_hardware_mode(control_internal_mode);

EXIT:
    return;
}

/* Worker thread has finished mode switch
 *
 * @param mode The activated USB mode
 */
void control_mode_switched(const char *mode)
{
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

/** set the chosen usb state
 *
 * gauge what mode to enter and then call control_set_usb_mode()
 *
 */
void control_select_usb_mode(void)
{
    char *mode_to_set = 0;

    if( usbmoded_get_rescue_mode() ) {
        log_debug("Entering rescue mode!\n");
        control_set_usb_mode(MODE_DEVELOPER);
        goto EXIT;
    }

    if( usbmoded_get_diag_mode() ) {
        /* Assumption is that in diag-mode there is only
         * one mode configured i.e. list head is diag-mode. */
        GList *iter = usbmoded_get_modelist();
        if( !iter ) {
            log_err("Diagnostic mode is not configured!");
        }
        else {
            struct mode_list_elem *data = iter->data;
            log_debug("Entering diagnostic mode!");
            control_set_usb_mode(data->mode_name);
        }
        goto EXIT;
    }

    mode_to_set = config_get_mode_setting();

    /* If there is only one allowed mode, use it without
     * going through ask-mode */
    if( !strcmp(MODE_ASK, mode_to_set) ) {
        // FIXME free() vs g_free() conflict
        gchar *available = common_get_mode_list(AVAILABLE_MODES_LIST);
        if( *available && !strchr(available, ',') ) {
            free(mode_to_set), mode_to_set = available, available = 0;
        }
        g_free(available);
    }

    if( mode_to_set && usbmoded_can_export() ) {
        control_set_usb_mode(mode_to_set);
    }
    else {
        /* config is corrupted or we do not have a mode configured, fallback to charging
         * We also fall back here in case the device is locked and we do not
         * export the system contents. Or if we are in acting dead mode.
         */
        control_set_usb_mode(MODE_CHARGING_FALLBACK);
    }
EXIT:
    free(mode_to_set);
}

/** set the usb connection status
 *
 * @param cable_state CABLE_STATE_DISCONNECTED, ...
 */
void control_set_cable_state(cable_state_t cable_state)
{
    cable_state_t prev = control_cable_state;
    control_cable_state = cable_state;

    if( control_cable_state == prev )
        goto EXIT;

    log_debug("control_cable_state: %s -> %s",
              cable_state_repr(prev),
              cable_state_repr(control_cable_state));

    switch( control_cable_state ) {
    default:
    case CABLE_STATE_DISCONNECTED:
        control_set_usb_mode(MODE_UNDEFINED);
        break;
    case CABLE_STATE_CHARGER_CONNECTED:
        control_set_usb_mode(MODE_CHARGER);
        break;
    case CABLE_STATE_PC_CONNECTED:
        control_select_usb_mode();
        break;
    }

EXIT:
    return;
}

/** get the usb connection status
 *
 * @return CABLE_STATE_DISCONNECTED, ...
 */
cable_state_t control_get_cable_state(void)
{
    return control_cable_state;
}

void control_clear_cable_state(void)
{
    control_cable_state = CABLE_STATE_UNKNOWN;
}

/** Get if the cable (pc or charger) is connected or not
 *
 * @ return true if connected, false if disconnected
 */
bool control_get_connection_state(void)
{
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
