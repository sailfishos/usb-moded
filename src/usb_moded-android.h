/**
 * @file usb_moded-android.h
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

#ifndef  USB_MODED_ANDROID_H_
# define USB_MODED_ANDROID_H_

# include <stdbool.h>
# include <glib.h>

/* ========================================================================= *
 * Constants
 * ========================================================================= */

# define ANDROID0_DIRECTORY     "/sys/class/android_usb/android0"
# define ANDROID0_ENABLE        "/sys/class/android_usb/android0/enable"
# define ANDROID0_FUNCTIONS     "/sys/class/android_usb/android0/functions"
# define ANDROID0_ID_PRODUCT    "/sys/class/android_usb/android0/idProduct"
# define ANDROID0_ID_VENDOR     "/sys/class/android_usb/android0/idVendor"
# define ANDROID0_MANUFACTURER  "/sys/class/android_usb/android0/iManufacturer"
# define ANDROID0_PRODUCT       "/sys/class/android_usb/android0/iProduct"
# define ANDROID0_SERIAL        "/sys/class/android_usb/android0/iSerial"

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * ANDROID
 * ------------------------------------------------------------------------- */

bool   android_in_use           (void);
gchar *android_get_serial       (void);
bool   android_init             (void);
void   android_quit             (void);
bool   android_set_enabled      (bool enable);
bool   android_set_charging_mode(void);
bool   android_set_function     (const char *function);
bool   android_set_productid    (const char *id);
bool   android_set_vendorid     (const char *id);
bool   android_set_attr         (const char *function, const char *attr, const char *value);

#endif /* USB_MODED_ANDROID_H_ */
