/*
 * Copyright (C) 2011 Nokia Corporation. All rights reserved.
 * Copyright (C) 2013-2019 Jolla Ltd.
 *
 * Author: Philippe De Swert <philippe.de-swert@nokia.com>
 * Author: Philippe De Swert <philippe.deswert@jollamobile.com>
 * Author: Slava Monich <slava.monich@jolla.com>
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
 *
 * usb-moded_network : (De)activates network depending on the network setting system.
 */

#ifndef  USB_MODED_NETWORK_H_
# define USB_MODED_NETWORK_H_

# include "usb_moded-dyn-config.h"

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * CONNMAN
 * ------------------------------------------------------------------------- */

# ifdef CONNMAN
gboolean connman_set_tethering(const char *path, gboolean on);
# endif

/* ------------------------------------------------------------------------- *
 * NETWORK
 * ------------------------------------------------------------------------- */

int network_set_up_dhcpd(modedata_t *data);
int network_up          (modedata_t *data);
int network_down        (modedata_t *data);
int network_update      (void);

#endif /* USB_MODED_NETWORK_H_ */
