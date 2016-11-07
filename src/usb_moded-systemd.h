/*
 
  Copyright (C) 2013-2016 Jolla Oy. All rights reserved.

  author: Philippe De Swert <philippe.deswert@njollamobile.com>
  author: Simo Piiroinen <simo.piiroinen@jollamobile.com>

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

#define SYSTEMD_STOP	"StopUnit"
#define SYSTEMD_START   "StartUnit"

gboolean systemd_control_service(const char *name, const char *method);
gboolean systemd_control_start(void);
void     systemd_control_stop(void);
