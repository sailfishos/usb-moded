/**
 * @file usb_moded-configfs.h
 *
 * Copyright (C) 2018-2019 Jolla. All rights reserved.
 *
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

#ifndef  USB_MODED_CONFIGFS_H_
# define USB_MODED_CONFIGFS_H_

# include <stdbool.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * CONFIGFS
 * ------------------------------------------------------------------------- */

bool configfs_in_use                 (void);
bool configfs_set_udc                (bool enable);
bool configfs_init                   (void);
void configfs_quit                   (void);
bool configfs_set_charging_mode      (void);
bool configfs_set_productid          (const char *id);
bool configfs_set_vendorid           (const char *id);
bool configfs_set_function           (const char *functions);
bool configfs_add_mass_storage_lun   (int lun);
bool configfs_remove_mass_storage_lun(int lun);
bool configfs_set_mass_storage_attr  (int lun, const char *attr, const char *value);

#endif /* USB_MODED_CONFIGFS_H_ */
