/**
 * @file usb_moded-appsync.c
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Copyright (C) 2013-2018 Jolla Ltd.
 *
 * @author: Philippe De Swert <philippe.de-swert@nokia.com>
 * @author: Philippe De Swert <phdeswer@lumi.maa>
 * @author: Philippe De Swert <philippedeswert@gmail.com>
 * @author: Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author: Thomas Perl <m@thp.io>
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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "usb_moded-appsync.h"
#include "usb_moded-appsync-dbus-private.h"
#include "usb_moded-modesetting.h"
#include "usb_moded-log.h"
#include "usb_moded-systemd.h"

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- appsync -- */

static void              appsync_free_elem                 (struct list_elem *elem);
static void              appsync_free_elem_cb              (gpointer elem, gpointer user_data);
void                     appsync_free_appsync_list         (void);
static gint              appsync_list_sort_func            (gconstpointer a, gconstpointer b);
void                     appsync_read_list                 (int diag);
static struct list_elem *appsync_read_file                 (const gchar *filename, int diag);
int                      appsync_activate_sync             (const char *mode);
int                      appsync_activate_sync_post        (const char *mode);
int                      appsync_mark_active               (const gchar *name, int post);
static gboolean          appsync_enumerate_usb_cb          (gpointer data);
static void              appsync_start_enumerate_usb_timer (void);
static void              appsync_cancel_enumerate_usb_timer(void);
static void              appsync_enumerate_usb             (void);
void                     appsync_stop_apps                 (int post);
int                      appsync_stop                      (gboolean force);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static GList *appsync_sync_list = NULL;

static guint appsync_enumerate_usb_id = 0;
static struct timeval appsync_sync_tv = {0, 0};
#ifdef APP_SYNC_DBUS
static  int appsync_no_dbus = 0;
#else
static  int appsync_no_dbus = 0;
#endif /* APP_SYNC_DBUS */

/* ========================================================================= *
 * Functions
 * ========================================================================= */

static void appsync_free_elem(struct list_elem *elem)
{
    g_free(elem->name);
    g_free(elem->launch);
    g_free(elem->mode);
    free(elem);
}

static void appsync_free_elem_cb(gpointer elem, gpointer user_data)
{
    (void)user_data;
    appsync_free_elem(elem);
}

void appsync_free_appsync_list(void)
{
    if( appsync_sync_list != 0 )
    {
        /*g_list_free_full(appsync_sync_list, appsync_free_elem); */
        g_list_foreach (appsync_sync_list, appsync_free_elem_cb, NULL);
        g_list_free (appsync_sync_list);
        appsync_sync_list = 0;
        log_debug("Appsync list freed\n");
    }
}

static gint appsync_list_sort_func(gconstpointer a, gconstpointer b)
{
    return strcasecmp( (char*)a, (char*)b );
}

void appsync_read_list(int diag)
{
    GDir *confdir = 0;

    const gchar *dirname;
    struct list_elem *list_item;

    appsync_free_appsync_list();

    if(diag)
    {
        if( !(confdir = g_dir_open(CONF_DIR_DIAG_PATH, 0, NULL)) )
            goto cleanup;
    }
    else
    {
        if( !(confdir = g_dir_open(CONF_DIR_PATH, 0, NULL)) )
            goto cleanup;
    }

    while( (dirname = g_dir_read_name(confdir)) )
    {
        log_debug("Read file %s\n", dirname);
        if( (list_item = appsync_read_file(dirname, diag)) )
            appsync_sync_list = g_list_append(appsync_sync_list, list_item);
    }

cleanup:
    if( confdir ) g_dir_close(confdir);

    /* sort list alphabetically so services for a mode
     * can be run in a certain order */
    appsync_sync_list=g_list_sort(appsync_sync_list, appsync_list_sort_func);

    /* set up session bus connection if app sync in use
     * so we do not need to make the time consuming connect
     * operation at enumeration time ... */

    if( appsync_sync_list )
    {
        log_debug("Sync list valid\n");
#ifdef APP_SYN_DBUS
        dbusappsync_init_connection();
#endif
    }
}

static struct list_elem *appsync_read_file(const gchar *filename, int diag)
{
    gchar *full_filename = NULL;
    GKeyFile *settingsfile = NULL;
    struct list_elem *list_item = NULL;

    if(diag)
    {
        if( !(full_filename = g_strconcat(CONF_DIR_DIAG_PATH, "/", filename, NULL)) )
            goto cleanup;
    }
    else
    {
        if( !(full_filename = g_strconcat(CONF_DIR_PATH, "/", filename, NULL)) )
            goto cleanup;
    }

    if( !(settingsfile = g_key_file_new()) )
        goto cleanup;

