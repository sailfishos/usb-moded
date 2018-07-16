/*
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Copyright (C) 2012-2018 Jolla. All rights reserved.
 *
 * @author: Philippe De Swert <philippe.de-swert@nokia.com>
 * @author: Philippe De Swert <philippedeswert@gmail.com>
 * @author: Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author: Simo Piiroinen <simo.piiroinen@jollamobile.com>
 * @author: Andrew den Exter <andrew.den.exter@jolla.com>
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
#  include "../config-static.h"
# else
#  include "../config.h"
# endif

# include <stdlib.h>
# include <stdbool.h>
# include <unistd.h>
# include <stdio.h>
# include <string.h>
# include <errno.h>
# include <fcntl.h>

# include <sys/stat.h>
# include <sys/wait.h>

# include <glib-2.0/glib.h>
# include <glib-object.h>

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
 * Types
 * ========================================================================= */

typedef enum {
    CABLE_STATE_UNKNOWN,
    CABLE_STATE_DISCONNECTED,
    CABLE_STATE_CHARGER_CONNECTED,
    CABLE_STATE_PC_CONNECTED,
    CABLE_STATE_NUMOF
} cable_state_t;

/** Mode list types
 */
typedef enum mode_list_type_t {
    /** All configured modes */
    SUPPORTED_MODES_LIST,
    /** Configured modes that can be activated */
    AVAILABLE_MODES_LIST
} mode_list_type_t;

/* ========================================================================= *
 * Data
 * ========================================================================= */

/** PC connection delay (FIXME: is defunct now)
 *
 * Slow cable insert / similar physical issues can lead to a charger
 * getting initially recognized as a pc connection. This defines how
 * long we should wait and see if pc connection gets corrected to a
 * charger kind.
 */
extern int             usbmoded_cable_connection_delay;

/** Rescue mode flag
 *
 * When enabled, usb-moded allows developer_mode etc when device is
 * booted up with cable connected without requiring device unlock.
 * Which can be useful if UI for some reason does not come up.
 */
extern bool            usbmoded_rescue_mode;

/* ========================================================================= *
 * Functions
 * ========================================================================= */

/* -- cable -- */

const char *cable_state_repr(cable_state_t state);

/* -- usbmoded -- */

void                   usbmoded_rethink_usb_charging_fallback(void);
const char            *usbmoded_get_external_mode            (void);
const char            *usbmoded_get_usb_mode                 (void);
void                   usbmoded_set_usb_mode                 (const char *internal_mode);
void                   usbmoded_select_usb_mode              (void);
void                   usbmoded_set_cable_state              (cable_state_t cable_state);
cable_state_t          usbmoded_get_cable_state              (void);
bool                   usbmoded_get_connection_state         (void);
int                    usbmoded_valid_mode                   (const char *mode);
gchar                 *usbmoded_get_mode_list                (mode_list_type_t type);
const char            *usbmoded_get_usb_module               (void);
bool                   usbmoded_set_usb_module               (const char *module);
struct mode_list_elem *usbmoded_get_usb_mode_data            (void);
void                   usbmoded_set_usb_mode_data            (struct mode_list_elem *data);
void                   usbmoded_send_supported_modes_signal  (void);
void                   usbmoded_send_available_modes_signal  (void);
void                   usbmoded_send_hidden_modes_signal     (void);
void                   usbmoded_send_whitelisted_modes_signal(void);
void                   usbmoded_acquire_wakelock             (const char *wakelock_name);
void                   usbmoded_release_wakelock             (const char *wakelock_name);
void                   usbmoded_allow_suspend                (void);
void                   usbmoded_delay_suspend                (void);
bool                   usbmoded_can_export                  (void);
bool                   usbmoded_init_done_p                  (void);
void                   usbmoded_set_init_done                (bool reached);
void                   usbmoded_probe_init_done              (void);
void                   usbmoded_exit_mainloop                (int exitcode);
int                    usbmoded_system_                      (const char *file, int line, const char *func, const char *command);
FILE                  *usbmoded_popen_                       (const char *file, int line, const char *func, const char *command, const char *type);
void                   usbmoded_usleep_                      (const char *file, int line, const char *func, useconds_t usec);

/* ========================================================================= *
 * Macros
 * ========================================================================= */

# define               usbmoded_system(command)      usbmoded_system_(__FILE__,__LINE__,__FUNCTION__,(command))
# define               usbmoded_popen(command, type) usbmoded_popen_(__FILE__,__LINE__,__FUNCTION__,(command),(type))
# define               usbmoded_usleep(usec)         usbmoded_usleep_(__FILE__,__LINE__,__FUNCTION__,(usec))
# define               usbmoded_msleep(msec)         usbmoded_usleep_(__FILE__,__LINE__,__FUNCTION__,(msec)*1000)
# define               usbmoded_sleep(sec)           usbmoded_usleep_(__FILE__,__LINE__,__FUNCTION__,(sec)*1000000)

#endif /* USB_MODED_H_ */
