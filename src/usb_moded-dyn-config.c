/**
 * @file usb_moded-dyn-config.c
 *
 * Copyright (c) 2011 Nokia Corporation. All rights reserved.
 * Copyright (c) 2013 - 2021 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Philippe De Swert <philippedeswert@gmail.com>
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author Thomas Perl <thomas.perl@jolla.com>
 * @author Slava Monich <slava.monich@jolla.com>
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

#include "usb_moded-dyn-config.h"

#include "usb_moded-log.h"

#include <glob.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * MODEDATA
 * ------------------------------------------------------------------------- */

static void        modedata_free_cb(gpointer self);
void               modedata_free   (modedata_t *self);
modedata_t        *modedata_copy   (const modedata_t *that);
static gint        modedata_sort_cb(gconstpointer a, gconstpointer b);
static modedata_t *modedata_load   (const gchar *filename);

/* ------------------------------------------------------------------------- *
 * MODELIST
 * ------------------------------------------------------------------------- */

void   modelist_free(GList *modelist);
GList *modelist_load(bool diag);

/* ========================================================================= *
 * MODEDATA
 * ========================================================================= */

/** Type agnostice relase modedata_t object callback
 *
 * @param self Object pointer, or NULL
 */
static void
modedata_free_cb(gpointer self)
{
    modedata_free(self);
}

/** Relase modedata_t object
 *
 * @param self Object pointer, or NULL
 */
void
modedata_free(modedata_t *self)
{
    LOG_REGISTER_CONTEXT;

    if( self ) {
        g_free(self->mode_name);
        g_free(self->mode_module);
        g_free(self->network_interface);
        g_free(self->sysfs_path);
        g_free(self->sysfs_value);
        g_free(self->sysfs_reset_value);
        g_free(self->android_extra_sysfs_path);
        g_free(self->android_extra_sysfs_value);
        g_free(self->android_extra_sysfs_path2);
        g_free(self->android_extra_sysfs_value2);
        g_free(self->android_extra_sysfs_path3);
        g_free(self->android_extra_sysfs_value3);
        g_free(self->android_extra_sysfs_path4);
        g_free(self->android_extra_sysfs_value4);
        g_free(self->idProduct);
        g_free(self->idVendorOverride);
#ifdef CONNMAN
        g_free(self->connman_tethering);
#endif
        free(self);
    }
}

/** Clone modedata_t object
 *
 * @param that Object pointer, or NULL
 *
 * @return Object pointer, or NULL
 */
modedata_t *
modedata_copy(const modedata_t *that)
{
    modedata_t *self = 0;

    if( !that )
        goto EXIT;

    if( !(self = calloc(1, sizeof *self)) )
        goto EXIT;

    self->mode_name                  = g_strdup(that->mode_name);
    self->mode_module                = g_strdup(that->mode_module);
    self->appsync                    = that->appsync;
    self->network                    = that->network;
    self->mass_storage               = that->mass_storage;
    self->network_interface          = g_strdup(that->network_interface);
    self->sysfs_path                 = g_strdup(that->sysfs_path);
    self->sysfs_value                = g_strdup(that->sysfs_value);
    self->sysfs_reset_value          = g_strdup(that->sysfs_reset_value);
    self->android_extra_sysfs_path   = g_strdup(that->android_extra_sysfs_path);
    self->android_extra_sysfs_value  = g_strdup(that->android_extra_sysfs_value);
    self->android_extra_sysfs_path2  = g_strdup(that->android_extra_sysfs_path2);
    self->android_extra_sysfs_value2 = g_strdup(that->android_extra_sysfs_value2);
    self->android_extra_sysfs_path3  = g_strdup(that->android_extra_sysfs_path3);
    self->android_extra_sysfs_value3 = g_strdup(that->android_extra_sysfs_value3);
    self->android_extra_sysfs_path4  = g_strdup(that->android_extra_sysfs_path4);
    self->android_extra_sysfs_value4 = g_strdup(that->android_extra_sysfs_value4);
    self->idProduct                  = g_strdup(that->idProduct);
    self->idVendorOverride           = g_strdup(that->idVendorOverride);
    self->nat                        = that->nat;
    self->dhcp_server                = that->dhcp_server;
#ifdef CONNMAN
    self->connman_tethering          = g_strdup(that->connman_tethering);
#endif

EXIT:
    return self;
}