    if( !g_key_file_load_from_file(settingsfile, full_filename, G_KEY_FILE_NONE, NULL) )
        goto cleanup;

    if( !(list_item = calloc(1, sizeof *list_item)) )
        goto cleanup;

    list_item->name = g_key_file_get_string(settingsfile, APP_INFO_ENTRY, APP_INFO_NAME_KEY, NULL);
    log_debug("Appname = %s\n", list_item->name);
    list_item->launch = g_key_file_get_string(settingsfile, APP_INFO_ENTRY, APP_INFO_LAUNCH_KEY, NULL);
    log_debug("Launch = %s\n", list_item->launch);
    list_item->mode = g_key_file_get_string(settingsfile, APP_INFO_ENTRY, APP_INFO_MODE_KEY, NULL);
    log_debug("Launch mode = %s\n", list_item->mode);
    list_item->systemd = g_key_file_get_integer(settingsfile, APP_INFO_ENTRY, APP_INFO_SYSTEMD_KEY, NULL);
    log_debug("Systemd control = %d\n", list_item->systemd);
    list_item->post = g_key_file_get_integer(settingsfile, APP_INFO_ENTRY, APP_INFO_POST, NULL);
    list_item->state = APP_STATE_DONTCARE;

cleanup:

    if(settingsfile)
        g_key_file_free(settingsfile);
    g_free(full_filename);

    /* if a minimum set of required elements is not filled in we discard the list_item */
    if( list_item && !(list_item->name && list_item->mode) )
    {
        log_debug("Element invalid, discarding\n");
        appsync_free_elem(list_item);
        list_item = 0;
    }

    return list_item;
}

/* @return 0 on succes, 1 if there is a failure */
int appsync_activate_sync(const char *mode)
{
    GList *iter;
    int count = 0;

    log_debug("activate sync");

    /* Get start of activation timestamp */
    gettimeofday(&appsync_sync_tv, 0);

    if( appsync_sync_list == 0 )
    {
        log_debug("No sync list! Enumerating\n");
        appsync_enumerate_usb();
        return 0;
    }

    /* Count apps that need to be activated for this mode and
     * mark them as currently inactive */
    for( iter = appsync_sync_list; iter; iter = g_list_next(iter) )
    {
        struct list_elem *data = iter->data;

        if(!strcmp(data->mode, mode))
        {
            ++count;
            data->state = APP_STATE_INACTIVE;
        }
        else
        {
            data->state = APP_STATE_DONTCARE;
        }
    }

    /* If there is nothing to activate, enumerate immediately */
    if(count <= 0)
    {
        log_debug("Nothing to launch.\n");
        appsync_enumerate_usb();
        return(0);
    }

#ifdef APP_SYNC_DBUS
    /* check dbus initialisation, skip dbus activated services if this fails */
    if(!dbusappsync_init())
    {
        log_debug("dbus setup failed => skipping dbus launched apps \n");
        appsync_no_dbus = 1;
    }
#endif /* APP_SYNC_DBUS */

    /* start timer */
    appsync_start_enumerate_usb_timer();

    /* go through list and launch apps */
    for( iter = appsync_sync_list; iter; iter = g_list_next(iter) )
    {
        struct list_elem *data = iter->data;
        if(!strcmp(mode, data->mode))
        {
            /* do not launch items marked as post, will be launched after usb is up */
            if(data->post)
            {
                continue;
            }
            log_debug("launching pre-enum-app %s\n", data->name);
            if(data->systemd)
            {
                if(!systemd_control_service(data->name, SYSTEMD_START))
                    goto error;
                appsync_mark_active(data->name, 0);
            }
            else if(data->launch)
            {
                /* skipping if dbus session bus is not available,
                 * or not compiled in */
                if(appsync_no_dbus)
                    appsync_mark_active(data->name, 0);
#ifdef APP_SYNC_DBUS
                else
                    if(!dbusappsync_launch_app(data->launch))
                        appsync_mark_active(data->name, 0);
                else
                    goto error;
#endif /* APP_SYNC_DBUS */
            }
        }
    }

    return(0);

error:
    log_warning("Error launching a service!\n");
    return(1);
}

