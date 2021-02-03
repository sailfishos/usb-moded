/**
 * @file usb_moded-common.h
 *
 * Copyright (c) 2010 Nokia Corporation. All rights reserved.
 * Copyright (c) 2012 - 2021 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
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

#ifndef  USB_MODED_COMMON_H_
# define USB_MODED_COMMON_H_

# include <stdio.h>
# include <stdbool.h>
# include <glib.h>

/* ========================================================================= *
 * Types
 * ========================================================================= */

/** Mode list types
 */
typedef enum mode_list_type_t {
    /** All configured modes */
    SUPPORTED_MODES_LIST,
    /** Configured modes that can be activated */
    AVAILABLE_MODES_LIST
} mode_list_type_t;

typedef enum {
    CABLE_STATE_UNKNOWN,
    CABLE_STATE_DISCONNECTED,
    CABLE_STATE_CHARGER_CONNECTED,
    CABLE_STATE_PC_CONNECTED,
    CABLE_STATE_NUMOF
} cable_state_t;

typedef enum waitres_t
{
    WAIT_FAILED,
    WAIT_READY,
    WAIT_TIMEOUT,
} waitres_t;

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * CABLE_STATE
 * ------------------------------------------------------------------------- */

const char *cable_state_repr(cable_state_t state);

/* ------------------------------------------------------------------------- *
 * COMMON
 * ------------------------------------------------------------------------- */

const char *common_map_mode_to_hardware         (const char *internal_mode);
const char *common_map_mode_to_external         (const char *internal_mode);
void        common_send_supported_modes_signal  (void);
void        common_send_available_modes_signal  (void);
void        common_send_hidden_modes_signal     (void);
void        common_send_whitelisted_modes_signal(void);
void        common_acquire_wakelock             (const char *wakelock_name);
void        common_release_wakelock             (const char *wakelock_name);
int         common_system_                      (const char *file, int line, const char *func, const char *command);
FILE       *common_popen_                       (const char *file, int line, const char *func, const char *command, const char *type);
waitres_t   common_wait                         (unsigned tot_ms, bool (*ready_cb)(void *aptr), void *aptr);
bool        common_msleep_                      (const char *file, int line, const char *func, unsigned msec);
bool        common_modename_is_internal         (const char *modename);
bool        common_modename_is_static           (const char *modename);
int         common_valid_mode                   (const char *mode);
gchar      *common_get_mode_list                (mode_list_type_t type, uid_t uid);

/* ========================================================================= *
 * Macros
 * ========================================================================= */

# define               common_system(command)      common_system_(__FILE__,__LINE__,__FUNCTION__,(command))
# define               common_popen(command, type) common_popen_(__FILE__,__LINE__,__FUNCTION__,(command),(type))
# define               common_msleep(msec)         common_msleep_(__FILE__,__LINE__,__FUNCTION__,(msec))
# define               common_sleep(sec)           common_msleep_(__FILE__,__LINE__,__FUNCTION__,(sec)*1000)

/* ========================================================================= *
 * Constants
 * ========================================================================= */
# define UID_UNKNOWN ((uid_t)-1)

#endif /* USB_MODED_COMMON_H_ */
