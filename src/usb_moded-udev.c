/**
  @file usb_moded-udev.c
 
  Copyright (C) 2011 Nokia Corporation. All rights reserved.

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


#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <locale.h>
#include <unistd.h>

#include <poll.h>

#include <libudev.h>

#include <glib.h>

#include "usb_moded-log.h"
#include "usb_moded-config.h"
#include "usb_moded-hw-ab.h"
#include "usb_moded.h"
#include "usb_moded-modes.h"

/* global variables */
static struct udev *udev;
static struct udev_monitor *mon;
static GIOChannel *iochannel;
static guint watch_id; 
static char *dev_name = 0;
static int cleanup = 0;
/* track cable and charger connects disconnects */
static int cable = 0, charger = 0;
static guint cable_connection_timeout_id = 0;

/** Bookkeeping data for power supply locating heuristics */
typedef struct power_device {
        /** Device path used by udev */
        const char *syspath;
        /** Likelyhood of being power supply */
        int score;
} power_device;

/* static function definitions */
static gboolean monitor_udev(GIOChannel *iochannel G_GNUC_UNUSED, GIOCondition cond,
                             gpointer data G_GNUC_UNUSED);
static void udev_parse(struct udev_device *dev, bool initial);
static void setup_cable_connection(void);
static void setup_charger_connection(void);
static void cancel_cable_connection_timeout(void);
static void schedule_cable_connection_timeout(void);
static gboolean cable_connection_timeout_cb(gpointer data);

static void notify_issue (gpointer data)
{
	/* we do not want to restart when we try to clean up */
	if(cleanup)
		return;
        log_debug("USB connection watch destroyed, restarting it\n!");
        /* restart trigger */
        hwal_cleanup();
	hwal_init();
}

static int check_device_is_usb_power_supply(const char *syspath)
{
  struct udev *udev;
  struct udev_device *dev = 0;
  const char *udev_name;
  int score = 0;

  udev = udev_new();
  dev = udev_device_new_from_syspath(udev, syspath);
  if(!dev)
        return 0;
  udev_name = udev_device_get_sysname(dev);

  /* try to assign a weighed score */

  /* check it is no battery */
  if(strstr(udev_name, "battery") || strstr(udev_name, "BAT"))
        return 0;
  /* if it contains usb in the name it very likely is good */
  if(strstr(udev_name, "usb"))
        score = score + 10;
  /* often charger is also mentioned in the name */
  if(strstr(udev_name, "charger"))
        score = score + 5;
  /* present property is used to detect activity, however online is better */
  if(udev_device_get_property_value(dev, "POWER_SUPPLY_PRESENT"))
        score = score + 5;
  if(udev_device_get_property_value(dev, "POWER_SUPPLY_ONLINE"))
        score = score + 10;
  /* type is used to detect if it is a cable or dedicated charger. 
     Bonus points if it is there. */
  if(udev_device_get_property_value(dev, "POWER_SUPPLY_TYPE"))
        score = score + 10;

  /* clean up */
  udev_device_unref(dev);
  udev_unref(udev);

  return(score);
}


