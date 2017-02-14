/*
  Copyright (C) 2012 Nokia Corporation. All rights reserved.
  Copyright (C) 2013-2016 Jolla Ltd.

  Author: Philippe De Swert <philippe.de-swert@nokia.com>
  Author: Philippe De Swert <philippe.deswert@jollamobile.com>
  Author: Simo Piiroinen <simo.piiroinen@jollamobile.com>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the Lesser GNU General Public License 
  version 2 as published by the Free Software Foundation. 

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.
 
  You should have received a copy of the Lesser GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
  02110-1301 USA
*/
/*
 * Function definitions to interact with a security lock to know if 
   we can expose the system contents or not
 */

/*============================================================================= */
gboolean usb_moded_get_export_permission(void);
gboolean start_devicelock_listener(void);
void     stop_devicelock_listener(void);
