/**
 * @file usb_moded-devicelock.h
 *
 * Copyright (c) 2010 Nokia Corporation. All rights reserved.
 * Copyright (c) 2013 - 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Vesa Halttunen <vesa.halttunen@jollamobile.com>
 * @author Simo Piiroinen <simo.piiroinen@jollamobile.com>
 * @author Andrew den Exter <andrew.den.exter@jolla.com>
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
 * Interacts with the devicelock to know if we can expose the system contents or not
 */

#ifndef  USB_MODED_DEVICELOCK_H_
# define USB_MODED_DEVICELOCK_H_

# include <stdbool.h>

/* ========================================================================= *
 * Constants
 * ========================================================================= */

# define DEVICELOCK_SERVICE              "org.nemomobile.devicelock"
# define DEVICELOCK_OBJECT               "/devicelock"
# define DEVICELOCK_INTERFACE            "org.nemomobile.lipstick.devicelock"
# define DEVICELOCK_GET_STATE_REQ        "state"
# define DEVICELOCK_STATE_CHANGED_SIG    "stateChanged"

# define DEVICELOCK_STATE_CHANGED_MATCH\
     "type='signal'"\
     ",interface='"DEVICELOCK_INTERFACE"'"\
     ",path='"DEVICELOCK_OBJECT"'"\
     ",member='"DEVICELOCK_STATE_CHANGED_SIG"'"

# define DEVICELOCK_NAME_OWNER_CHANGED_MATCH\
     "type='signal'"\
     ",interface='"DBUS_INTERFACE_DBUS"'"\
     ",member='"DBUS_NAME_OWNER_CHANGED_SIG"'"\
     ",arg0='"DEVICELOCK_SERVICE"'"

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * DEVICELOCK
 * ------------------------------------------------------------------------- */

bool devicelock_have_export_permission(void);
bool devicelock_start_listener        (void);
void devicelock_stop_listener         (void);

#endif /* USB_MODED_DEVICELOCK_H_ */
