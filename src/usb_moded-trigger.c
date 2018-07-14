/**
 * @file usb_moded-trigger.c
 *
 * Copyright (C) 2011 Nokia Corporation. All rights reserved.
 * Copyright (C) 2014-2018 Jolla Ltd.
 *
 * @author: Philippe De Swert <philippedeswert@gmail.com>
 * @author: Philippe De Swert <phdeswer@lumi.maa>
 * @author: Philippe De Swert <philippe.de-swert@nokia.com>
 * @author: Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author: Simo Piiroinen <simo.piiroinen@jollamobile.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <unistd.h>

#include <poll.h>

#include <libudev.h>

#include <glib.h>

#include "usb_moded.h"
#include "usb_moded-log.h"
#include "usb_moded-config-private.h"
#include "usb_moded-udev.h"
#include "usb_moded-modesetting.h"
#include "usb_moded-trigger.h"
#if defined MEEGOLOCK
# include "usb_moded-devicelock.h"
#endif /* MEEGOLOCK */

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- trigger -- */

static void     trigger_udev_error_cb        (gpointer data);
static gboolean trigger_udev_input_cb        (GIOChannel *iochannel, GIOCondition cond, gpointer data);
static void     trigger_parse_udev_properties(struct udev_device *dev);
gboolean        trigger_init                 (void);
void            trigger_stop                 (void);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static struct udev         *trigger_udev_handle    = 0;
static struct udev_monitor *trigger_udev_monitor   = 0;
static GIOChannel          *trigger_udev_iochannel = 0;
static guint                trigger_udev_input_id  = 0;
static const char          *trigger_udev_sysname   = 0;

/* ========================================================================= *
 * Functions
 * ========================================================================= */

static void trigger_udev_error_cb (gpointer data)
{
    (void)data;

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
    trigger_udev_handle = udev_new();
    if (!trigger_udev_handle)
    {
        log_err("Can't create udev\n");
        return 1;
    }

    udev_path = config_check_trigger();
    if(udev_path)
        dev = udev_device_new_from_syspath(trigger_udev_handle, udev_path);
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
        trigger_udev_sysname = udev_device_get_sysname(dev);
        log_debug("device name = %s\n", trigger_udev_sysname);
    }
    trigger_udev_monitor = udev_monitor_new_from_netlink (trigger_udev_handle, "udev");
    if (!trigger_udev_monitor)
    {
        log_err("Unable to monitor the netlink\n");
        /* communicate failure, mainloop will exit and call appropriate clean-up */
        return 1;
    }
    ret = udev_monitor_filter_add_match_subsystem_devtype(trigger_udev_monitor, config_get_trigger_subsystem(), NULL);
    if(ret != 0)
    {
        log_err("Udev match failed.\n");
        return 1;
    }
    ret = udev_monitor_enable_receiving (trigger_udev_monitor);
    if(ret != 0)
    {
        log_err("Failed to enable monitor recieving.\n");
        return 1;
    }

    /* check if we are already connected */
    trigger_parse_udev_properties(dev);

    trigger_udev_iochannel = g_io_channel_unix_new(udev_monitor_get_fd(trigger_udev_monitor));
    trigger_udev_input_id = g_io_add_watch_full(trigger_udev_iochannel, 0, G_IO_IN, trigger_udev_input_cb, NULL, trigger_udev_error_cb);

    /* everything went well */
    log_debug("Trigger enabled!\n");
    return 0;
}

static gboolean trigger_udev_input_cb(GIOChannel *iochannel G_GNUC_UNUSED, GIOCondition cond,
                               gpointer data G_GNUC_UNUSED)
{
    struct udev_device *dev;

    if(cond & G_IO_IN)
    {
        /* This normally blocks but G_IO_IN indicates that we can read */
        dev = udev_monitor_receive_device (trigger_udev_monitor);
        if (dev)
        {
            /* check if it is the actual device we want to check */
            if(strcmp(trigger_udev_sysname, udev_device_get_sysname(dev))) {
                log_crit("name does not match, disabling udev trigger io-watch");
                trigger_udev_input_id = 0;
                return FALSE;
            }

            if(!strcmp(udev_device_get_action(dev), "change"))
            {
                log_debug("Trigger event recieved.\n");
                trigger_parse_udev_properties(dev);
            }
            udev_device_unref(dev);
        }
        /* if we get something else something bad happened stop watching to avoid busylooping */
        else
        {
            log_debug("Bad trigger data. Stopping\n");
            trigger_udev_input_id = 0;
            trigger_stop();
            return FALSE;
        }
    }

    /* keep watching */
    return TRUE;
}

void trigger_stop(void)
{
    if(trigger_udev_input_id)
    {
        g_source_remove(trigger_udev_input_id);
        trigger_udev_input_id = 0;
    }
    if(trigger_udev_iochannel) {
        g_io_channel_unref(trigger_udev_iochannel);
        trigger_udev_iochannel = NULL;
    }
    if(trigger_udev_monitor)
    {
        udev_monitor_unref(trigger_udev_monitor);
        trigger_udev_monitor = 0;
    }
    if(trigger_udev_handle)
    {
        udev_unref(trigger_udev_handle);
        trigger_udev_handle = 0;
    }
    trigger_udev_sysname = 0;
}

static void trigger_parse_udev_properties(struct udev_device *dev)
{
    char *trigger_property = 0;
    char *trigger_value = 0;
    char *trigger_mode = 0;

    const char *value = 0;

    if( !usbmoded_can_export() )
        goto EXIT;

    if( !(trigger_mode = config_get_trigger_mode()) )
        goto EXIT;

    if( !(trigger_property = config_get_trigger_property()) )
        goto EXIT;

    if( !(value = udev_device_get_property_value(dev, trigger_property)) )
        goto EXIT;

    if( (trigger_value = config_get_trigger_value()) ) {
        if( strcmp(trigger_value, value) )
            goto EXIT;
    }

    usbmoded_set_usb_mode(trigger_mode);

EXIT:
    free(trigger_value);
    free(trigger_property);
    free(trigger_mode);
}
