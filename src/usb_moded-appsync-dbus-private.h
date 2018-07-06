/*
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Copyright (C) 2018 Jolla Ltd.
 *
 * Author: Philippe De Swert <philippe.de-swert@nokia.com>
 * Author: Philippe De Swert <phdeswer@lumi.maa>
 * Author: Philippe De Swert <philippedeswert@gmail.com>
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

#ifndef  USB_MODED_APPSYNC_DBUS_PRIVATE_H_
# define USB_MODED_APPSYNC_DBUS_PRIVATE_H_

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- dbusappsync -- */

gboolean dbusappsync_init_connection(void);
gboolean dbusappsync_init           (void);
void     dbusappsync_cleanup        (void);
int      dbusappsync_launch_app     (char *launch);

#endif /* USB_MODED_APPSYNC_DBUS_PRIVATE_H_ */
