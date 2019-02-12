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

#include "usb_moded-trigger.h"

#include "usb_moded-config-private.h"
#include "usb_moded-control.h"
#include "usb_moded-log.h"

#include <stdlib.h>
#include <string.h>

#include <libudev.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- trigger -- */

static void     trigger_udev_error_cb        (gpointer data);
static gboolean trigger_udev_input_cb        (GIOChannel *iochannel, GIOCondition cond, gpointer data);
static void     trigger_parse_udev_properties(struct udev_device *dev);
bool            trigger_init                 (void);
void            trigger_stop                 (void);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static struct udev         *trigger_udev_handle    = 0;
static struct udev_monitor *trigger_udev_monitor   = 0;
static guint                trigger_udev_watch_id  = 0;
static gchar               *trigger_udev_sysname   = 0;

/* ========================================================================= *
 * Functions
 * ========================================================================= */

static void trigger_udev_error_cb (gpointer data)
{
    LOG_REGISTER_CONTEXT;

    (void)data;

    log_debug("trigger watch destroyed\n!");
    /* clean up & restart trigger */
    trigger_stop();
    trigger_init();
}

bool trigger_init(void)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    gchar *devpath   = 0;
    gchar *subsystem = 0;
    struct udev_device *dev = 0;
    GIOChannel          *chn = 0;

    int ret;

    if( !(devpath = config_check_trigger()) ) {
        log_err("No trigger path. Not starting trigger.\n");
        goto EXIT;
    }

    if( !(subsystem = config_get_trigger_subsystem()) ) {
        log_err("No trigger subsystem. Not starting trigger.\n");
        goto EXIT;
    }

    /* Create the udev object */
    if( !(trigger_udev_handle = udev_new()) ) {
        log_err("Can't create udev\n");
    }

    dev = udev_device_new_from_syspath(trigger_udev_handle, devpath);
    if( !dev ) {
        log_err("Unable to find the trigger device.");
        goto EXIT;
    }

    trigger_udev_sysname = g_strdup(udev_device_get_sysname(dev));
    log_debug("device name = %s\n", trigger_udev_sysname);

    trigger_udev_monitor = udev_monitor_new_from_netlink(trigger_udev_handle,
                                                         "udev");
    if( !trigger_udev_monitor ) {
        log_err("Unable to monitor the netlink\n");
        goto EXIT;
    }

    ret = udev_monitor_filter_add_match_subsystem_devtype(trigger_udev_monitor,
                                                          subsystem,
                                                          NULL);
    if (ret != 0 ) {
        log_err("Udev match failed.\n");
        goto EXIT;
    }

    ret = udev_monitor_enable_receiving(trigger_udev_monitor);
    if( ret != 0 ) {
        log_err("Failed to enable monitor recieving.\n");
        goto EXIT;
    }

    /* check if we are already connected */
    trigger_parse_udev_properties(dev);

    chn = g_io_channel_unix_new(udev_monitor_get_fd(trigger_udev_monitor));
    if( !chn )
        goto EXIT;

    trigger_udev_watch_id = g_io_add_watch_full(chn,
                                                0,
                                                G_IO_IN,
                                                trigger_udev_input_cb,
                                                NULL,
                                                trigger_udev_error_cb);

    /* everything went well */
    log_debug("Trigger enabled!\n");
    ack = true;

EXIT:
    if(chn)
        g_io_channel_unref(chn);

    if( dev )
        udev_device_unref(dev);

    g_free(subsystem);
    g_free(devpath);

    /* All or nothing */
    if( !ack )
        trigger_stop();

    return ack;
}

static gboolean trigger_udev_input_cb(GIOChannel *iochannel G_GNUC_UNUSED, GIOCondition cond,
                                      gpointer data G_GNUC_UNUSED)
{
    LOG_REGISTER_CONTEXT;

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
                trigger_udev_watch_id = 0;
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
            trigger_udev_watch_id = 0;
            trigger_stop();
            return FALSE;
        }
    }

    /* keep watching */
    return TRUE;
}

void trigger_stop(void)
{
    LOG_REGISTER_CONTEXT;

    if(trigger_udev_watch_id)
    {
        g_source_remove(trigger_udev_watch_id);
        trigger_udev_watch_id = 0;
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
    g_free(trigger_udev_sysname),
        trigger_udev_sysname = 0;
}

static void trigger_parse_udev_properties(struct udev_device *dev)
{
    LOG_REGISTER_CONTEXT;

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

    control_set_usb_mode(trigger_mode);

EXIT:
    free(trigger_value);
    free(trigger_property);
    free(trigger_mode);
}
