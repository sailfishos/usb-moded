/**
 * @file usb_moded-configfs.c
 *
 * Copyright (c) 2018 - 2021 Jolla Ltd.
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

#include "usb_moded-configfs.h"

#include "usb_moded-android.h"
#include "usb_moded-common.h"
#include "usb_moded-config-private.h"
#include "usb_moded-log.h"
#include "usb_moded-mac.h"

#include <sys/stat.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* ========================================================================= *
 * Constants
 * ========================================================================= */

/* Due to legacy these defaults must match what is required by Sony XA2 port */
#define DEFAULT_GADGET_BASE_DIRECTORY    "/config/usb_gadget/g1"
#define DEFAULT_GADGET_FUNC_DIRECTORY    "functions"
#define DEFAULT_GADGET_CONF_DIRECTORY    "configs/b.1"

#define DEFAULT_GADGET_CTRL_UDC          "UDC"
#define DEFAULT_GADGET_CTRL_ID_VENDOR    "idVendor"
#define DEFAULT_GADGET_CTRL_ID_PRODUCT   "idProduct"
#define DEFAULT_GADGET_CTRL_MANUFACTURER "strings/0x409/manufacturer"
#define DEFAULT_GADGET_CTRL_PRODUCT      "strings/0x409/product"
#define DEFAULT_GADGET_CTRL_SERIAL       "strings/0x409/serialnumber"

#define DEFAULT_FUNCTION_MASS_STORAGE    "mass_storage.usb0"
#define DEFAULT_FUNCTION_RNDIS           "rndis_bam.rndis"
#define DEFAULT_FUNCTION_MTP             "ffs.mtp"

#define DEFAULT_RNDIS_CTRL_WCEIS         "wceis"
#define DEFAULT_RNDIS_CTRL_ETHADDR       "ethaddr"

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * CONFIGFS
 * ------------------------------------------------------------------------- */

static gchar      *configfs_get_conf               (const char *key, const char *def);
static void        configfs_read_configuration     (void);
static int         configfs_file_type              (const char *path);
static const char *configfs_function_path          (char *buff, size_t size, const char *func, ...);
static const char *configfs_unit_path              (char *buff, size_t size, const char *func, const char *unit);
static const char *configfs_config_path            (char *buff, size_t size, const char *func);
static bool        configfs_mkdir                  (const char *path);
static bool        configfs_rmdir                  (const char *path);
static const char *configfs_register_function      (const char *function);
#ifdef DEAD_CODE
static bool        configfs_unregister_function    (const char *function);
#endif //DEAD_CODE
static const char *configfs_add_unit               (const char *function, const char *unit);
static bool        configfs_remove_unit            (const char *function, const char *unit);
static bool        configfs_enable_function        (const char *function);
static bool        configfs_disable_function       (const char *function);
static bool        configfs_disable_all_functions  (void);
static char       *configfs_strip                  (char *str);
bool               configfs_in_use                 (void);
static bool        configfs_probe                  (void);
static const char *configfs_udc_enable_value       (void);
static bool        configfs_write_file             (const char *path, const char *text);
static bool        configfs_read_file              (const char *path, char *buff, size_t size);
#ifdef DEAD_CODE
static bool        configfs_read_udc               (char *buff, size_t size);
#endif // DEAD_CODE
static bool        configfs_write_udc              (const char *text);
bool               configfs_set_udc                (bool enable);
bool               configfs_init                   (void);
void               configfs_quit                   (void);
bool               configfs_set_charging_mode      (void);
bool               configfs_set_productid          (const char *id);
bool               configfs_set_vendorid           (const char *id);
static const char *configfs_map_function           (const char *func);
bool               configfs_set_function           (const char *functions);
bool               configfs_add_mass_storage_lun   (int lun);
bool               configfs_remove_mass_storage_lun(int lun);
bool               configfs_set_mass_storage_attr  (int lun, const char *attr, const char *value);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static int configfs_probed = -1;