/** Callback for sorting mode list alphabetically
 *
 * For use with g_list_sort()
 *
 * @param a  Object pointer
 * @param b  Object pointer
 *
 * @return result of comparing object names
 */
static gint
modedata_sort_cb(gconstpointer a, gconstpointer b)
{
    LOG_REGISTER_CONTEXT;

    modedata_t *aa = (modedata_t *)a;
    modedata_t *bb = (modedata_t *)b;

    return g_strcmp0(aa->mode_name, bb->mode_name);
}

/** Load mode data from file
 *
 * @param filename  Path to file from which to read
 *
 * @return Mode data object, or NULL
 */
static modedata_t *
modedata_load(const gchar *filename)
{
    LOG_REGISTER_CONTEXT;

    modedata_t *self        = NULL;
    bool        success     = false;
    GKeyFile   *settingsfile = g_key_file_new();

    if( !g_key_file_load_from_file(settingsfile, filename, G_KEY_FILE_NONE, NULL) ) {
        log_err("%s: can't read mode configuration file", filename);
        goto EXIT;
    }

    if( !(self = calloc(1, sizeof *self)) )
        goto EXIT;

    // [MODE_ENTRY = "mode"]
    self->mode_name         = g_key_file_get_string(settingsfile, MODE_ENTRY, MODE_NAME_KEY, NULL);
    self->mode_module       = g_key_file_get_string(settingsfile, MODE_ENTRY, MODE_MODULE_KEY, NULL);

    log_debug("Dynamic mode name = %s\n", self->mode_name);
    log_debug("Dynamic mode module = %s\n", self->mode_module);

    self->appsync           = g_key_file_get_integer(settingsfile, MODE_ENTRY, MODE_NEEDS_APPSYNC_KEY, NULL);
    self->mass_storage      = g_key_file_get_integer(settingsfile, MODE_ENTRY, MODE_MASS_STORAGE_KEY, NULL);
    self->network           = g_key_file_get_integer(settingsfile, MODE_ENTRY, MODE_NETWORK_KEY, NULL);
    self->network_interface = g_key_file_get_string(settingsfile,  MODE_ENTRY, MODE_NETWORK_INTERFACE_KEY, NULL);

    // [MODE_OPTIONS_ENTRY = "options"]
    self->sysfs_path                 = g_key_file_get_string(settingsfile,  MODE_OPTIONS_ENTRY, MODE_SYSFS_PATH, NULL);
    self->sysfs_value                = g_key_file_get_string(settingsfile,  MODE_OPTIONS_ENTRY, MODE_SYSFS_VALUE, NULL);
    self->sysfs_reset_value          = g_key_file_get_string(settingsfile,  MODE_OPTIONS_ENTRY, MODE_SYSFS_RESET_VALUE, NULL);

    self->android_extra_sysfs_path   = g_key_file_get_string(settingsfile,  MODE_OPTIONS_ENTRY, MODE_ANDROID_EXTRA_SYSFS_PATH, NULL);
    self->android_extra_sysfs_path2  = g_key_file_get_string(settingsfile,  MODE_OPTIONS_ENTRY, MODE_ANDROID_EXTRA_SYSFS_PATH2, NULL);
    self->android_extra_sysfs_path3  = g_key_file_get_string(settingsfile,  MODE_OPTIONS_ENTRY, MODE_ANDROID_EXTRA_SYSFS_PATH3, NULL);
    self->android_extra_sysfs_path4  = g_key_file_get_string(settingsfile,  MODE_OPTIONS_ENTRY, MODE_ANDROID_EXTRA_SYSFS_PATH4, NULL);
    self->android_extra_sysfs_value  = g_key_file_get_string(settingsfile,  MODE_OPTIONS_ENTRY, MODE_ANDROID_EXTRA_SYSFS_VALUE, NULL);
    self->android_extra_sysfs_value2 = g_key_file_get_string(settingsfile,  MODE_OPTIONS_ENTRY, MODE_ANDROID_EXTRA_SYSFS_VALUE2, NULL);
    self->android_extra_sysfs_value3 = g_key_file_get_string(settingsfile,  MODE_OPTIONS_ENTRY, MODE_ANDROID_EXTRA_SYSFS_VALUE3, NULL);
    self->android_extra_sysfs_value4 = g_key_file_get_string(settingsfile,  MODE_OPTIONS_ENTRY, MODE_ANDROID_EXTRA_SYSFS_VALUE4, NULL);

    self->idProduct                  = g_key_file_get_string(settingsfile,  MODE_OPTIONS_ENTRY, MODE_IDPRODUCT, NULL);
    self->idVendorOverride           = g_key_file_get_string(settingsfile,  MODE_OPTIONS_ENTRY, MODE_IDVENDOROVERRIDE, NULL);
    self->nat                        = g_key_file_get_integer(settingsfile, MODE_OPTIONS_ENTRY, MODE_HAS_NAT, NULL);
    self->dhcp_server                = g_key_file_get_integer(settingsfile, MODE_OPTIONS_ENTRY, MODE_HAS_DHCP_SERVER, NULL);
#ifdef CONNMAN
    self->connman_tethering          = g_key_file_get_string(settingsfile,  MODE_OPTIONS_ENTRY, MODE_CONNMAN_TETHERING, NULL);
#endif

    //log_debug("Dynamic mode sysfs path = %s\n", self->sysfs_path);
    //log_debug("Dynamic mode sysfs value = %s\n", self->sysfs_value);
    //log_debug("Android extra mode sysfs path2 = %s\n", self->android_extra_sysfs_path2);
    //log_debug("Android extra value2 = %s\n", self->android_extra_sysfs_value2);

    if( self->mode_name == NULL || self->mode_module == NULL ) {
        log_err("%s: mode_name or mode_module not defined", filename);
        goto EXIT;
    }

    if( self->network && self->network_interface == NULL) {
        log_err("%s: network not fully defined", filename);
        goto EXIT;
    }

    if( (self->sysfs_path && !self->sysfs_value) ||
        (self->sysfs_reset_value && !self->sysfs_path) ) {
        /* In theory all of this is optional.
         *
         * In most cases 'sysfs_value' holds a list of functions to enable,
         * and 'sysfs_path' or 'sysfs_reset_value' values are simply ignored.
         *
         * However, for the benefit of existing special configuration files
         * like the one for host mode:
         * - having sysfs_path implies that sysfs_value should be set too
         * - having sysfs_reset_value implies that sysfs_path should be set
         */
        log_err("%s: sysfs_value not fully defined", filename);
        goto EXIT;
    }

    log_debug("%s: successfully loaded", filename);
    success = true;

EXIT:
    g_key_file_free(settingsfile);

    if( !success )
        modedata_free(self), self = 0;

    return self;
}

