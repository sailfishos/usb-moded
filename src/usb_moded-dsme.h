/**
 * @file usb_moded-dsme.h
 *
 * Copyright (C) 2013-2018 Jolla. All rights reserved.
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

#ifndef MEEGOLOCK
# warning usb_moded-dsme.h used without enabling MEEGOLOCK
#endif

#ifndef  USB_MODED_DSME_H_
# define USB_MODED_DSME_H_

# include <glib.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- dsme -- */

gboolean dsme_listener_start(void);
void     dsme_listener_stop (void);
gboolean dsme_in_user_state (void);

#endif /* USB_MODED_DSME_H_ */