static gchar *GADGET_BASE_DIRECTORY    = 0;
static gchar *GADGET_FUNC_DIRECTORY    = 0;
static gchar *GADGET_CONF_DIRECTORY    = 0;

static gchar *GADGET_CTRL_UDC          = 0;
static gchar *GADGET_CTRL_ID_VENDOR    = 0;
static gchar *GADGET_CTRL_ID_PRODUCT   = 0;
static gchar *GADGET_CTRL_MANUFACTURER = 0;
static gchar *GADGET_CTRL_PRODUCT      = 0;
static gchar *GADGET_CTRL_SERIAL       = 0;

static gchar *FUNCTION_MASS_STORAGE    = 0;
static gchar *FUNCTION_RNDIS           = 0;
static gchar *FUNCTION_MTP             = 0;

static gchar *RNDIS_CTRL_WCEIS         = 0;
static gchar *RNDIS_CTRL_ETHADDR       = 0;

/* ========================================================================= *
 * Settings
 * ========================================================================= */

static gchar *configfs_get_conf(const char *key, const char *def)
{
    LOG_REGISTER_CONTEXT;

    return config_get_conf_string("configfs", key) ?: g_strdup(def);
}

/** Parse configfs configuration entries
 *
 * The defaults correspond with ini-file like (h3113 values):
 *
 * [configfs]
 * gadget_base_directory = /config/usb_gadget/g1
 * gadget_func_directory = functions
 * gadget_conf_directory = configs/b.1
 * function_mass_storage = mass_storage.usb0
 * function_rndis        = rndis_bam.rndis
 * function_mtp          = ffs.mtp
 */
static void configfs_read_configuration(void)
{
    LOG_REGISTER_CONTEXT;

    /* This must be done only once
     */
    static bool done = false;

    if( done )
        goto EXIT;

    done = true;

    gchar *temp_setting;

    /* Gadget directories
     */
    GADGET_BASE_DIRECTORY =
        configfs_get_conf("gadget_base_directory",
                          DEFAULT_GADGET_BASE_DIRECTORY);

    temp_setting = configfs_get_conf("gadget_func_directory",
                             DEFAULT_GADGET_FUNC_DIRECTORY);
    GADGET_FUNC_DIRECTORY = g_strdup_printf("%s/%s",
                                            GADGET_BASE_DIRECTORY,
                                            temp_setting);
    g_free(temp_setting);

    temp_setting = configfs_get_conf("gadget_conf_directory",
                             DEFAULT_GADGET_CONF_DIRECTORY);
    GADGET_CONF_DIRECTORY =
        g_strdup_printf("%s/%s",
                        GADGET_BASE_DIRECTORY,
                        temp_setting);
    g_free(temp_setting);

    /* Gadget control files
     */
    GADGET_CTRL_UDC =
        g_strdup_printf("%s/%s",
                        GADGET_BASE_DIRECTORY,
                        DEFAULT_GADGET_CTRL_UDC);

    GADGET_CTRL_ID_VENDOR =
        g_strdup_printf("%s/%s",
                        GADGET_BASE_DIRECTORY,
                        DEFAULT_GADGET_CTRL_ID_VENDOR);

    GADGET_CTRL_ID_PRODUCT =
        g_strdup_printf("%s/%s",
                        GADGET_BASE_DIRECTORY,
                        DEFAULT_GADGET_CTRL_ID_PRODUCT);

    GADGET_CTRL_MANUFACTURER =
        g_strdup_printf("%s/%s",
                        GADGET_BASE_DIRECTORY,
                        DEFAULT_GADGET_CTRL_MANUFACTURER);

    GADGET_CTRL_PRODUCT =
        g_strdup_printf("%s/%s",
                        GADGET_BASE_DIRECTORY,
                        DEFAULT_GADGET_CTRL_PRODUCT);

    GADGET_CTRL_SERIAL =
        g_strdup_printf("%s/%s",
                        GADGET_BASE_DIRECTORY,
                        DEFAULT_GADGET_CTRL_SERIAL);

    /* Functions
     */
    FUNCTION_MASS_STORAGE =
        configfs_get_conf("function_mass_storage",
                          DEFAULT_FUNCTION_MASS_STORAGE);

    FUNCTION_RNDIS =
        configfs_get_conf("function_rndis",
                          DEFAULT_FUNCTION_RNDIS);

    FUNCTION_MTP =
        configfs_get_conf("function_mtp",
                          DEFAULT_FUNCTION_MTP);

    /* Function control files */
    RNDIS_CTRL_WCEIS =
        g_strdup_printf("%s/%s/%s",
                        GADGET_FUNC_DIRECTORY,
                        FUNCTION_RNDIS,
                        DEFAULT_RNDIS_CTRL_WCEIS);

    RNDIS_CTRL_ETHADDR =
        g_strdup_printf("%s/%s/%s",
                        GADGET_FUNC_DIRECTORY,
                        FUNCTION_RNDIS,
                        DEFAULT_RNDIS_CTRL_ETHADDR);

EXIT:
    return;
}

