/*
  Copyright (C) 2010 Nokia Corporation. All rights reserved.

  Author: Philippe De Swert <philippe.de-swert@nokia.com>

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

/* initialize dbus communication channels */
gboolean usb_moded_dbus_init(void);

/* cleanup usb on exit */
void usb_moded_dbus_cleanup(void);

/* send signal on system bus */
int usb_moded_send_signal(const char *state_ind);

/* send error signal system bus */
int usb_moded_send_error_signal(const char *error);

/* send supported modes signal system bus */
int usb_moded_send_supported_modes_signal(const char *supported_modes);

/* send hidden modes signal system bus */
int usb_moded_send_hidden_modes_signal(const char *hidden_modes);
