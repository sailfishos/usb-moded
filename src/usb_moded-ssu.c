/**
 * @file usb_moded-ssu.c
 *
 * Copyright (c) 2016 - 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Simo Piiroinen <simo.piiroinen@jollamobile.com>
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

#include "usb_moded-ssu.h"

#include "usb_moded-log.h"

#include <stdlib.h>
#include <string.h>

#include <ssusysinfo.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * SSU
 * ------------------------------------------------------------------------- */

static void          ssu_free_handle          (void);
static ssusysinfo_t *ssu_get_handle           (void);
gchar               *ssu_get_manufacturer_name(void);
gchar               *ssu_get_product_name     (void);

/* ========================================================================= *
 * Data
 * ========================================================================= */

/** Cached ssu-sysinfo handle */
static ssusysinfo_t *ssu_instance = 0;

/** Flag for ssu-sysinfo instance has been initialized */
static gboolean      ssu_intialized = FALSE;

/* ========================================================================= *
 * Functions
 * ========================================================================= */

/** Atexit callback for releasing cached ssu-sysinfo handle */
static void ssu_free_handle(void)
{
    LOG_REGISTER_CONTEXT;

    /* Make sure instance does not get created on exit path */
    ssu_intialized = TRUE;

    /* Release existing instance */
    ssusysinfo_delete(ssu_instance),
        ssu_instance = 0;
}

/** Helper for obtaining ssu-sysinfo handle on demand
 *
 * @return ssu-sysinfo, or NULL in case of errors
 */
static ssusysinfo_t *ssu_get_handle(void)
{
    LOG_REGISTER_CONTEXT;

    /* Attempt only once */
    if( !ssu_intialized ) {
        ssu_intialized = TRUE;
        ssu_instance = ssusysinfo_create();
        atexit(ssu_free_handle);
    }
    return ssu_instance;
}

/** Read device manufacturer name from the SSU configuration
 *
 * Caller must release non-null return value with g_free().
 *
 * @return human readable string, or NULL in case of errors
 */
gchar *
ssu_get_manufacturer_name(void)
{
    LOG_REGISTER_CONTEXT;

    gchar *res = 0;
    const char *val = ssusysinfo_device_manufacturer(ssu_get_handle());
    if( val && strcmp(val, "UNKNOWN") )
        res = g_strdup(val);
    log_debug("%s() -> %s", __FUNCTION__, res ?: "N/A");
    return res;
}

/** Read device model name from the SSU configuration
 *
 * Caller must release non-null return value with g_free().
 *
 * @return human readable string, or NULL in case of errors
 */
gchar *
ssu_get_product_name(void)
{
    LOG_REGISTER_CONTEXT;

    gchar *res = 0;
    const char *val = ssusysinfo_device_pretty_name(ssu_get_handle());
    if( val && strcmp(val, "UNKNOWN") )
        res = g_strdup(val);
    log_debug("%s() -> %s", __FUNCTION__, res ?: "N/A");
    return res;
}
