/**
 * @file usb_moded-modules.c
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Copyright (C) 2012-2018 Jolla. All rights reserved.
 *
 * @author: Philippe De Swert <philippe.de-swert@nokia.com>
 * @author: Philippe De Swert <phdeswer@lumi.maa>
 * @author: Philippe De Swert <philippedeswert@gmail.com>
 * @author: Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author: Thomas Perl <m@thp.io>
 * @author: Slava Monich <slava.monich@jolla.com>
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

#include <glib.h>
#include <glib/gprintf.h>

#include <libkmod.h>

#include "usb_moded.h"
#include "usb_moded-modules.h"
#include "usb_moded-log.h"
#include "usb_moded-config.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded-modesetting.h"
#include "usb_moded-modes.h"

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- modules -- */

bool        modules_init                     (void);
bool        modules_in_use                   (void);
void        modules_quit                     (void);
int         modules_load_module              (const char *module);
int         modules_unload_module            (const char *module);
static int  modules_module_is_loaded         (const char *module);
const char *modules_get_loaded_module        (void);
int         modules_cleanup_module           (const char *module);
static int  modules_prepare_for_module_switch(int force);
void        modules_check_module_state       (const char *module_name);

/* ========================================================================= *
 * Data
 * ========================================================================= */

/* kmod context - initialized at start in usbmoded_init by ctx_init()
 *  and cleaned up by ctx_cleanup() functions */
static struct kmod_ctx *ctx = 0;

/* ========================================================================= *
 * Functions
 * ========================================================================= */

static bool modules_have_module(const char *module)
{
    // TODO: not fully untested due to lack of suitable hw

    bool ack = false;
    struct kmod_list *list = 0;

    if( kmod_module_new_from_lookup(ctx, module, &list) < 0 )
        goto EXIT;

    if( list == 0 )
        goto EXIT;

    ack = true;

EXIT:
    if( list )
        kmod_module_unref_list(list);

    log_debug("module %s does%s exist", module, ack ? "" : "not ");
    return ack;

}

static int modules_probed = -1;

bool modules_in_use(void)
{
    if( modules_probed < 0 )
        log_debug("modules_in_use() called before modules_probe()");

    return modules_probed > 0;
}

static bool modules_probe(void)
{
    static const char * const lut[] = {
        MODULE_MASS_STORAGE,
        MODULE_FILE_STORAGE,
        MODULE_DEVELOPER,
        MODULE_MTP,
        0
    };

    if( modules_probed == -1 ) {
        modules_probed = false;
        /* Check if we have at least one of the kernel modules we
         * expect to use for something.
         */
        for( size_t i = 0; lut[i] ; ++i ) {
            if( modules_have_module(lut[i]) ) {
                modules_probed = true;
                break;
            }
        }
        log_warning("MODULES %sdetected", modules_probed ? "" : "not ");
    }

    return modules_in_use();
}

/* kmod module init */
bool modules_init(void)
{
    bool ack = false;

    if( !ctx ) {
        if( !(ctx = kmod_new(NULL, NULL)) )
            goto EXIT;
    }

    if( kmod_load_resources(ctx) < 0 )
        goto EXIT;

    if( !modules_probe() )
        goto EXIT;

    ack = true;
EXIT:
    return ack;
}

/* kmod module cleanup */
void modules_quit(void)
{
    if( ctx )
        kmod_unref(ctx), ctx = 0;
}

/** load module
 *
 * @param module Name of the module to load
 * @return 0 on success, non-zero on failure
 *
 */
int modules_load_module(const char *module)
{
    int ret = 0;

    const int probe_flags = KMOD_PROBE_APPLY_BLACKLIST;
    struct kmod_module *mod;
    char *charging_args = NULL;
    char *load = NULL;

    if(!strcmp(module, MODULE_NONE))
        return 0;

    if( !modules_in_use() ) {
        log_warning("load module %s - without module support", module);
        return -1;
    }

    /* copy module to load as it might be modified if we're trying charging mode */
    load = strdup(module);
    if(!strcmp(module, MODULE_CHARGING) || !strcmp(module, MODULE_CHARGE_FALLBACK))
    {
        /* split the string in module name and argument, they are the same for MODULE_CHARGE_FALLBACK
         so no need to handle them separately  */
        gchar **strings;

        /* since the mass_storage module is the newer one and we check against it to avoid
         loading failures we use it here, as we fall back to g_file_storage if g_mass_storage
         fails to load */
        strings = g_strsplit(MODULE_CHARGE_FALLBACK, " ", 2);
        //log_debug("module args = %s, module = %s\n", strings[1], strings[0]);
        charging_args = strdup(strings[1]);
        /* load was already assigned. Free it to re-assign */
        free(load);
        load = strdup(strings[0]);
        g_strfreev(strings);

    }
    ret = kmod_module_new_from_name(ctx, load, &mod);
    /* since kmod_module_new_from_name does not check if the module
     exists we test it's path in case we deal with the mass-storage one */
    if(!strcmp(module, MODULE_MASS_STORAGE) &&
       (kmod_module_get_path(mod) == NULL))
    {
        log_debug("Fallback on older g_file_storage\n");
        ret = kmod_module_new_from_name(ctx, MODULE_FILE_STORAGE, &mod);
    }

    if(!charging_args)
        ret = kmod_module_probe_insert_module(mod, probe_flags, NULL, NULL, NULL, NULL);
    else
    {
        ret = kmod_module_probe_insert_module(mod, probe_flags, charging_args, NULL, NULL, NULL);
        free(charging_args);
    }
    kmod_module_unref(mod);
    free(load);

    if( ret == 0)
        log_info("Module %s loaded successfully\n", module);
    else
        log_info("Module %s failed to load\n", module);
    return(ret);
}

