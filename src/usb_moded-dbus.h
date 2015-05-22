/*
  Copyright (C) 2010 Nokia Corporation. All rights reserved.
  Copyright (C) 2012-2015 Jolla. All rights reserved.

  Author: Philippe De Swert <philippe.de-swert@meego.com>

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

#include <dbus/dbus.h>
/**
  * @credential usb-moded::usb-moded-dbus-bind  Credential needed to connect to the bus
**/
#define USB_MODE_SERVICE		"com.meego.usb_moded"
#define USB_MODE_INTERFACE		"com.meego.usb_moded"
#define USB_MODE_OBJECT			"/com/meego/usb_moded"

/**
 * sig_usb_state_ind: Notify interested parties of state and mode changes
 *
 * This signals both the transient states listed below as well as the "real"
 * states listed in usb_moded-modes.h.
 **/
#define USB_MODE_SIGNAL_NAME		"sig_usb_state_ind"
#define USB_MODE_CONFIG_SIGNAL_NAME	"sig_usb_config_ind"
#define USB_MODE_ERROR_SIGNAL_NAME	"sig_usb_state_error_ind"
#define USB_MODE_SUPPORTED_MODES_SIGNAL_NAME "sig_usb_supported_modes_ind"

/* supported methods */
#define USB_MODE_STATE_REQUEST	"mode_request"  /* returns the current mode */
#define USB_MODE_RESCUE_OFF	"rescue_off"    /* turns rescue mode off so normal mode selection is restored */
#define USB_MODE_CONFIG_GET	"get_config"	/* returns the mode set in the config */
#define USB_MODE_LIST		"get_modes"	/* returns a comma-separated list of supported modes for ui's */

/**
  * @credential usb-moded::USBControl	Credential needed to be able to call the set_mode or set_config methods
**/
#define USB_MODE_STATE_SET	"set_mode"	/* set a mode (only works when connected) */
#define USB_MODE_CONFIG_SET	"set_config"	/* set the mode that needs to be activated in the config file */
#define USB_MODE_NETWORK_SET	"net_config"    /* set the network config in the config file */
#define USB_MODE_NETWORK_GET	"get_net_config"    /* get the network config from the config file */

/**
 * (Transient) states reported by "sig_usb_state_ind" that are not modes.
 * These are only reported by the signal, and never returned by e.g. "mode_request".
 **/
#define USB_CONNECTED			"USB connected"
#define USB_DISCONNECTED		"USB disconnected"
#define USB_REALLY_DISCONNECT		"USB mode change in progress"
#define DATA_IN_USE			"data_in_use"
#define USB_CONNECTED_DIALOG_SHOW	"mode_requested_show_dialog"
#define USB_PRE_UNMOUNT			"pre-unmount"
#define RE_MOUNT_FAILED			"mount_failed"
#define CHARGER_CONNECTED		"charger_connected"
#define CHARGER_DISCONNECTED		"charger_disconnected"
#define MODE_SETTING_FAILED		"mode_setting_failed"

/* errors */
#define UMOUNT_ERROR			"Unmounting filesystem failed. Exporting impossible"
/* qtn_usb_filessystem_inuse is the NOKIA/Meego error string */
