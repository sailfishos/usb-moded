/**
 * @file udev-search.c
 *
 * This is a basic utility to check for potential
 * viable paths to use for usb_moded.
 *
 * This is in case usb_moded can not figure it out for itself.
 *
 * compile with gcc -o udev-search udev-search.c -ludev
 *
 * Copyright (C) 2014-2018 Jolla. All rights reserved.
 *
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
#include <string.h>

#include <libudev.h>

/* ========================================================================= *
 * Protorypes
 * ========================================================================= */

/* -- check -- */

static int check_device_is_usb_power_supply(const char *syspath);

/* ========================================================================= *
 * Functions
 * ========================================================================= */

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
    printf("device name = %s\n", udev_name);
    /* check it is no battery */
    if(strstr(udev_name, "battery") ||
       strstr(udev_name, "BAT"))
        return 0;
    if(strstr(udev_name, "usb"))
        score = score + 10;
    if(strstr(udev_name, "charger"))
        score = score + 5;
    if(udev_device_get_property_value(dev, "POWER_SUPPLY_PRESENT"))
    {
        score = score + 5;
        printf("present property found\n");
    }
    if(udev_device_get_property_value(dev, "POWER_SUPPLY_ONLINE"))
    {
        score = score + 10;
        printf("online property found\n");
    }
    if(udev_device_get_property_value(dev, "POWER_SUPPLY_TYPE"))
    {
        score = score + 10;
        printf("type property found\n");
    }

    return(score);
}

int main (int argc, char **argv)
{
    (void)argc;
    (void)argv;

    struct udev *udev;
    struct udev_enumerate *list;
    struct udev_list_entry *list_entry, *first_entry;
    const char *udev_name;
    int ret = 0, score = 0;

    typedef struct power_device_t {
        const char *syspath;
        int score;
    } power_device_t;

    power_device_t power_dev;

    power_dev.score = 0;

    udev = udev_new();
    list = udev_enumerate_new(udev);
    ret  = udev_enumerate_add_match_subsystem(list, "power_supply");
    ret = udev_enumerate_scan_devices(list);
    if(ret < 0)
    {
        printf("List empty\n");
        exit(1);
    }
    first_entry = udev_enumerate_get_list_entry(list);
    udev_list_entry_foreach(list_entry, first_entry)
    {
        udev_name =  udev_list_entry_get_name(list_entry);
        score = check_device_is_usb_power_supply(udev_name);
        printf("power_supply device name = %s score = %d\n", udev_name, score);
        if(score)
        {
            if(score > power_dev.score)
            {
                power_dev.score = score;
                power_dev.syspath = udev_name;
            }
        }
    }

    printf("most likely power supply device = %s\n", power_dev.syspath);

    exit(0);
}
