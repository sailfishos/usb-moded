/**
 * @file usb_moded-worker.h
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

#ifndef  USB_MODED_WORKER_H_
# define USB_MODED_WORKER_H_

# include "usb_moded-dyn-config.h"

/* ========================================================================= *
 * Constants
 * ========================================================================= */

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * WORKER
 * ------------------------------------------------------------------------- */

bool              worker_bailing_out          (void);
const char       *worker_get_kernel_module    (void);
bool              worker_set_kernel_module    (const char *module);
void              worker_clear_kernel_module  (void);
const modedata_t *worker_get_usb_mode_data    (void);
modedata_t       *worker_dup_usb_mode_data    (void);
void              worker_set_usb_mode_data    (const modedata_t *data);
bool              worker_request_hardware_mode(const char *mode);
void              worker_clear_hardware_mode  (void);
bool              worker_init                 (void);
void              worker_quit                 (void);
void              worker_wakeup               (void);

#endif /* USB_MODED_WORKER_H_ */
