/*
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Copyright (C) 2013-2018 Jolla Ltd.
 *
 * Author: Philippe De Swert <philippe.de-swert@nokia.com>
 * Author: Philippe De Swert <phdeswer@lumi.maa>
 * Author: Philippe De Swert <philippedeswert@gmail.com>
 * Author: Philippe De Swert <philippe.deswert@jollamobile.com>
 * Author: Thomas Perl <m@thp.io>
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

#ifndef  USB_MODED_MODULES_H_
# define USB_MODED_MODULES_H_

# include <stdbool.h>

/* ========================================================================= *
 * Constants
 * ========================================================================= */

/* module name definitions */
# define MODULE_MASS_STORAGE     "g_mass_storage"
# define MODULE_FILE_STORAGE     "g_file_storage"
# define MODULE_CHARGING         "g_mass_storage luns=1 stall=0 removable=1"
# define MODULE_CHARGE_FALLBACK  "g_file_storage luns=1 stall=0 removable=1"
# define MODULE_NONE             "none"
# define MODULE_DEVELOPER        "g_ether"
# define MODULE_MTP              "g_ffs"

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- modules -- */

bool        modules_in_use            (void);
bool        modules_init              (void);
void        modules_quit              (void);
int         modules_load_module       (const char *module);
int         modules_unload_module     (const char *module);

#endif /* USB_MODED_MODULES_H_ */
