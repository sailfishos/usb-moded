/**
 * @file usb_moded-util.c
 *
 * Copyright (C) 2013-2018 Jolla. All rights reserved.
 *
 * @author: Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author: Philippe De Swert <philippedeswert@gmail.com>
 * @author: Martin Jones <martin.jones@jollamobile.com>
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
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <dbus/dbus.h>

#include "usb_moded-dbus.h"

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- util -- */

static int util_query_mode            (void);
static int util_get_modelist          (void);
static int util_get_mode_configured   (void);
static int util_unset_rescue          (void);
static int util_set_mode              (char *mode);
static int util_set_mode_config       (char *mode);
static int util_set_hide_mode_config  (char *mode);
static int util_set_unhide_mode_config(char *mode);
static int util_get_hiddenlist        (void);
static int util_handle_network        (char *network);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static DBusConnection *conn = 0;

/* ========================================================================= *
 * Functions
 * ========================================================================= */

static int util_query_mode (void)
{
    DBusMessage *req = NULL, *reply = NULL;
    char *ret = 0;

    if ((req = dbus_message_new_method_call(USB_MODE_SERVICE, USB_MODE_OBJECT, USB_MODE_INTERFACE, USB_MODE_STATE_REQUEST)) != NULL)
    {
        if ((reply = dbus_connection_send_with_reply_and_block(conn, req, -1, NULL)) != NULL)
        {
            dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &ret, DBUS_TYPE_INVALID);
            dbus_message_unref(reply);
        }
        dbus_message_unref(req);
    }

    if(ret)
    {
        printf("mode = %s\n", ret);
        return 0;
    }

    /* not everything went as planned, return error */
    return 1;
}

static int util_get_modelist (void)
{
    DBusMessage *req = NULL, *reply = NULL;
    char *ret = 0;

    if ((req = dbus_message_new_method_call(USB_MODE_SERVICE, USB_MODE_OBJECT, USB_MODE_INTERFACE, USB_MODE_LIST)) != NULL)
    {
        if ((reply = dbus_connection_send_with_reply_and_block(conn, req, -1, NULL)) != NULL)
        {
            dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &ret, DBUS_TYPE_INVALID);
            dbus_message_unref(reply);
        }
        dbus_message_unref(req);
    }

    if(ret)
    {
        printf("modes supported are = %s\n", ret);
        return 0;
    }

    /* not everything went as planned, return error */
    return 1;
}

static int util_get_mode_configured (void)
{
    DBusMessage *req = NULL, *reply = NULL;
    char *ret = 0;

    if ((req = dbus_message_new_method_call(USB_MODE_SERVICE, USB_MODE_OBJECT, USB_MODE_INTERFACE, USB_MODE_CONFIG_GET)) != NULL)
    {
        if ((reply = dbus_connection_send_with_reply_and_block(conn, req, -1, NULL)) != NULL)
        {
            dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &ret, DBUS_TYPE_INVALID);
            dbus_message_unref(reply);
        }
        dbus_message_unref(req);
    }

    if(ret)
    {
        printf("On USB connection usb_moded will set the following mode based on the configuration = %s\n", ret);
        return 0;
    }

    /* not everything went as planned, return error */
    return 1;
}

static int util_unset_rescue (void)
{
    DBusMessage *req = NULL, *reply = NULL;
    int ret = 0;

    if ((req = dbus_message_new_method_call(USB_MODE_SERVICE, USB_MODE_OBJECT, USB_MODE_INTERFACE, USB_MODE_RESCUE_OFF)) != NULL)
    {
        if ((reply = dbus_connection_send_with_reply_and_block(conn, req, -1, NULL)) != NULL)
        {
            if(reply)
                ret = 1;
            dbus_message_unref(reply);
        }
        dbus_message_unref(req);
    }

    if(ret)
    {
        printf("Rescue mode is off\n");
        return 0;
    }
    else
        return 1;
}

static int util_set_mode (char *mode)
{
    DBusMessage *req = NULL, *reply = NULL;
    char *ret = 0;

    printf("Trying to set the following mode %s\n", mode);
    if ((req = dbus_message_new_method_call(USB_MODE_SERVICE, USB_MODE_OBJECT, USB_MODE_INTERFACE, USB_MODE_STATE_SET)) != NULL)
    {
        dbus_message_append_args (req, DBUS_TYPE_STRING, &mode, DBUS_TYPE_INVALID);
        if ((reply = dbus_connection_send_with_reply_and_block(conn, req, -1, NULL)) != NULL)
        {
            dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &ret, DBUS_TYPE_INVALID);
            dbus_message_unref(reply);
        }
        dbus_message_unref(req);
    }

    if(ret)
    {
        printf("mode set = %s\n", ret);
        return 0;
    }

    /* not everything went as planned, return error */
    return 1;
}

