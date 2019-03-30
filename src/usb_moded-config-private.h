/*
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Copyright (C) 2012-2019 Jolla. All rights reserved.
 *
 * Author: Philippe De Swert <philippe.de-swert@nokia.com>
 * Author: Philippe De Swert <philippedeswert@gmail.com>
 * Author: Philippe De Swert <philippe.deswert@jollamobile.com>
 * Author: Slava Monich <slava.monich@jolla.com>
 * Author: Martin Jones <martin.jones@jollamobile.com>
 * Author: Andrew den Exter <andrew.den.exter@jolla.com>
 * Author: Simo Piiroinen <simo.piiroinen@jollamobile.com>
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

/*
 * Gets/sets information for the usb modes from dbus
 */

/*============================================================================= */

#ifndef  USB_MODED_CONFIG_PRIVATE_H_
# define USB_MODED_CONFIG_PRIVATE_H_

# include "usb_moded-config.h"

# include <stdbool.h>
# include <glib.h>

/* ========================================================================= *
 * Constants
 * ========================================================================= */

# define USB_MODED_STATIC_CONFIG_DIR    "/etc/usb-moded"
# define USB_MODED_STATIC_CONFIG_FILE   USB_MODED_STATIC_CONFIG_DIR"/usb-moded.ini"

# define USB_MODED_DYNAMIC_CONFIG_DIR    "/var/lib/usb-moded"
# define USB_MODED_DYNAMIC_CONFIG_FILE   USB_MODED_DYNAMIC_CONFIG_DIR"/usb-moded.ini"

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * CONFIG
 * ------------------------------------------------------------------------- */

char                *config_find_mounts             (void);
int                  config_find_sync               (void);
char                *config_find_alt_mount          (void);
char                *config_find_udev_path          (void);
char                *config_find_udev_subsystem     (void);
char                *config_check_trigger           (void);
char                *config_get_trigger_subsystem   (void);
char                *config_get_trigger_mode        (void);
char                *config_get_trigger_property    (void);
char                *config_get_trigger_value       (void);
char                *config_get_conf_string         (const gchar *entry, const gchar *key);
char                *config_get_mode_setting        (void);
set_config_result_t  config_set_config_setting      (const char *entry, const char *key, const char *value);
set_config_result_t  config_set_mode_setting        (const char *mode);
set_config_result_t  config_set_hide_mode_setting   (const char *mode);
set_config_result_t  config_set_unhide_mode_setting (const char *mode);
set_config_result_t  config_set_mode_whitelist      (const char *whitelist);
set_config_result_t  config_set_mode_in_whitelist   (const char *mode, int allowed);
set_config_result_t  config_set_network_setting     (const char *config, const char *setting);
char                *config_get_network_setting     (const char *config);
bool                 config_init                    (void);
char                *config_get_android_manufacturer(void);
char                *config_get_android_vendor_id   (void);
char                *config_get_android_product     (void);
char                *config_get_android_product_id  (void);
char                *config_get_hidden_modes        (void);
char                *config_get_mode_whitelist      (void);
int                  config_is_roaming_not_allowed  (void);

/* ========================================================================= *
 * Macros
 * ========================================================================= */

# define SET_CONFIG_OK(ret) ((ret) >= SET_CONFIG_UPDATED)

#endif /* USB_MODED_CONFIG_PRIVATE_H_ */
