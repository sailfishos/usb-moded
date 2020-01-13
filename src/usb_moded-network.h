/**
 * @file usb_moded-network.h
 *
 * Copyright (c) 2011 Nokia Corporation. All rights reserved.
 * Copyright (c) 2013 - 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author Slava Monich <slava.monich@jolla.com>
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

bool connman_set_tethering(const char *technology, bool on);

/* ------------------------------------------------------------------------- *
 * NETWORK
 * ------------------------------------------------------------------------- */

int  network_update_udhcpd_config(const modedata_t *data);
int  network_up                  (const modedata_t *data);
void network_down                (const modedata_t *data);
void network_update              (void);

#endif /* USB_MODED_NETWORK_H_ */