static int util_set_mode_config (char *mode)
{
    DBusMessage *req = NULL, *reply = NULL;
    char *ret = 0;

    printf("Trying to set the following mode %s in the config file\n", mode);
    if ((req = dbus_message_new_method_call(USB_MODE_SERVICE, USB_MODE_OBJECT, USB_MODE_INTERFACE, USB_MODE_CONFIG_SET)) != NULL)
    {
        dbus_message_append_args (req, DBUS_TYPE_STRING, &mode, DBUS_TYPE_INVALID);
        if ((reply = dbus_connection_send_with_reply_and_block(conn, req, -1, NULL)) != NULL)
        {
            dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &ret, DBUS_TYPE_INVALID);
            dbus_message_unref(reply);
        }
        dbus_message_unref(req);
    }

    if(ret)
    {
        printf("mode set in the configuration file = %s\n", ret);
        return 0;
    }

    /* not everything went as planned, return error */
    return 1;
}

static int util_set_hide_mode_config (char *mode)
{
    DBusMessage *req = NULL, *reply = NULL;
    char *ret = 0;

    printf("Trying to hide the following mode %s in the config file\n", mode);
    if ((req = dbus_message_new_method_call(USB_MODE_SERVICE, USB_MODE_OBJECT, USB_MODE_INTERFACE, USB_MODE_HIDE)) != NULL)
    {
        dbus_message_append_args (req, DBUS_TYPE_STRING, &mode, DBUS_TYPE_INVALID);
        if ((reply = dbus_connection_send_with_reply_and_block(conn, req, -1, NULL)) != NULL)
        {
            dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &ret, DBUS_TYPE_INVALID);
            dbus_message_unref(reply);
        }
        dbus_message_unref(req);
    }

    if(ret)
    {
        printf("mode hidden = %s\n", ret);
        return 0;
    }

    /* not everything went as planned, return error */
    return 1;
}

static int util_set_unhide_mode_config (char *mode)
{
    DBusMessage *req = NULL, *reply = NULL;
    char *ret = 0;

    printf("Trying to unhide the following mode %s in the config file\n", mode);
    if ((req = dbus_message_new_method_call(USB_MODE_SERVICE, USB_MODE_OBJECT, USB_MODE_INTERFACE, USB_MODE_UNHIDE)) != NULL)
    {
        dbus_message_append_args (req, DBUS_TYPE_STRING, &mode, DBUS_TYPE_INVALID);
        if ((reply = dbus_connection_send_with_reply_and_block(conn, req, -1, NULL)) != NULL)
        {
            dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &ret, DBUS_TYPE_INVALID);
            dbus_message_unref(reply);
        }
        dbus_message_unref(req);
    }

    if(ret)
    {
        printf("mode unhidden = %s\n", ret);
        return 0;
    }

    /* not everything went as planned, return error */
    return 1;
}

static int util_get_hiddenlist (void)
{
    DBusMessage *req = NULL, *reply = NULL;
    char *ret = 0;

    if ((req = dbus_message_new_method_call(USB_MODE_SERVICE, USB_MODE_OBJECT, USB_MODE_INTERFACE, USB_MODE_HIDDEN_GET)) != NULL)
    {
        if ((reply = dbus_connection_send_with_reply_and_block(conn, req, -1, NULL)) != NULL)
        {
            dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &ret, DBUS_TYPE_INVALID);
            dbus_message_unref(reply);
        }
        dbus_message_unref(req);
    }

    if(ret)
    {
        printf("hidden modes are = %s\n", ret);
        return 0;
    }

    /* not everything went as planned, return error */
    return 1;
}