gboolean hwal_init(void)
{
  char *udev_path = NULL, *udev_subsystem = NULL;
  struct udev_device *dev;
  struct udev_enumerate *list;
  struct udev_list_entry *list_entry, *first_entry;
  struct power_device power_dev;
  int ret = 0;

  cleanup = 0;
	
  /* Create the udev object */
  udev = udev_new();
  if (!udev) 
  {
    log_err("Can't create udev\n");
    return FALSE;
  }
  
  udev_path = find_udev_path();
  if(udev_path)
  {
	dev = udev_device_new_from_syspath(udev, udev_path);
	g_free(udev_path);
  }
  else
  	dev = udev_device_new_from_syspath(udev, "/sys/class/power_supply/usb");
  if (!dev) 
  {
    log_debug("Trying to guess $power_supply device.\n");

    power_dev.score = 0;
    power_dev.syspath = 0;

    list = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(list, "power_supply");
    udev_enumerate_scan_devices(list);
    first_entry = udev_enumerate_get_list_entry(list);
    udev_list_entry_foreach(list_entry, first_entry)
    {
	udev_path =  (char *)udev_list_entry_get_name(list_entry);
        ret = check_device_is_usb_power_supply(udev_path);
        if(ret)
        {
		if(ret > power_dev.score)
		{
			power_dev.score = ret;
			power_dev.syspath = udev_path;
		}
	}
    }
    /* check if we found anything with some kind of score */
    if(power_dev.score > 0)
    {
  	dev = udev_device_new_from_syspath(udev, power_dev.syspath);
    }
    if(!dev)
    {
	log_err("Unable to find $power_supply device.");
	/* communicate failure, mainloop will exit and call appropriate clean-up */
	return FALSE;
    }
  }

  dev_name = strdup(udev_device_get_sysname(dev));
  log_debug("device name = %s\n", dev_name);
  mon = udev_monitor_new_from_netlink (udev, "udev");
  if (!mon) 
  {
    log_err("Unable to monitor the netlink\n");
    /* communicate failure, mainloop will exit and call appropriate clean-up */
    return FALSE;
  }
  udev_subsystem = find_udev_subsystem();
  if(udev_subsystem)
  {
	  ret = udev_monitor_filter_add_match_subsystem_devtype(mon, udev_subsystem, NULL);
	  g_free(udev_subsystem);
  }
  else
	  ret = udev_monitor_filter_add_match_subsystem_devtype(mon, "power_supply", NULL);
  if(ret != 0)
  {
    log_err("Udev match failed.\n");
    return FALSE;
  }
  ret = udev_monitor_enable_receiving (mon);
  if(ret != 0)
  { 
     log_err("Failed to enable monitor recieving.\n");
     return FALSE;
  }

  /* check if we are already connected */
  udev_parse(dev, true);
  
  iochannel = g_io_channel_unix_new(udev_monitor_get_fd(mon));
  watch_id = g_io_add_watch_full(iochannel, 0, G_IO_IN, monitor_udev, NULL,notify_issue);

  /* everything went well */
  udev_device_unref(dev);
  return TRUE;
}

static gboolean monitor_udev(GIOChannel *iochannel G_GNUC_UNUSED, GIOCondition cond,
                             gpointer data G_GNUC_UNUSED)
{
  struct udev_device *dev;

  gboolean continue_watching = TRUE;

  /* No code paths are allowed to bypass the release_wakelock() call below */
  acquire_wakelock(USB_MODED_WAKELOCK_PROCESS_INPUT);

  if(cond & G_IO_IN)
  {
    /* This normally blocks but G_IO_IN indicates that we can read */
    dev = udev_monitor_receive_device (mon);
    if (!dev)
    {
      /* if we get something else something bad happened stop watching to avoid busylooping */
      continue_watching = FALSE;
    }
    else
    {
      /* check if it is the actual device we want to check */
      if(!strcmp(dev_name, udev_device_get_sysname(dev)))
      {
	if(!strcmp(udev_device_get_action(dev), "change"))
	{
	  udev_parse(dev, false);
	}
      }

      udev_device_unref(dev);
    }
  }

  if(cond & (G_IO_ERR | G_IO_HUP | G_IO_NVAL))
  {
    /* Unhandled errors turn io watch to virtual busyloop too */
    continue_watching = FALSE;
  }

  release_wakelock(USB_MODED_WAKELOCK_PROCESS_INPUT);

  if (!continue_watching && watch_id )
  {
    watch_id = 0;
    log_crit("udev io watch disabled");
  }

  return continue_watching;
}

void hwal_cleanup(void)
{
  cleanup = 1;

  log_debug("HWhal cleanup\n");

  if(watch_id != 0)
  {
    g_source_remove(watch_id);
    watch_id = 0;
  }
  if(iochannel != NULL)
  {
    g_io_channel_unref(iochannel);
    iochannel = NULL;
  }
  cancel_cable_connection_timeout();
  free(dev_name);
  udev_monitor_unref(mon);
  udev_unref(udev);
}

static void setup_cable_connection(void)
{
	cancel_cable_connection_timeout();

	log_debug("UDEV:USB pc cable connected\n");

	cable = 1;
	charger = 0;
	set_usb_connected(TRUE);
}

static void setup_charger_connection(void)
{
	cancel_cable_connection_timeout();

	log_debug("UDEV:USB dedicated charger connected\n");

	if (cable) {
		/* The connection was initially reported incorrectly
		 * as pc cable, then later on declared as charger.
		 *
		 * Clear "connected" boolean flag so that the
		 * set_charger_connected() call below acts as if charger
		 * were detected already on connect: mode gets declared
		 * as dedicated charger (and the mode selection dialog
		 * shown by ui gets closed).
		 */
		set_usb_connection_state(FALSE);
	}

	charger = 1;
	cable = 0;
	set_charger_connected(TRUE);
}