/* ========================================================================= *
 * Functions
 * ========================================================================= */

static int configfs_file_type(const char *path)
{
    LOG_REGISTER_CONTEXT;

    int type = -1;

    if( !path )
        goto EXIT;

    struct stat st;
    if( lstat(path, &st) == -1 )
        goto EXIT;

    type = st.st_mode & S_IFMT;

EXIT:
    return type;
}

static const char *
configfs_function_path(char *buff, size_t size, const char *func, ...)
{
    LOG_REGISTER_CONTEXT;

    char *pos = buff;
    char *end = buff + size;

    snprintf(pos, end-pos, "%s", GADGET_FUNC_DIRECTORY);

    va_list va;
    va_start(va, func);
    while( func ) {
        pos = strchr(pos, 0);
        snprintf(pos, end-pos, "/%s", func);
        func = va_arg(va, char *);
    }
    va_end(va);

    return buff;
}

static const char *
configfs_unit_path(char *buff, size_t size, const char *func, const char *unit)
{
    LOG_REGISTER_CONTEXT;

    return configfs_function_path(buff, size, func, unit, NULL);
}

static const char *
configfs_config_path(char *buff, size_t size, const char *func)
{
    LOG_REGISTER_CONTEXT;

    snprintf(buff, size, "%s/%s", GADGET_CONF_DIRECTORY, func);
    return buff;
}

static bool
configfs_mkdir(const char *path)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if( mkdir(path, 0775) == -1 && errno != EEXIST ) {
        log_err("%s: mkdir failed: %m", path);
        goto EXIT;
    }

    if( configfs_file_type(path) != S_IFDIR ) {
        log_err("%s: is not a directory", path);
        goto EXIT;
    }

    ack = true;

EXIT:
    return ack;
}

static bool
configfs_rmdir(const char *path)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if( rmdir(path) == -1 && errno != ENOENT ) {
        log_err("%s: rmdir failed: %m", path);
        goto EXIT;
    }

    ack = true;

EXIT:
    return ack;
}

static const char *
configfs_register_function(const char *function)
{
    LOG_REGISTER_CONTEXT;

    const char *res = 0;

    static char fpath[PATH_MAX];
    configfs_function_path(fpath, sizeof fpath, function, NULL);

    if( !configfs_mkdir(fpath) )
        goto EXIT;

    log_debug("function %s is registered", function);

    res = fpath;

EXIT:
    return res;
}

#ifdef DEAD_CODE
static bool
configfs_unregister_function(const char *function)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    char fpath[PATH_MAX];
    configfs_function_path(fpath, sizeof fpath, function, NULL);

    if( !configfs_rmdir(fpath) )
        goto EXIT;

    log_debug("function %s is unregistered", function);
    ack = true;

