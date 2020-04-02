/**
 * @file usb_moded-modesetting.h
 *
 * Copyright (c) 2010 Nokia Corporation. All rights reserved.
 * Copyright (c) 2013 - 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Philippe De Swert <phdeswer@lumi.maa>
 * @author Philippe De Swert <philippedeswert@gmail.com>
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author Thomas Perl <m@thp.io>
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

#ifndef  USB_MODED_MODESETTING_H_
# define USB_MODED_MODESETTING_H_

# include <stdbool.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * MODESETTING
 * ------------------------------------------------------------------------- */

void modesetting_verify_values     (void);
int  modesetting_write_to_file_real(const char *file, int line, const char *func, const char *path, const char *text);
bool modesetting_is_mounted        (const char *mountpoint);
bool modesetting_mount             (const char *mountpoint);
bool modesetting_unmount           (const char *mountpoint);
bool modesetting_enter_dynamic_mode(void);
void modesetting_leave_dynamic_mode(void);
void modesetting_init              (void);
void modesetting_quit              (void);

/* ========================================================================= *
 * Macros
 * ========================================================================= */

# define write_to_file(path,text)\
     modesetting_write_to_file_real(__FILE__,__LINE__,__FUNCTION__,(path),(text))

/* Used to retry syscalls that can return EINTR. Taken from bionic unistd.h */
#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(exp) ({         \
    __typeof__(exp) _rc;                   \
    do {                                   \
        _rc = (exp);                       \
    } while (_rc == -1 && errno == EINTR); \
    _rc; })
#endif

#endif /* USB_MODED_MODESETTING_H_ */
