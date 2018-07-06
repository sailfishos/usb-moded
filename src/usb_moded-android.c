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

bool         android_in_use                (void);
static bool  android_probe            (void);
gchar       *android_get_serial       (void);
bool         android_init_values      (void);
bool         android_set_charging_mode(void);
bool         android_set_productid    (const char *id);
bool         android_set_vendorid     (const char *id);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static int android_probed = -1;

/* ========================================================================= *
 * Functions
 * ========================================================================= */

bool android_in_use(void)
{
    if( android_probed < 0 )
        log_debug("android_in_use() called before android_probe()");

    return android_probed > 0;
}

static bool android_probe(void)
{
    if( android_probed <= 0 ) {
        struct stat st;

        android_probed = (lstat(ANDROID0_DIRECTORY, &st) == 0 &&
                          S_ISDIR(st.st_mode));
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
bool android_init_values(void)
{
    gchar *text;

    if( !android_probe() )
        goto EXIT;

    /* Disable */
    write_to_file(ANDROID0_ENABLE, "0");

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
        write_to_file(ANDROID0_ID_VENDOR, text);
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
        write_to_file(ANDROID0_ID_PRODUCT, text);
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

    /* Some devices can have enumeration issues due to incomplete
     * configuration on the 1st connect after bootup. Briefly setting
     * up for example mass_storage function can be utilized as a
     * workaround in such cases. */
    if(!usbmoded_init_done_p()) {
        const char *function = usbmoded_get_android_bootup_function();
        if(function) {
            write_to_file(ANDROID0_FUNCTIONS, function);
            write_to_file(ANDROID0_ENABLE, "1");
            write_to_file(ANDROID0_ENABLE, "0");
        }
    }

    /* Clear functions and enable */
    write_to_file(ANDROID0_FUNCTIONS, "none");
    write_to_file(ANDROID0_ENABLE, "1");

EXIT:
    return android_in_use();
}

/* Set a charging mode for the android gadget
 *
 * @return 0 if successful, 1 on failure
 */
bool android_set_charging_mode(void)
{
    bool ack = false;
    if( android_in_use() ) {
        /* disable, set functions to "mass_storage", re-enable */
        write_to_file(ANDROID0_ENABLE, "0");
        write_to_file(ANDROID0_ID_PRODUCT, "0AFE"); /* TODO: make configurable */
        write_to_file(ANDROID0_FUNCTIONS, "mass_storage");
        ack = write_to_file(ANDROID0_ENABLE, "1") != -1;
    }
    log_debug("ANDROID %s() -> %d", __func__, ack);
    return ack;
}

/* Set a product id for the android gadget
 *
 * @return 0 if successful, 1 on failure
 */
bool android_set_productid(const char *id)
{
    bool ack = false;
    if( android_in_use() ) {
        ack = write_to_file(ANDROID0_ID_PRODUCT, id) != -1;
    }
    log_debug("ANDROID %s(%s) -> %d", __func__, id, ack);
    return ack;
}

/* Set a vendor id for the android gadget
 *
 * @return 0 if successful, 1 on failure
 */
bool android_set_vendorid(const char *id)
{
    bool ack = false;
    if( android_in_use() ) {
        ack = write_to_file(ANDROID0_ID_VENDOR, id) != -1;
    }
    log_debug("ANDROID %s(%s) -> %d", __func__, id, ack);
    return ack;
}