EXIT:
    return ack;
}
#endif

static const char *
configfs_add_unit(const char *function, const char *unit)
{
    LOG_REGISTER_CONTEXT;

    const char *res = 0;

    static char upath[PATH_MAX];
    configfs_unit_path(upath, sizeof upath, function, unit);

    if( !configfs_mkdir(upath) )
        goto EXIT;

    log_debug("function %s unit %s added", function, unit);

    res = upath;

EXIT:
    return res;
}

static bool
configfs_remove_unit(const char *function, const char *unit)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    static char upath[PATH_MAX];
    configfs_unit_path(upath, sizeof upath, function, unit);

    if( !configfs_rmdir(upath) )
        goto EXIT;

    log_debug("function %s unit %s removed", function, unit);

    ack = true;

EXIT:
    return ack;
}

static bool
configfs_enable_function(const char *function)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    const char *fpath = configfs_register_function(function);
    if( !fpath ) {
        log_err("function %s is not registered", function);
        goto EXIT;
    }

    char cpath[PATH_MAX];
    configfs_config_path(cpath, sizeof cpath, function);

    switch( configfs_file_type(cpath) ) {
    case S_IFLNK:
        if( unlink(cpath) == -1 ) {
            log_err("%s: unlink failed: %m", cpath);
            goto EXIT;
        }
        /* fall through */
    case -1:
        if( symlink(fpath, cpath) == -1 ) {
            log_err("%s: failed to symlink to %s: %m", cpath, fpath);
            goto EXIT;
        }
        break;
    default:
        log_err("%s: is not a symlink", cpath);
        goto EXIT;
    }

    log_debug("function %s is enabled", function);
    ack = true;

EXIT:
    return ack;
}

static bool
configfs_disable_function(const char *function)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    char cpath[PATH_MAX];
    configfs_config_path(cpath, sizeof cpath, function);

    if( configfs_file_type(cpath) != S_IFLNK ) {
        log_err("%s: is not a symlink", cpath);
        goto EXIT;
    }

    if( unlink(cpath) == -1 ) {
        log_err("%s: unlink failed: %m", cpath);
        goto EXIT;
    }

    log_debug("function %s is disabled", function);
    ack = true;

EXIT:
    return ack;
}

static bool
configfs_disable_all_functions(void)
{
    LOG_REGISTER_CONTEXT;

    bool  ack = false;
    DIR  *dir = 0;

    if( !(dir = opendir(GADGET_CONF_DIRECTORY)) ) {
        log_err("%s: opendir failed: %m", GADGET_CONF_DIRECTORY);
        goto EXIT;
    }

    ack = true;

    struct dirent *de;
    while( (de = readdir(dir)) ) {
        if( de->d_type != DT_LNK )
            continue;

        if( !configfs_disable_function(de->d_name) )
            ack = false;
    }

    if( ack )
        log_debug("all functions are disabled");

EXIT:
    if( dir )
        closedir(dir);

    return ack;
}

static char *configfs_strip(char *str)
{
    LOG_REGISTER_CONTEXT;

    unsigned char *src = (unsigned char *)str;
    unsigned char *dst = (unsigned char *)str;

    while( *src > 0 && *src <= 32 ) ++src;

    for( ;; )
    {
        while( *src > 32 ) *dst++ = *src++;
        while( *src > 0 && *src <= 32 ) ++src;
        if( *src == 0 ) break;
        *dst++ = ' ';
    }
    *dst = 0;
    return str;
}

bool
configfs_in_use(void)
{
    LOG_REGISTER_CONTEXT;

    if( configfs_probed < 0 )
        log_debug("configfs_in_use() called before configfs_probe()");
    return configfs_probed > 0;
}

