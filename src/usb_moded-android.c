/**
 * @file usb_moded-android.c
 *
 * Copyright (c) 2013 - 2021 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
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

#include "usb_moded-android.h"

#include "usb_moded-config-private.h"
#include "usb_moded-log.h"
#include "usb_moded-mac.h"
#include "usb_moded-modesetting.h"

#include <unistd.h>
#include <stdio.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * ANDROID
 * ------------------------------------------------------------------------- */

static bool  android_write_file       (const char *path, const char *text);
bool         android_in_use           (void);
static bool  android_probe            (void);
gchar       *android_get_serial       (void);
bool         android_init             (void);
void         android_quit             (void);
bool         android_set_enabled      (bool enable);
bool         android_set_charging_mode(void);
bool         android_set_function     (const char *function);
bool         android_set_productid    (const char *id);
bool         android_set_vendorid     (const char *id);
bool         android_set_attr         (const char *function, const char *attr, const char *value);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static int android_probed = -1;

/* ========================================================================= *
 * Functions
 * ========================================================================= */

static bool
android_write_file(const char *path, const char *text)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if( !path || !text )
        goto EXIT;

    log_debug("WRITE %s '%s'", path, text);

    char buff[64];
    snprintf(buff, sizeof buff, "%s\n", text);

    if( write_to_file(path, buff) == -1 )
        goto EXIT;

    ack = true;

EXIT:

    return ack;
}

bool
android_in_use(void)
{
    LOG_REGISTER_CONTEXT;

    if( android_probed < 0 )
        log_debug("android_in_use() called before android_probe()");

    return android_probed > 0;
}

static bool
android_probe(void)
{
    LOG_REGISTER_CONTEXT;

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
    LOG_REGISTER_CONTEXT;

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
 *
 * @return true if android usb backend is ready for use, false otherwise
 */
bool
android_init(void)
{
    LOG_REGISTER_CONTEXT;

    gchar *text;

    if( !android_probe() )
        goto EXIT;

    /* Disable */
    android_set_enabled(false);

    /* Configure */
    if( (text = android_get_serial()) )
    {
        android_write_file(ANDROID0_SERIAL, text);
        g_free(text);
    }

    text = config_get_android_manufacturer();
    if(text)
    {
        android_write_file(ANDROID0_MANUFACTURER, text);
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
        android_write_file(ANDROID0_PRODUCT, text);
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
        android_set_attr("f_rndis", "ethaddr", text);
        g_free(text);
    }
    /* For rndis to be discovered correctly in M$ Windows (vista and later) */
    android_set_attr("f_rndis", "wceis", "1");

    /* Make sure remnants off mass-storage mode do not cause
     * issues for charging_fallback & co */
    android_set_attr("f_mass_storage", "lun/nofua", "0");
    android_set_attr("f_mass_storage", "lun/file", "");

EXIT:
    return android_in_use();
}

/** Cleanup resources allocated by android usb backend
 */
void
android_quit(void)
{
    /* For now this exists for symmetry with other backends only */
}

bool
android_set_enabled(bool enable)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;
    if( android_in_use() ) {
        const char *val = enable ? "1" : "0";
        ack = android_write_file(ANDROID0_ENABLE, val);
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
    LOG_REGISTER_CONTEXT;

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
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if( !function )
        goto EXIT;

    if( !android_in_use() )
        goto EXIT;

    if( !android_set_enabled(false) )
        goto EXIT;

    if( !android_write_file(ANDROID0_FUNCTIONS, function) )
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
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if( id && android_in_use() ) {
        char str[16];
        char *end = 0;
        unsigned num = strtol(id, &end, 16);
        if( end > id && *end == 0 ) {
            snprintf(str, sizeof str, "%04x", num);
            id = str;
        }
        ack = android_write_file(ANDROID0_ID_PRODUCT, id);
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
    LOG_REGISTER_CONTEXT;

    bool ack = false;
    if( id && android_in_use() ) {
        char str[16];
        char *end = 0;
        unsigned num = strtol(id, &end, 16);
        if( end > id && *end == 0 ) {
            snprintf(str, sizeof str, "%04x", num);
            id = str;
        }
        ack = android_write_file(ANDROID0_ID_VENDOR, id);
    }
    log_debug("ANDROID %s(%s) -> %d", __func__, id, ack);
    return ack;
}

/** Set function attribute
 *
 * @return true if successful, false on failure
 */
bool
android_set_attr(const char *function, const char *attr, const char *value)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if( function && attr && value && android_in_use() ) {
        char path[256];
        snprintf(path, sizeof path, "%s/%s/%s",
                 ANDROID0_DIRECTORY, function, attr);
        ack = android_write_file(path, value);
    }
    log_debug("ANDROID %s(%s, %s, %s) -> %d", __func__,
              function, attr, value, ack);
    return ack;
}