/* ========================================================================= *
 * MODELIST
 * ========================================================================= */

/** Release mode list
 *
 * @param modelist List pointer, or NULL
 */
void
modelist_free(GList *modelist)
{
    LOG_REGISTER_CONTEXT;

    g_list_free_full(modelist, modedata_free_cb);
}

/** Load mode data files from configuration directory
 *
 * @param diag  true to load diagnostic modes, or
 *              false for normal modes
 *
 * @return List of mode data objects, or NULL
 */
GList *
modelist_load(bool diag)
{
    LOG_REGISTER_CONTEXT;

    GList      *modelist = 0;
    const char *dirpath  = diag ? DIAG_DIR_PATH : MODE_DIR_PATH;
    gchar      *pattern  = g_strdup_printf("%s/*.ini", dirpath);
    glob_t      gb       = {};

    if( glob(pattern, 0, 0, &gb) != 0 )
        log_debug("no mode configuration ini-files found");

    for( size_t i = 0; i < gb.gl_pathc; ++i ) {
        const char *filepath = gb.gl_pathv[i];
        log_debug("Read file %s\n", filepath);
        modedata_t *list_item = modedata_load(filepath);
        if(list_item)
            modelist = g_list_append(modelist, list_item);
    }

    globfree(&gb);
    g_free(pattern);

    return g_list_sort(modelist, modedata_sort_cb);
}
