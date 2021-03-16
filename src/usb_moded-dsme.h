/**
 * @file usb_moded-dsme.h
 *
 * Copyright (c) 2013 - 2021 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
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

#ifndef MEEGOLOCK
# warning usb_moded-dsme.h used without enabling MEEGOLOCK
#endif

#ifndef  USB_MODED_DSME_H_
# define USB_MODED_DSME_H_

# include <stdbool.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * DSME_STATE
 * ------------------------------------------------------------------------- */

bool dsme_state_is_shutdown(void);
bool dsme_state_is_user    (void);

/* ------------------------------------------------------------------------- *
 * DSME
 * ------------------------------------------------------------------------- */

bool dsme_start_listener(void);
void dsme_stop_listener (void);

#endif /* USB_MODED_DSME_H_ */
