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

#include "usb_moded-modules.h"

#include "usb_moded-log.h"

#include <stdlib.h>
#include <string.h>

#include <libkmod.h>

#include <glib.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- modules -- */

bool        modules_init                     (void);
bool        modules_in_use                   (void);
void        modules_quit                     (void);
int         modules_load_module              (const char *module);
int         modules_unload_module            (const char *module);

/* ========================================================================= *
 * Data
 * ========================================================================= */

/** Availability of kernel module based gadget configuration functionality
 *
 * -1 = not checked yet
 *  0 = not available
 *  1 = chosen as means of gadget configuration for usb-moded
 */
static int modules_probed = -1;

/* kmod context - initialized at start in usbmoded_init by ctx_init()
 *  and cleaned up by ctx_cleanup() functions */
static struct kmod_ctx *modules_ctx = 0;

/* ========================================================================= *
 * Functions
 * ========================================================================= */

static bool modules_have_module(const char *module)
{
    // TODO: not fully untested due to lack of suitable hw

    bool ack = false;
    struct kmod_list *list = 0;

    if( kmod_module_new_from_lookup(modules_ctx, module, &list) < 0 )
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

    if( !modules_ctx ) {
        if( !(modules_ctx = kmod_new(NULL, NULL)) )
            goto EXIT;
    }

    if( kmod_load_resources(modules_ctx) < 0 )
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
    if( modules_ctx )
        kmod_unref(modules_ctx), modules_ctx = 0;
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
         * so no need to handle them separately  */
        gchar **strings;

        /* since the mass_storage module is the newer one and we check against it to avoid
         * loading failures we use it here, as we fall back to g_file_storage if g_mass_storage
         * fails to load */
        strings = g_strsplit(MODULE_CHARGE_FALLBACK, " ", 2);
        //log_debug("module args = %s, module = %s\n", strings[1], strings[0]);
        charging_args = strdup(strings[1]);
        /* load was already assigned. Free it to re-assign */
        free(load);
        load = strdup(strings[0]);
        g_strfreev(strings);

    }
    ret = kmod_module_new_from_name(modules_ctx, load, &mod);
    /* since kmod_module_new_from_name does not check if the module
     * exists we test it's path in case we deal with the mass-storage one */
    if(!strcmp(module, MODULE_MASS_STORAGE) &&
       (kmod_module_get_path(mod) == NULL))
    {
        log_debug("Fallback on older g_file_storage\n");
        ret = kmod_module_new_from_name(modules_ctx, MODULE_FILE_STORAGE, &mod);
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
    return ret;
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

    kmod_module_new_from_name(modules_ctx, module, &mod);
    ret = kmod_module_remove_module(mod, KMOD_REMOVE_NOWAIT);
    kmod_module_unref(mod);

    return ret;
}
