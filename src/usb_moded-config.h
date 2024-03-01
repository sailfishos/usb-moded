/**
 * @file usb_moded-config.h
 *
 * Copyright (c) 2010 Nokia Corporation. All rights reserved.
 * Copyright (c) 2012 - 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Philippe De Swert <phdeswer@lumi.maa>
 * @author Philippe De Swert <philippedeswert@gmail.com>
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author Thomas Perl <m@thp.io>
 * @author Slava Monich <slava.monich@jolla.com>
 * @author Simo Piiroinen <simo.piiroinen@jollamobile.com>
 * @author Andrew den Exter <andrew.den.exter@jolla.com>
 *
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

#ifndef  USB_MODED_CONFIG_H_
# define USB_MODED_CONFIG_H_

/* ========================================================================= *
 * Constants
 * ========================================================================= */

/* [usbmode] */
# define MODE_SETTING_ENTRY              "usbmode"

# define MODE_SETTING_KEY                "mode"
# define MODE_HIDE_KEY                   "hide"
# define MODE_WHITELIST_KEY              "whitelist"

# define FS_MOUNT_DEFAULT                "/dev/mmcblk0p1"
# define FS_MOUNT_ENTRY                  "mountpoints"
# define FS_MOUNT_KEY                    "mount"
# define FS_SYNC_ENTRY                   "sync"
# define FS_SYNC_KEY                     "nofua"

# define ALT_MOUNT_ENTRY                 "altmount"
# define ALT_MOUNT_KEY                   "mount"

/* [udev]
 *
 * # Charger device tracking: enabled by default, uses 'usb' device
 * # if present, otherwise scans for the best candidate among
 * # all devices in 'power_supply' subsystem.
 *
 * charger_tracking = 1
 * path = /sys/class/power_supply/usb
 * subsystem = power_supply
 *
 * # Extcon device tracking: enabled by default. If enabled
 * # tracks all devices in 'extcon' subsystem for USB=N changes.
 * # In case of multiple device nodes providing conflicting
 * # state information, device path needs to be explicitly given.
 *
 * extcon_tracking = 1
 * extcon_path = null
 * extcon_subsystem = extcon
 *
 * # Android usb device tracking: disabled by default. If enabled
 * # tracks state of 'android0 device' / all devices in
 * # 'android_usb' subsystem devices that have state property.
 *
 * android_tracking = 0
 * android_path = /sys/class/android_usb/android0
 * android_subsystem = android_usb
 */
# define UDEV_ENTRY                      "udev"

# define UDEV_CHARGER_TRACKING_KEY       "charger_tracking"
# define UDEV_CHARGER_TRACKING_FALLBACK  "1"
# define UDEV_CHARGER_PATH_KEY           "path"
# define UDEV_CHARGER_PATH_FALLBACK      "/sys/class/power_supply/usb"
# define UDEV_CHARGER_SUBSYSTEM_KEY      "subsystem"
# define UDEV_CHARGER_SUBSYSTEM_FALLBACK "power_supply"

# define UDEV_EXTCON_TRACKING_KEY        "extcon_tracking"
# define UDEV_EXTCON_TRACKING_FALLBACK   "1"
# define UDEV_EXTCON_PATH_KEY            "extcon_path"
# define UDEV_EXTCON_PATH_FALLBACK       NULL
# define UDEV_EXTCON_SUBSYSTEM_KEY       "extcon_subsystem"
# define UDEV_EXTCON_SUBSYSTEM_FALLBACK  "extcon"

# define UDEV_ANDROID_TRACKING_KEY       "android_tracking"
# define UDEV_ANDROID_TRACKING_FALLBACK  "0"
# define UDEV_ANDROID_PATH_KEY           "android_path"
# define UDEV_ANDROID_PATH_FALLBACK      "/sys/class/android_usb/android0"
# define UDEV_ANDROID_SUBSYSTEM_KEY      "android_subsystem"
# define UDEV_ANDROID_SUBSYSTEM_FALLBACK "android_usb"

/* For compatibility with older versions of dev package headers */
# define UDEV_PATH_ENTRY                 UDEV_ENTRY
# define UDEV_PATH_KEY                   UDEV_CHARGER_PATH_KEY
# define UDEV_SUBSYSTEM_KEY              UDEV_CHARGER_SUBSYSTEM_KEY

/* [cdrom] */
# define CDROM_ENTRY                     "cdrom"
# define CDROM_PATH_KEY                  "path"
# define CDROM_TIMEOUT_KEY               "timeout"

/* [trigger] */
# define TRIGGER_ENTRY                   "trigger"
# define TRIGGER_PATH_KEY                "path"
# define TRIGGER_UDEV_SUBSYSTEM          "udev_subsystem"
# define TRIGGER_MODE_KEY                "mode"
# define TRIGGER_PROPERTY_KEY            "property"
# define TRIGGER_PROPERTY_VALUE_KEY      "value"

/* [network] */
# define NETWORK_ENTRY                   "network"
# define NETWORK_IP_KEY                  "ip"
# define NETWORK_IP_FALLBACK             "192.168.2.15"
# define NETWORK_INTERFACE_KEY           "interface"
# define NETWORK_INTERFACE_FALLBACK      "usb0"
# define NETWORK_GATEWAY_KEY             "gateway"
# define NETWORK_GATEWAY_FALLBACK        NULL
# define NETWORK_NAT_INTERFACE_KEY       "nat_interface"
# define NETWORK_NAT_INTERFACE_FALLBACK  NULL
# define NETWORK_NETMASK_KEY             "netmask"
# define NETWORK_NETMASK_FALLBACK        "255.255.255.0"
# define NO_ROAMING_KEY                  "noroaming"

/* [android] */
# define ANDROID_ENTRY                   "android"
# define ANDROID_MANUFACTURER_KEY        "iManufacturer"
# define ANDROID_VENDOR_ID_KEY           "idVendor"
# define ANDROID_PRODUCT_KEY             "iProduct"
# define ANDROID_PRODUCT_ID_KEY          "idProduct"

/* [mode_group] */
# define MODE_GROUP_ENTRY                "mode_group"

/* ========================================================================= *
 * Types
 * ========================================================================= */

/** Configuration change result
 */
typedef enum set_config_result_t {
    SET_CONFIG_ERROR = -1,  /**< Value change failed */
    SET_CONFIG_UPDATED,     /**< Value change succeeded */
    SET_CONFIG_UNCHANGED,   /**< Value did not change */
} set_config_result_t;

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

// (in usb_moded-config-private.h)

#endif /* USB_MODED_CONFIG_H_ */
