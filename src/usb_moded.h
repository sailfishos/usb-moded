/**
 * @file usb_moded.h
 *
 * Copyright (c) 2010 Nokia Corporation. All rights reserved.
 * Copyright (c) 2012 - 2021 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Philippe De Swert <philippedeswert@gmail.com>
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
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

#ifndef  USB_MODED_H_
# define USB_MODED_H_

# ifdef STATIC_CONFIG
#  include "../config-static.h" // NOTRIM
# else
#  include "../config.h" // NOTRIM
# endif

# include "usb_moded-dyn-config.h"

/* ========================================================================= *
 * Constants
 * ========================================================================= */

# define USB_MODED_LOCKFILE                     "/var/run/usb_moded.pid"

/** Name of the wakelock usb_moded uses for temporary suspend delay */
# define USB_MODED_WAKELOCK_STATE_CHANGE        "usb_moded_state"

/** Name of the wakelock usb_moded uses for input processing */
# define USB_MODED_WAKELOCK_PROCESS_INPUT       "usb_moded_input"

/** How long usb_moded will delay suspend by default [ms] */
# define USB_MODED_SUSPEND_DELAY_DEFAULT_MS      5000

/** How long usb_moded is allowed to block suspend [ms] */
# define USB_MODED_SUSPEND_DELAY_MAXIMUM_MS \
     (USB_MODED_SUSPEND_DELAY_DEFAULT_MS * 2)

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

/* ------------------------------------------------------------------------- *
 * MAIN
 * ------------------------------------------------------------------------- */

int main(int argc, char *argv[]);

#endif /* USB_MODED_H_ */