static bool
configfs_probe(void)
{
    LOG_REGISTER_CONTEXT;

    configfs_read_configuration();

    if( configfs_probed <= 0 ) {
        configfs_probed = (access(GADGET_BASE_DIRECTORY, F_OK) == 0 &&
                           access(GADGET_CTRL_UDC, F_OK) == 0);
        log_warning("CONFIGFS %sdetected", configfs_probed ? "" : "not ");
    }
    return configfs_in_use();
}

static const char *
configfs_udc_enable_value(void)
{
    LOG_REGISTER_CONTEXT;

    static bool  probed = false;
    static char *value  = 0;

    if( !probed ) {
        probed = true;

        /* Find first symlink in /sys/class/udc directory */
        struct dirent *de;
        DIR *dir = opendir("/sys/class/udc");
        if( dir ) {
            while( (de = readdir(dir)) ) {
                if( de->d_type != DT_LNK )
                    continue;
                if( de->d_name[0] == '.' )
                    continue;
                value = strdup(de->d_name);
                break;
            }
            closedir(dir);
        }
    }

    return value ?: "";
}

static bool
configfs_write_file(const char *path, const char *text)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;
    int  fd  = -1;

    if( !path || !text )
        goto EXIT;

    log_debug("WRITE %s '%s'", path, text);

    char buff[64];
    snprintf(buff, sizeof buff, "%s\n", text);
    size_t size = strlen(buff);

    if( (fd = open(path, O_WRONLY)) == -1 ) {
        log_err("%s: can't open for writing: %m", path);
        goto EXIT;
    }

    int rc = write(fd, buff, size);
    if( rc == -1 ) {
        log_err("%s: write failure: %m", path);
        goto EXIT;
    }

    if( (size_t)rc != size ) {
        log_err("%s: write failure: partial success", path);
        goto EXIT;
    }

    ack = true;

EXIT:
    if( fd != -1 )
        close(fd);

    return ack;
}

static bool
configfs_read_file(const char *path, char *buff, size_t size)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;
    int  fd  = -1;

    if( !path || !buff )
        goto EXIT;

    if( size < 2 )
        goto EXIT;

    if( (fd = open(path, O_RDONLY)) == -1 ) {
        log_err("%s: can't open for reading: %m", path);
        goto EXIT;
    }

    int rc = read(fd, buff, size - 1);
    if( rc == -1 ) {
        log_err("%s: read failure: %m", path);
        goto EXIT;
    }

    buff[rc] = 0;
    configfs_strip(buff);

    ack = true;

    log_debug("READ %s '%s'", path, buff);

EXIT:
    if( fd != -1 )
        close(fd);

    return ack;
}

#ifdef DEAD_CODE
static bool
configfs_read_udc(char *buff, size_t size)
{
    LOG_REGISTER_CONTEXT;

    return configfs_read_file(GADGET_CTRL_UDC, buff, size);
}
#endif

static bool
configfs_write_udc(const char *text)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    char prev[64];

    if( !configfs_read_file(GADGET_CTRL_UDC, prev, sizeof prev) )
        goto EXIT;

    if( strcmp(prev, text) ) {
        if( !configfs_write_file(GADGET_CTRL_UDC, text) )
            goto EXIT;
    }

    ack = true;

EXIT:
    return ack;

}

bool
configfs_set_udc(bool enable)
{
    LOG_REGISTER_CONTEXT;

    log_debug("UDC - %s", enable ? "ENABLE" : "DISABLE");

    const char *value = "";

    if( enable )
        value = configfs_udc_enable_value();

    return configfs_write_udc(value);
}

/** initialize the basic configfs values
 *
 * @return true if configfs backend is ready for use, false otherwise
 */
