/**
 * @file usb_moded-trigger.h
 *
 * Copyright (C) 2011 Nokia Corporation. All rights reserved.
 * Copyright (C) 2018 Jolla Ltd.
 *
 * @author: Philippe De Swert <philippe.de-swert@nokia.com>
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

#ifndef  USB_MODED_TRIGGER_H_
# define USB_MODED_TRIGGER_H_

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- trigger -- */

gboolean trigger_init(void);
void     trigger_stop(void);

#endif /* USB_MODED_TRIGGER_H_ */
