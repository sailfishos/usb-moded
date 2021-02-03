/**
 * @file usb_moded-user.h
 *
 * Copyright (c) 2021 Open Mobile Platform LLC.
 * Copyright (c) 2021 Jolla Ltd.
 *
 * @author Mike Salmela <mike.salmela@jolla.com>
 * @author Simo Piiroinen <simo.piiroinen@jolla.com>
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

#ifndef  USB_MODED_USER_H_
# define USB_MODED_USER_H_

# ifndef MEEGOLOCK
#  warning usb_moded-user.h used without enabling MEEGOLOCK
# endif

# include <stdbool.h>
# include <unistd.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * USER
 * ------------------------------------------------------------------------- */

uid_t user_get_current_user(void);

/* ------------------------------------------------------------------------- *
 * USER_WATCH
 * ------------------------------------------------------------------------- */

bool user_watch_init(void);
void user_watch_stop(void);

#endif /* USB_MODED_USER_H_ */