bool
configfs_init(void)
{
    LOG_REGISTER_CONTEXT;

    if( !configfs_probe() )
        goto EXIT;

    /* Disable */
    configfs_set_udc(false);

    /* Configure */
    gchar *text;
    if( (text = config_get_android_vendor_id()) ) {
        configfs_write_file(GADGET_CTRL_ID_VENDOR, text);
        g_free(text);
    }

    if( (text = config_get_android_product_id()) ) {
        configfs_write_file(GADGET_CTRL_ID_PRODUCT, text);
        g_free(text);
    }

    if( (text = config_get_android_manufacturer()) ) {
        configfs_write_file(GADGET_CTRL_MANUFACTURER, text);
        g_free(text);
    }

    if( (text = config_get_android_product()) ) {
        configfs_write_file(GADGET_CTRL_PRODUCT, text);
        g_free(text);
    }

    if( (text = android_get_serial()) ) {
        configfs_write_file(GADGET_CTRL_SERIAL, text);
        g_free(text);
    }

    /* Prep: charging_only */
    configfs_register_function(FUNCTION_MASS_STORAGE);

    /* Prep: mtp_mode */
    configfs_register_function(FUNCTION_MTP);

    /* Prep: developer_mode */
    configfs_register_function(FUNCTION_RNDIS);
    if( (text = mac_read_mac()) ) {
        configfs_write_file(RNDIS_CTRL_ETHADDR, text);
        g_free(text);
    }
    /* For rndis to be discovered correctly in M$ Windows (vista and later) */
    configfs_write_file(RNDIS_CTRL_WCEIS, "1");

    /* Leave disabled, will enable on cable connect detected */
EXIT:
    return configfs_in_use();
}

/** Cleanup resources allocated by configfs backend
 */
void
configfs_quit(void)
{
    g_free(GADGET_BASE_DIRECTORY),
        GADGET_BASE_DIRECTORY = 0;
    g_free(GADGET_FUNC_DIRECTORY),
        GADGET_FUNC_DIRECTORY = 0;
    g_free(GADGET_CONF_DIRECTORY),
        GADGET_CONF_DIRECTORY = 0;

    g_free(GADGET_CTRL_UDC),
        GADGET_CTRL_UDC = 0;
    g_free(GADGET_CTRL_ID_VENDOR),
        GADGET_CTRL_ID_VENDOR= 0;
    g_free(GADGET_CTRL_ID_PRODUCT),
        GADGET_CTRL_ID_PRODUCT= 0;
    g_free(GADGET_CTRL_MANUFACTURER),
        GADGET_CTRL_MANUFACTURER= 0;
    g_free(GADGET_CTRL_PRODUCT),
        GADGET_CTRL_PRODUCT = 0;
    g_free(GADGET_CTRL_SERIAL),
        GADGET_CTRL_SERIAL = 0;

    g_free(FUNCTION_MASS_STORAGE),
        FUNCTION_MASS_STORAGE = 0;
    g_free(FUNCTION_RNDIS),
        FUNCTION_RNDIS = 0;
    g_free(FUNCTION_MTP),
        FUNCTION_MTP = 0;

    g_free(RNDIS_CTRL_WCEIS),
        RNDIS_CTRL_WCEIS = 0;
    g_free(RNDIS_CTRL_ETHADDR),
        RNDIS_CTRL_ETHADDR= 0;
}

/* Set a charging mode for the configfs gadget
 *
 * @return true if successful, false on failure
 */
bool
configfs_set_charging_mode(void)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if( !configfs_set_function("mass_storage") )
        goto EXIT;

    /* TODO: make this configurable */
    configfs_set_productid("0AFE");

    if( !configfs_set_udc(true) )
        goto EXIT;

    ack = true;

EXIT:
    log_debug("CONFIGFS %s() -> %d", __func__, ack);
    return ack;
}

/* Set a product id for the configfs gadget
 *
 * @return true if successful, false on failure
 */
