/**
  @file usb_moded-trigger.c
 
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
#include <locale.h>
#include <unistd.h>

#include <poll.h>

#include <libudev.h>

#include <glib.h>

#include "usb_moded.h"
#include "usb_moded-log.h"
#include "usb_moded-config.h"
#include "usb_moded-hw-ab.h"
#include "usb_moded-modesetting.h"
#include "usb_moded-trigger.h"
#if defined MEEGOLOCK
#include "usb_moded-lock.h"
#endif /* MEEGOLOCK */

/* global variables */
static struct udev *udev = 0;
static struct udev_monitor *mon = 0;
static GIOChannel *iochannel = 0;
static guint watch_id = 0;
static const char *dev_name;

/* static function definitions */
static gboolean monitor_udev(GIOChannel *iochannel G_GNUC_UNUSED, GIOCondition cond,
                             gpointer data G_GNUC_UNUSED);
static void udev_parse(struct udev_device *dev);

static void notify_issue (gpointer data)
{
        log_debug("trigger watch destroyed\n!");
	/* clean up & restart trigger */
	trigger_stop();
	trigger_init();
}


gboolean trigger_init(void)
{
  const gchar *udev_path = NULL;
  struct udev_device *dev;
  int ret = 0;
	
  /* Create the udev object */
  udev = udev_new();
  if (!udev) 
  {
    log_err("Can't create udev\n");
    return 1;
  }
  
  udev_path = check_trigger();
  if(udev_path)
	dev = udev_device_new_from_syspath(udev, udev_path);
  else
  {
    log_err("No trigger path. Not starting trigger.\n");
    return 1;	
  }
  if (!dev) 
  {
    log_err("Unable to find the trigger device.");
    return 1;
  }
  else
  {
    dev_name = udev_device_get_sysname(dev);
    log_debug("device name = %s\n", dev_name);
  } 
  mon = udev_monitor_new_from_netlink (udev, "udev");
  if (!mon) 
  {
    log_err("Unable to monitor the netlink\n");
    /* communicate failure, mainloop will exit and call appropriate clean-up */
    return 1;
  }
  ret = udev_monitor_filter_add_match_subsystem_devtype(mon, get_trigger_subsystem(), NULL);
  if(ret != 0)
  {
    log_err("Udev match failed.\n");
    return 1;
  }
  ret = udev_monitor_enable_receiving (mon);
  if(ret != 0)
  { 
     log_err("Failed to enable monitor recieving.\n");
     return 1;
  }

  /* check if we are already connected */
  udev_parse(dev);
  
  iochannel = g_io_channel_unix_new(udev_monitor_get_fd(mon));
  watch_id = g_io_add_watch_full(iochannel, 0, G_IO_IN, monitor_udev, NULL, notify_issue);

  /* everything went well */
  log_debug("Trigger enabled!\n");
  return 0;
}

static gboolean monitor_udev(GIOChannel *iochannel G_GNUC_UNUSED, GIOCondition cond,
                             gpointer data G_GNUC_UNUSED)
{
  struct udev_device *dev;

  if(cond & G_IO_IN)
  {
    /* This normally blocks but G_IO_IN indicates that we can read */
    dev = udev_monitor_receive_device (mon);
    if (dev) 
    {
      /* check if it is the actual device we want to check */
      if(strcmp(dev_name, udev_device_get_sysname(dev))) {
        log_crit("name does not match, disabling udev trigger io-watch");
        watch_id = 0;
        return FALSE;
      }
       
      if(!strcmp(udev_device_get_action(dev), "change"))
      {
        log_debug("Trigger event recieved.\n");
	udev_parse(dev);
      }
      udev_device_unref(dev);
    }
    /* if we get something else something bad happened stop watching to avoid busylooping */  
    else
    {
   	log_debug("Bad trigger data. Stopping\n");
        watch_id = 0;
        trigger_stop();
        return FALSE;
    }
  }
  
  /* keep watching */
  return TRUE;
}

void trigger_stop(void)
{
  if(watch_id)
  {
    g_source_remove(watch_id);
    watch_id = 0;
  }
  if(iochannel) {
    g_io_channel_unref(iochannel);
    iochannel = NULL;
  }
  if(mon)
  {
    udev_monitor_unref(mon);
    mon = 0;
  }
  if(udev)
  {
    udev_unref(udev);
    udev = 0;
  }
  dev_name = 0;
}

static void udev_parse(struct udev_device *dev)
{
  const char *tmp = 0;
  char *trigger = 0;
 
  trigger = get_trigger_property();
  tmp = udev_device_get_property_value(dev, trigger);
  if(!tmp)
  {
    /* do nothing and return */
    free(trigger);
    return;
  }
  else
  {
    free(trigger);
    trigger = get_trigger_value();
    if(trigger)
    {
	if(!strcmp(tmp, trigger))
	{
#if defined MEEGOLOCK
	 if(!usb_moded_get_export_permission())
#endif /* MEEGOLOCK */
	   if(strcmp(get_trigger_mode(), get_usb_mode()) != 0)
	   {
		usb_moded_mode_cleanup(get_usb_module());
      	   	set_usb_mode(get_trigger_mode());
	   }
	   free(trigger);
	}
	else
	{
	   free(trigger);
	   return;
	}
    }
    else
    /* for triggers without trigger value */	
    {
#if defined MEEGOLOCK
     if(!usb_moded_get_export_permission())
#endif /* MEEGOLOCK */
       if(strcmp(get_trigger_mode(), get_usb_mode()) != 0)
	{
	    usb_moded_mode_cleanup(get_usb_module());
       	    set_usb_mode(get_trigger_mode());
	}
    }
    return;
  }
}
