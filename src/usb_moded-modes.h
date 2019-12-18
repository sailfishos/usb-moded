/**
 * @file usb_moded-modes.h
 *
 * Copyright (c) 2010 Nokia Corporation. All rights reserved.
 * Copyright (c) 2013 - 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Philippe De Swert <philippedeswert@gmail.com>
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author Thomas Perl <m@thp.io>
 * @author Martin Jones <martin.jones@jollamobile.com>
 * @author Andrew den Exter <andrew.den.exter@jolla.com>
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

#ifndef  USB_MODED_MODES_H_
# define USB_MODED_MODES_H_

/* ========================================================================= *
 * Constants
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * Internal modes
 *
 * These modes are defined internally within usb-moded and are thus
 * always available.
 *
 * Generally speaking these are also activated automatically and thus
 * not really selectable - except:
 * - MODE_ASK which can be set as default mode
 * - MODE_CHARGING which can be acticated on request too
 * ------------------------------------------------------------------------- */

/** No cable connected */
# define MODE_UNDEFINED          "undefined"

/** Pending mode activation
 *
 * Used for signaling "in between modes" state.
 */
# define MODE_BUSY               "busy"

/** Connected to a dedicated charger */
# define MODE_CHARGER            "dedicated_charger"

/** Blocked mode selection
 *
 * While prerequisites for dynamic mode activation are not met e.g.
 * device is locked, pc connection is used for charging.
 */
# define MODE_CHARGING_FALLBACK  "charging_only_fallback"

/** Pending mode selection
 *
 * While mode selection dialog is shown to user, pc connection
 * is used for charging.
 */
# define MODE_ASK                "ask"

/** Charging only selected */
# define MODE_CHARGING           "charging_only"

/* ------------------------------------------------------------------------- *
 * Dynamic modes
 *
 * These modes are defined in usb-moded configuration files.
 *
 * From usb-moded point of view mode names have no special meaning,
 * but a set of known values is still defined (and are likely to
 * have localized name presentation in UI context).
 * ------------------------------------------------------------------------- */

# define MODE_MASS_STORAGE       "mass_storage"
# define MODE_DEVELOPER          "developer_mode"
# define MODE_MTP                "mtp_mode"
# define MODE_HOST               "host_mode"
# define MODE_CONNECTION_SHARING "connection_sharing"
# define MODE_DIAG               "diag_mode"
# define MODE_ADB                "adb_mode"
# define MODE_PC_SUITE           "pc_suite"

#endif /* USB_MODED_MODES_H_ */
