/**
  @file usb_moded-modesetting.h
 
  Copyright (C) 2010 Nokia Corporation. All rights reserved.

  @author: Philippe De Swert <philippe.de-swert@nokia.com>

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

#include "usb_moded-dyn-config.h"

int write_to_file_real(const char *file, int line, const char *func, const char *path, const char *text);

#define write_to_file(path,text)\
   write_to_file_real(__FILE__,__LINE__,__FUNCTION__,(path),(text))

int set_mtp_mode(void);
int set_dynamic_mode(void);
void unset_dynamic_mode(void);
/* clean up for the mode changes on disconnect */
int usb_moded_mode_cleanup(const char *module);
void usb_moded_mode_verify_values(void);
void usb_moded_mode_init(void);
void usb_moded_mode_quit(void);