bool
configfs_set_productid(const char *id)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if( id && configfs_in_use() ) {
        /* Config files have things like "0A02".
         * Kernel wants to see "0x0a02" ... */
        char *end = 0;
        unsigned num = strtol(id, &end, 16);
        char     str[16];
        if( end > id && *end == 0 ) {
            snprintf(str, sizeof str, "0x%04x", num);
            id = str;
        }
        ack = configfs_write_file(GADGET_CTRL_ID_PRODUCT, id);
    }

    log_debug("CONFIGFS %s(%s) -> %d", __func__, id, ack);
    return ack;
}

/* Set a vendor id for the configfs gadget
 *
 * @return true if successful, false on failure
 */
bool
configfs_set_vendorid(const char *id)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if( id && configfs_in_use() ) {
        log_debug("%s(%s) was called", __func__, id);

        /* Config files have things like "0A02".
         * Kernel wants to see "0x0a02" ... */
        char *end = 0;
        unsigned num = strtol(id, &end, 16);
        char     str[16];

        if( end > id && *end == 0 ) {
            snprintf(str, sizeof str, "0x%04x", num);
            id = str;
        }

        ack = configfs_write_file(GADGET_CTRL_ID_VENDOR, id);
    }

    log_debug("CONFIGFS %s(%s) -> %d", __func__, id, ack);
    return ack;
}

static const char *
configfs_map_function(const char *func)
{
    LOG_REGISTER_CONTEXT;

    if( func == 0 )
        ;
    else if( !strcmp(func, "mass_storage") )
        func = FUNCTION_MASS_STORAGE;
    else if( !strcmp(func, "rndis") )
        func = FUNCTION_RNDIS;
    else if( !strcmp(func, "mtp") )
        func = FUNCTION_MTP;
    else if( !strcmp(func, "ffs") ) // existing config files ...
        func = FUNCTION_MTP;
    return func;
}

/* Set active functions
 *
 * @param function Comma separated list of function names to
 *                 enable, or NULL to disable all
 *
 * @return true if successful, false on failure
 */
bool
configfs_set_function(const char *functions)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    gchar **vec = 0;

    if( !configfs_in_use() )
        goto EXIT;

    if( !configfs_set_udc(false) )
        goto EXIT;

    if( !configfs_disable_all_functions() )
        goto EXIT;

    if( functions ) {
        vec = g_strsplit(functions, ",", 0);
        for( size_t i = 0; vec[i]; ++i ) {
            /* Normalize names used by usb-moded itself and already
             * existing configuration files etc.
             */
            const char *use = configfs_map_function(vec[i]);
            if( !use || !*use )
                continue;
            if( !configfs_enable_function(use) )
                goto EXIT;
        }
    }

    /* Leave disabled, so that caller can adjust attributes
     * etc before enabling */

    ack = true;

EXIT:
    log_debug("CONFIGFS %s(%s) -> %d", __func__, functions, ack);
    g_strfreev(vec);
    return ack;
}

bool
configfs_add_mass_storage_lun(int lun)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if( !configfs_in_use() )
        goto EXIT;

    char unit[32];
    snprintf(unit, sizeof unit, "lun.%d", lun);
    ack = configfs_add_unit(FUNCTION_MASS_STORAGE, unit) != 0;

EXIT:
    return ack;
}

bool
configfs_remove_mass_storage_lun(int lun)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if( !configfs_in_use() )
        goto EXIT;

    char unit[32];
    snprintf(unit, sizeof unit, "lun.%d", lun);
    ack = configfs_remove_unit(FUNCTION_MASS_STORAGE, unit);

EXIT:
    return ack;
}

bool
configfs_set_mass_storage_attr(int lun, const char *attr, const char *value)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if( !configfs_in_use() )
        goto EXIT;

    char unit[32];
    snprintf(unit, sizeof unit, "lun.%d", lun);
    char path[PATH_MAX];
    configfs_function_path(path, sizeof path, FUNCTION_MASS_STORAGE,
                           unit, attr, NULL);
    ack = configfs_write_file(path, value);

EXIT:
    return ack;
}
