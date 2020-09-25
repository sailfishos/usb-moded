/**
 * @file usb_moded-appsync.h
 *
 * Copyright (c) 2010 Nokia Corporation. All rights reserved.
 * Copyright (c) 2013 - 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Philippe De Swert <phdeswer@lumi.maa>
 * @author Philippe De Swert <philippedeswert@gmail.com>
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

#ifndef  USB_MODED_APPSYNC_H_
# define USB_MODED_APPSYNC_H_

# include <stdbool.h>

/* ========================================================================= *
 * Constants
 * ========================================================================= */

# define CONF_DIR_PATH          "/etc/usb-moded/run"
# define CONF_DIR_DIAG_PATH     "/etc/usb-moded/run-diag"

# define APP_INFO_ENTRY         "info"
# define APP_INFO_MODE_KEY      "mode"
# define APP_INFO_NAME_KEY      "name"
# define APP_INFO_LAUNCH_KEY    "launch"
# define APP_INFO_SYSTEMD_KEY   "systemd"  // integer
# define APP_INFO_POST          "post"     // integer

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * APPSYNC
 * ------------------------------------------------------------------------- */

void appsync_switch_configuration(void);
void appsync_free_configuration  (void);
void appsync_load_configuration  (void);
int  appsync_activate_pre        (const char *mode);
int  appsync_activate_post       (const char *mode);
int  appsync_mark_active         (const char *name, int post);
void appsync_deactivate_pre      (void);
void appsync_deactivate_post     (void);
void appsync_deactivate_all      (bool force);

#endif /* USB_MODED_APPSYNC_H_ */