static gboolean cable_connection_timeout_cb(gpointer data)
{
	log_debug("connect delay: timeout");
	cable_connection_timeout_id = 0;

	setup_cable_connection();

	return FALSE;
}

static void cancel_cable_connection_timeout(void)
{
	if (cable_connection_timeout_id) {
		log_debug("connect delay: cancel");
		g_source_remove(cable_connection_timeout_id);
		cable_connection_timeout_id = 0;
	}
}


static void schedule_cable_connection_timeout(void)
{
	/* Ignore If already connected */
	if (get_usb_connection_state())
		return;

	if (!cable_connection_timeout_id && cable_connection_delay > 0) {
		/* Dedicated charger might be initially misdetected as
		 * pc cable. Delay a bit befor accepting the state. */

		log_debug("connect delay: started (%d ms)",
			  cable_connection_delay);
		cable_connection_timeout_id =
			g_timeout_add(cable_connection_delay,
				      cable_connection_timeout_cb,
				      NULL);
	}
	else {
		/* If more udev events indicating cable connection
		 * are received while waiting, accept immediately. */
		setup_cable_connection();
	}
}

static void udev_parse(struct udev_device *dev, bool initial)
{
	/* udev properties we are interested in */
	const char *power_supply_present = 0;
	const char *power_supply_online  = 0;
	const char *power_supply_type    = 0;

	/* Assume there is no usb connection until proven otherwise */
	bool connected  = false;

	/* Unless debug logging has been request via command line,
	 * suppress warnings about potential property issues and/or
	 * fallback strategies applied (to avoid spamming due to the
	 * code below seeing the same property values over and over
	 * again also in stable states).
	 */
	bool warnings = log_p(LOG_DEBUG);

	/*
	 * Check for present first as some drivers use online for when charging
	 * is enabled
	 */
	power_supply_present = udev_device_get_property_value(dev, "POWER_SUPPLY_PRESENT");
	if (!power_supply_present) {
		power_supply_present =
		power_supply_online = udev_device_get_property_value(dev, "POWER_SUPPLY_ONLINE");
	}

	if (power_supply_present && !strcmp(power_supply_present, "1"))
		connected = true;

	/* Transition period = Connection status derived from udev
	 * events disagrees with usb-moded side bookkeeping. */
	if (connected != get_usb_connection_state()) {
		/* Enable udev property diagnostic logging */
		warnings = true;
		/* Block suspend briefly */
		delay_suspend();
	}

	/* disconnect */
	if (!connected) {
		if (warnings && !power_supply_present)
			log_err("No usable power supply indicator\n");

		log_debug("DISCONNECTED");

		cancel_cable_connection_timeout();

		if (charger) {
			log_debug("UDEV:USB dedicated charger disconnected\n");
			set_charger_connected(FALSE);
		}

		if (cable) {
			log_debug("UDEV:USB cable disconnected\n");
			set_usb_connected(FALSE);
		}

		cable = 0;
		charger = 0;
	}
	else {
		if (warnings && power_supply_online)
			log_warning("Using online property\n");

		power_supply_type = udev_device_get_property_value(dev, "POWER_SUPPLY_TYPE");
		/*
		 * Power supply type might not exist also :(
		 * Send connected event but this will not be able
		 * to discriminate between charger/cable.
		 */
		if (!power_supply_type) {
			if( warnings )
				log_warning("Fallback since cable detection might not be accurate. "
					    "Will connect on any voltage on charger.\n");
			schedule_cable_connection_timeout();
			goto cleanup;
		}

		log_debug("CONNECTED - POWER_SUPPLY_TYPE = %s", power_supply_type);

		if (!strcmp(power_supply_type, "USB") ||
		    !strcmp(power_supply_type, "USB_CDP")) {
			if( initial )
				setup_cable_connection();
			else
				schedule_cable_connection_timeout();
		}
		else if (!strcmp(power_supply_type, "USB_DCP") ||
			 !strcmp(power_supply_type, "USB_HVDCP") ||
			 !strcmp(power_supply_type, "USB_HVDCP_3")) {
			setup_charger_connection();
		}
		else if( !strcmp(power_supply_type, "Unknown")) {
			// nop
		}
		else {
			if (warnings)
				log_warning("unhandled power supply type: %s", power_supply_type);
		}
	}

cleanup:
	return;
}
