/**
 * @file usb_moded-control.h
 *
 * Copyright (C) 2013-2019 Jolla. All rights reserved.
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

#ifndef  USB_MODED_CONTROL_H_
# define USB_MODED_CONTROL_H_

# include "usb_moded.h"
# include "usb_moded-common.h"

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * CONTROL
 * ------------------------------------------------------------------------- */

void           control_rethink_usb_charging_fallback(void);
const char    *control_get_external_mode            (void);
void           control_clear_external_mode          (void);
const char    *control_get_target_mode              (void);
void           control_clear_target_mode            (void);
const char    *control_get_usb_mode                 (void);
void           control_clear_internal_mode          (void);
void           control_set_usb_mode                 (const char *mode);
void           control_mode_switched                (const char *mode);
void           control_select_usb_mode              (void);
void           control_set_cable_state              (cable_state_t cable_state);
cable_state_t  control_get_cable_state              (void);
void           control_clear_cable_state            (void);
bool           control_get_connection_state         (void);
void           control_set_last_seen_user           (uid_t uid);

#endif /* USB_MODED_CONTROL_H_ */