static int util_handle_network(char *network)
{
    char *operation = 0, *setting = 0, *value = 0;
    DBusMessage *req = NULL, *reply = NULL;
    char *ret = 0;

    operation = strtok(network, ":");
    printf("Operation = %s\n", operation);
    setting =  strtok(NULL, ",");
    printf("Setting = %s\n", setting);
    value =  strtok(NULL, ",");
    printf("Value = %s\n", value);
    if(operation == NULL || setting == NULL )
    {
        printf("Argument list is wrong. Please use get:$setting or set:$setting,$value\n");
        return 1;
    }
    if(!strcmp(operation, "set"))
    {
        if(value == NULL)
        {
            printf("Argument list is wrong. Please use set:$setting,$value\n");
            return 1;
        }
        if ((req = dbus_message_new_method_call(USB_MODE_SERVICE, USB_MODE_OBJECT, USB_MODE_INTERFACE, USB_MODE_NETWORK_SET)) != NULL)
        {
            dbus_message_append_args (req, DBUS_TYPE_STRING, &setting, DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID);
            if ((reply = dbus_connection_send_with_reply_and_block(conn, req, -1, NULL)) != NULL)
            {
                dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &setting, DBUS_TYPE_STRING, &ret, DBUS_TYPE_INVALID);
                dbus_message_unref(reply);
            }
            dbus_message_unref(req);
        }

        if(ret)
        {
            printf("The following USB network setting %s = %s has been set\n", setting, ret);
            return 0;
        }
        else
            return 1;
    }
    else if(!strcmp(operation, "get"))
    {

        if ((req = dbus_message_new_method_call(USB_MODE_SERVICE, USB_MODE_OBJECT, USB_MODE_INTERFACE, USB_MODE_NETWORK_GET)) != NULL)
        {
            dbus_message_append_args (req, DBUS_TYPE_STRING, &setting, DBUS_TYPE_INVALID);
            if ((reply = dbus_connection_send_with_reply_and_block(conn, req, -1, NULL)) != NULL)
            {
                dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &setting, DBUS_TYPE_STRING, &ret, DBUS_TYPE_INVALID);
                dbus_message_unref(reply);
            }
            dbus_message_unref(req);
        }

        if(ret)
        {
            printf("USB network setting %s = %s\n", setting, ret);
            return 0;
        }
        else
            return 1;
    }
    else
        /* unknown operation */
        return 1;
}

int main (int argc, char *argv[])
{
    int query = 0, network = 0, setmode = 0, config = 0;
    int modelist = 0, mode_configured = 0, hide = 0, unhide = 0, hiddenlist = 0;
    int res = 1, opt, rescue = 0;
    char *option = 0;

    if(argc == 1)
    {
        fprintf(stderr, "No options given, try -h for more information\n");
        exit(1);
    }

    while ((opt = getopt(argc, argv, "c:dhi:mn:qrs:u:v")) != -1)
    {
        switch (opt) {
        case 'c':
            config = 1;
            option = optarg;
            break;
        case 'd':
            mode_configured = 1;
            break;
        case 'i':
            hide = 1;
            option = optarg;
            break;
        case 'm':
            modelist = 1;
            break;
        case 'n':
            network = 1;
            option = optarg;
            break;
        case 'q':
            query = 1;
            break;
        case 'r':
            rescue = 1;
            break;
        case 's':
            setmode = 1;
            option = optarg;
            break;
        case 'u':
            unhide = 1;
            option = optarg;
            break;
        case 'v':
            hiddenlist = 1;
            break;
        case 'h':
        default:
                fprintf(stderr, "\nUsage: %s -<option> <args>\n\n \
                   Options are: \n \
                   \t-c to set a mode in the config file,\n \
                   \t-d to get the default mode set in the configuration, \n \
                   \t-h to get this help, \n \
                   \t-i hide a mode,\n \
                   \t-n to get/set network configuration. Use get:${config}/set:${config},${value}\n \
                   \t-m to get the list of supported modes, \n \
                   \t-q to query the current mode,\n \
                   \t-r turn rescue mode off,\n \
                   \t-s to set/activate a mode,\n \
                   \t-u unhide a mode,\n \
                   \t-v to get the list of hidden modes\n",
                        argv[0]);
            exit(1);
        }
    }

    /* init dbus */
    DBusError error = DBUS_ERROR_INIT;

    conn = dbus_bus_get_private(DBUS_BUS_SYSTEM, &error);
    if (!conn)
    {
        if (dbus_error_is_set(&error))
            return 1;
    }

    /* check which sub-routine to call */
    if(query)
        res = util_query_mode();
    else if (modelist)
        res = util_get_modelist();
    else if (mode_configured)
        res = util_get_mode_configured();
    else if (setmode)
        res = util_set_mode(option);
    else if (config)
        res = util_set_mode_config(option);
    else if (network)
        res = util_handle_network(option);
    else if (rescue)
        res = util_unset_rescue();
    else if (hide)
        res = util_set_hide_mode_config(option);
    else if (unhide)
        res = util_set_unhide_mode_config(option);
    else if (hiddenlist)
        res = util_get_hiddenlist();

    /* subfunctions will return 1 if an error occured, print message */
    if(res)
        printf("Sorry an error occured, your request was not processed.\n");

    /* clean-up and exit */
    dbus_connection_close(conn);
    dbus_connection_unref(conn);
    return 0;
}