/** unload module
 *
 * @param module Name of the module to unload
 * @return 0 on success, non-zero on failure
 *
 */
int modules_unload_module(const char *module)
{
    int ret = 0;

    struct kmod_module *mod;

    if(!strcmp(module, MODULE_NONE))
        return 0;

    if( !modules_in_use() ) {
        log_warning("unload module %s - without module support", module);
        return -1;
    }

    kmod_module_new_from_name(ctx, module, &mod);
    ret = kmod_module_remove_module(mod, KMOD_REMOVE_NOWAIT);
    kmod_module_unref(mod);

    return(ret);
}

/** Check which state a module is in
 *
 * @return 1 if loaded, 0 when not loaded
 */
static int modules_module_is_loaded(const char *module)
{
    int ret = 0;
    struct kmod_module *mod;

    kmod_module_new_from_name(ctx, module, &mod);
    ret = kmod_module_get_initstate(mod);
    kmod_module_unref(mod);
    if( ret == KMOD_MODULE_LIVE)
        return 1;

    return 0;
}

/** find which module is loaded
 *
 * @return The name of the loaded module, or NULL if no modules are loaded.
 */
const char * modules_get_loaded_module(void)
{
    if( !modules_in_use() ) {
        log_warning("get loaded module - without module support");
        return 0;
    }

    if(modules_module_is_loaded("g_ether"))
        return(MODULE_DEVELOPER);

    if(modules_module_is_loaded("g_ncm"))
        return("g_ncm");

    if(modules_module_is_loaded("g_ffs"))
        return(MODULE_MTP);

    if(modules_module_is_loaded("g_mass_storage"))
        return(MODULE_MASS_STORAGE);

    if(modules_module_is_loaded("g_file_storage"))
        return(MODULE_FILE_STORAGE);

    if(modules_module_is_loaded(usbmoded_get_usb_module()))
        return(usbmoded_get_usb_module());

    /* no module loaded */
    return(0);
}

/** clean up for modules when usb gets disconnected
 *
 * @param module The name of the module to unload
 * @return 0 on success, non-zero on failure
 *
 */
int modules_cleanup_module(const char *module)
{
    int retry = 0, failure;

    if(!strcmp(module, MODULE_NONE))
        goto END;

    if( !modules_in_use() ) {
        log_warning("cleanup module %s - without module support", module);
        goto END;
    }

    /* wait a bit for all components listening on dbus to clean up their act
     usbmoded_sleep(2); */
    /* check if things were not reconnected in that timespan
     if(usbmoded_get_connection_state())
     return(0);
     */

    failure = modules_unload_module(module);

    /* if we have MODULE_MASS_STORAGE it might be MODULE_FILE_STORAGE might
     be loaded. So check and unload that one if unloading fails first time */
    if(failure && !strcmp(MODULE_MASS_STORAGE, module))
        failure = modules_unload_module(MODULE_FILE_STORAGE);

    /* if it still failed it might be the mode has not been cleaned-up correctly,
     so we clean up the mode to be sure */
    if(failure)
    {
        modesetting_cleanup(modules_get_loaded_module());
    }

    while(failure)
    {
        /* module did not get unloaded. We will wait a bit and try again */
        usbmoded_sleep(1);
        /* send the REALLY  disconnect message */
        umdbus_send_state_signal(USB_REALLY_DISCONNECT);
        failure = modules_unload_module(module);
        log_debug("unloading failure = %d\n", failure);
        if(!failure)
            break;
        if(!modules_get_loaded_module())
            goto END;
        retry++;
        if(retry == 2)
            break;
    }
    if(!failure)
        log_info("Module %s unloaded successfully\n", module);
    else
    {
        log_err("Module %s did not unload! Failing and going to undefined.\n", module);
        return(1);
    }
END:
    return(0);
}

/** try to unload modules to support switching
 *
 *
 * @param force force unloading with a nasty clean-up on TRUE, or just try unloading when FALSE
 * @return 0 on success, 1 on failure, 2 if hard clean-up failed
 */

static int modules_prepare_for_module_switch (int force)
{
    if( !modules_in_use() ) {
        log_warning("prep for module switch force=%d - without module support", force);
        return 0;
    }

    const char *unload;
    int ret = 1;

    unload = modules_get_loaded_module();
    if(unload)
    {
        if(force)
            ret = modules_cleanup_module(unload);
        else
            ret = modules_unload_module(unload);
    }
    if(ret && force)
        return(2);
    return ret;
}

/** check for loaded modules and clean-up if they are not for the chosen mode
 *
 * @param module_name  module name to check for
 *
 */
void modules_check_module_state(const char *module_name)
{
    if( !modules_in_use() ) {
        log_warning("check module %s state - without module support", module_name);
        return;
    }

    const char *module;

    module = modules_get_loaded_module();
    if(module != NULL)
    {
        /* do nothing if the right module is already loaded */
        if(strcmp(module, module_name) != 0)
        {
            log_debug("%s not loaded, cleaning up\n", module_name);
            modules_prepare_for_module_switch(TRUE);
        }
    }
}
