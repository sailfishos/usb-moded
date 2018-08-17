/**
 * @file usb_moded-android.c
 *
 * Copyright (C) 2013-2018 Jolla. All rights reserved.
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
#include <glib.h>

#include "usb_moded.h"
#include "usb_moded-android.h"
#include "usb_moded-log.h"
#include "usb_moded-modesetting.h"
#include "usb_moded-config-private.h"
#include "usb_moded-mac.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/* ========================================================================= *
 * Functions
 * ========================================================================= */

/* -- android -- */

bool         android_in_use           (void);
static bool  android_probe            (void);
gchar       *android_get_serial       (void);
bool         android_init_values      (void);
bool         android_set_enabled      (bool enable);
bool         android_set_charging_mode(void);
bool         android_set_function     (const char *function);
bool         android_set_productid    (const char *id);
bool         android_set_vendorid     (const char *id);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static int android_probed = -1;

/* ========================================================================= *
 * Functions
 * ========================================================================= */

bool
android_in_use(void)
{
    if( android_probed < 0 )
        log_debug("android_in_use() called before android_probe()");

    return android_probed > 0;
}

static bool
android_probe(void)
{
    if( android_probed <= 0 ) {
        android_probed = access(ANDROID0_ENABLE, F_OK) == 0;
        log_warning("ANDROID0 %sdetected", android_probed ? "" : "not ");
    }

    return android_in_use();
}

/** Read android serial number from kernel command line
 */
gchar *
android_get_serial(void)
{
    static const char path[] = "/proc/cmdline";
    static const char find[] = "androidboot.serialno=";
    static const char pbrk[] = " \t\r\n,";

    char   *res  = 0;
    FILE   *file = 0;
    size_t  size = 0;
    char   *data = 0;

    if( !(file = fopen(path, "r")) ) {
        log_warning("%s: %s: %m", path, "can't open");
        goto EXIT;
    }

    if( getline(&data, &size, file) < 0 ) {
        log_warning("%s: %s: %m", path, "can't read");
        goto EXIT;
    }

    char *beg = strstr(data, find);
    if( !beg ) {
        log_warning("%s: no serial found", path);
        goto EXIT;
    }

    beg += sizeof find - 1;
    size_t len = strcspn(beg, pbrk);
    if( len < 1 ) {
        log_warning("%s: empty serial found", path);
        goto EXIT;
    }

    res = g_strndup(beg, len);

EXIT:

    free(data);

    if( file )
        fclose(file);

    return res;
}

/** initialize the basic android values
 */
bool
android_init_values(void)
{
    gchar *text;

    if( !android_probe() )
        goto EXIT;

    /* Disable */
    android_set_enabled(false);

    /* Configure */
    if( (text = android_get_serial()) )
    {
        write_to_file(ANDROID0_SERIAL, text);
        g_free(text);
    }

    text = config_get_android_manufacturer();
    if(text)
    {
        write_to_file(ANDROID0_MANUFACTURER, text);
        g_free(text);
    }
    text = config_get_android_vendor_id();
    if(text)
    {
        android_set_vendorid(text);
        g_free(text);
    }
    text = config_get_android_product();
    if(text)
    {
        write_to_file(ANDROID0_PRODUCT, text);
        g_free(text);
    }
    text = config_get_android_product_id();
    if(text)
    {
        android_set_productid(text);
        g_free(text);
    }
    text = mac_read_mac();
    if(text)
    {
        write_to_file("/sys/class/android_usb/f_rndis/ethaddr", text);
        g_free(text);
    }
    /* For rndis to be discovered correctly in M$ Windows (vista and later) */
    write_to_file("/sys/class/android_usb/f_rndis/wceis", "1");

EXIT:
    return android_in_use();
}

bool
android_set_enabled(bool enable)
{
    bool ack = false;
    if( android_in_use() ) {
        const char *val = enable ? "1" : "0";
        ack = write_to_file(ANDROID0_ENABLE, val) != -1;
    }
    log_debug("ANDROID %s(%d) -> %d", __func__, enable, ack);
    return ack;
}

/* Set a charging mode for the android gadget
 *
 * @return true if successful, false on failure
 */
bool
android_set_charging_mode(void)
{
    bool ack = false;

    if( !android_in_use() )
        goto EXIT;

    if( !android_set_function("mass_storage") )
        goto EXIT;

     /* TODO: make configurable */
    if( !android_set_productid("0AFE") )
        goto EXIT;

    if( !android_set_enabled(true) )
        goto EXIT;

    ack = true;

EXIT:
    log_debug("ANDROID %s() -> %d", __func__, ack);
    return ack;
}

/* Set a function for the android gadget
 *
 * @return true if successful, false on failure
 */
bool
android_set_function(const char *function)
{
    bool ack = false;

    if( !function )
        goto EXIT;

    if( !android_in_use() )
        goto EXIT;

    if( !android_set_enabled(false) )
        goto EXIT;

    if( write_to_file(ANDROID0_FUNCTIONS, function) == -1 )
        goto EXIT;

    /* Leave disabled, so that caller can adjust attributes
     * etc before enabling */

    ack = true;
EXIT:

    log_debug("ANDROID %s(%s) -> %d", __func__, function, ack);
    return ack;
}

/* Set a product id for the android gadget
 *
 * @return true if successful, false on failure
 */
bool
android_set_productid(const char *id)
{
    bool ack = false;
    if( id && android_in_use() ) {
        ack = write_to_file(ANDROID0_ID_PRODUCT, id) != -1;
    }
    log_debug("ANDROID %s(%s) -> %d", __func__, id, ack);
    return ack;
}

/* Set a vendor id for the android gadget
 *
 * @return true if successful, false on failure
 */
bool
android_set_vendorid(const char *id)
{
    bool ack = false;
    if( id && android_in_use() ) {
        ack = write_to_file(ANDROID0_ID_VENDOR, id) != -1;
    }
    log_debug("ANDROID %s(%s) -> %d", __func__, id, ack);
    return ack;
}