int appsync_activate_sync_post(const char *mode)
{
    GList *iter;

    log_debug("activate post sync");

    if( appsync_sync_list == 0 )
    {
        log_debug("No sync list! skipping post sync\n");
        return 0;
    }

#ifdef APP_SYNC_DBUS
    /* check dbus initialisation, skip dbus activated services if this fails */
    if(!dbusappsync_init())
    {
        log_debug("dbus setup failed => skipping dbus launched apps \n");
        appsync_no_dbus = 1;
    }
#endif /* APP_SYNC_DBUS */

    /* go through list and launch apps */
    for( iter = appsync_sync_list; iter; iter = g_list_next(iter) )
    {
        struct list_elem *data = iter->data;
        if(!strcmp(mode, data->mode))
        {
            /* launch only items marked as post, others are already running */
            if(!data->post)
                continue;
            log_debug("launching post-enum-app %s\n", data->name);
            if(data->systemd)
            {
                if(!systemd_control_service(data->name, SYSTEMD_START))
                    goto error;
                appsync_mark_active(data->name, 1);
            }
            else if(data->launch)
            {
                /* skipping if dbus session bus is not available,
                 * or not compiled in */
                if(appsync_no_dbus)
                    continue;
#ifdef APP_SYNC_DBUS
                else
                    if(dbusappsync_launch_app(data->launch) != 0)
                        goto error;
#endif /* APP_SYNC_DBUS */
            }
        }
    }

    return(0);

error:
    log_warning("Error launching a service!\n");
    return(1);
}

int appsync_mark_active(const gchar *name, int post)
{
    int ret = -1; // assume name not found
    int missing = 0;

    GList *iter;

    log_debug("%s-enum-app %s is started\n", post ? "post" : "pre", name);

    for( iter = appsync_sync_list; iter; iter = g_list_next(iter) )
    {
        struct list_elem *data = iter->data;

        if(!strcmp(data->name, name))
        {
            /* TODO: do we need to worry about duplicate names in the list? */
            ret = (data->state != APP_STATE_ACTIVE);
            data->state = APP_STATE_ACTIVE;

            /* updated + missing -> not going to enumerate */
            if( missing ) break;
        }
        else if( data->state == APP_STATE_INACTIVE && data->post == post )
        {
            missing = 1;

            /* updated + missing -> not going to enumerate */
            if( ret != -1 ) break;
        }
    }
    if( !post && !missing )
    {
        log_debug("All pre-enum-apps active. Let's enumerate\n");
        appsync_enumerate_usb();
    }

    /* -1=not found, 0=already active, 1=activated now */
    return ret;
}

static gboolean appsync_enumerate_usb_cb(gpointer data)
{
    (void)data;
    appsync_enumerate_usb_id = 0;
    log_debug("handling enumeration timeout");
    appsync_enumerate_usb();
    /* return false to stop the timer from repeating */
    return FALSE;
}

static void appsync_start_enumerate_usb_timer(void)
{
    log_debug("scheduling enumeration timeout");
    if( appsync_enumerate_usb_id )
        g_source_remove(appsync_enumerate_usb_id), appsync_enumerate_usb_id = 0;
    appsync_enumerate_usb_id = g_timeout_add_seconds(2, appsync_enumerate_usb_cb, NULL);
}

static void appsync_cancel_enumerate_usb_timer(void)
{
    if( appsync_enumerate_usb_id )
    {
        log_debug("canceling enumeration timeout");
        g_source_remove(appsync_enumerate_usb_id), appsync_enumerate_usb_id = 0;
    }
}

static void appsync_enumerate_usb(void)
{
    struct timeval tv;

    /* Stop the timer in case of explicit enumeration call */
    appsync_cancel_enumerate_usb_timer();

    /* Debug: how long it took from sync start to get here */
    gettimeofday(&tv, 0);
    timersub(&tv, &appsync_sync_tv, &tv);
    log_debug("sync to enum: %.3f seconds", tv.tv_sec + tv.tv_usec * 1e-6);

#ifdef APP_SYNC_DBUS
    /* remove dbus service */
    dbusappsync_cleanup();
#endif /* APP_SYNC_DBUS */
}

void appsync_stop_apps(int post)
{
    GList *iter = 0;

    for( iter = appsync_sync_list; iter; iter = g_list_next(iter) )
    {
        struct list_elem *data = iter->data;

        if(data->systemd && data->state == APP_STATE_ACTIVE && data->post == post)
        {
            log_debug("stopping %s-enum-app %s", post ? "post" : "pre", data->name);
            if(!systemd_control_service(data->name, SYSTEMD_STOP))
                log_debug("Failed to stop %s\n", data->name);
            data->state = APP_STATE_DONTCARE;
        }
    }
}

int appsync_stop(gboolean force)
{
    /* If force arg is used, stop all applications that
     * could have been started by usb-moded */
    if(force)
    {
        GList *iter;
        log_debug("assuming all applications are active");

        for( iter = appsync_sync_list; iter; iter = g_list_next(iter) )
        {
            struct list_elem *data = iter->data;
            data->state = APP_STATE_ACTIVE;
        }
    }

    /* Stop post-apps 1st */
    appsync_stop_apps(1);

    /* Then pre-apps */
    appsync_stop_apps(0);

    /* Do not leave active timers behind */
    appsync_cancel_enumerate_usb_timer();
    return(0);
}
