/**
 * @file usb_moded-config.c
 *
 * Copyright (c) 2010 Nokia Corporation. All rights reserved.
 * Copyright (c) 2012 - 2021 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Philippe De Swert <phdeswer@lumi.maa>
 * @author Philippe De Swert <philippedeswert@gmail.com>
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author Reto Zingg <reto.zingg@jollamobile.com>
 * @author Thomas Perl <m@thp.io>
 * @author Slava Monich <slava.monich@jolla.com>
 * @author Martin Jones <martin.jones@jollamobile.com>
 * @author Jarko Poutiainen <jarko.poutiainen@jollamobile.com>
 * @author Simo Piiroinen <simo.piiroinen@jollamobile.com>
 * @author Andrew den Exter <andrew.den.exter@jolla.com>
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

#include "usb_moded-config-private.h"

#include "usb_moded.h"
#include "usb_moded-control.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded-log.h"
#include "usb_moded-modes.h"
#include "usb_moded-worker.h"

#ifdef USE_MER_SSU
# include "usb_moded-ssu.h"
#endif

#include <sys/stat.h>

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <glob.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * CONFIG
 * ------------------------------------------------------------------------- */

static int           config_validate_ip              (const char *ipadd);
char                *config_find_mounts              (void);
int                  config_find_sync                (void);
char                *config_find_alt_mount           (void);
char                *config_check_trigger            (void);
char                *config_get_trigger_subsystem    (void);
char                *config_get_trigger_mode         (void);
char                *config_get_trigger_property     (void);
char                *config_get_trigger_value        (void);
static char         *config_get_network_ip           (void);
static char         *config_get_network_interface    (void);
static char         *config_get_network_gateway      (void);
static char         *config_get_network_netmask      (void);
static char         *config_get_network_nat_interface(void);
static int           config_get_conf_int             (const gchar *entry, const gchar *key);
char                *config_get_conf_string          (const gchar *entry, const gchar *key);
static gchar        *config_make_user_key_string     (const gchar *base_key, uid_t uid);
gchar               *config_get_user_conf_string     (const gchar *entry, const gchar *base_key, uid_t uid);
static char         *config_get_kcmdline_string      (const char *entry);
char                *config_get_mode_setting         (uid_t uid);
set_config_result_t  config_set_config_setting       (const char *entry, const char *key, const char *value);
set_config_result_t  config_set_user_config_setting  (const char *entry, const char *base_key, const char *value, uid_t uid);
set_config_result_t  config_set_mode_setting         (const char *mode, uid_t uid);
static char         *config_make_modes_string        (const char *key, const char *mode_name, int include);
set_config_result_t  config_set_hide_mode_setting    (const char *mode);
set_config_result_t  config_set_unhide_mode_setting  (const char *mode);
set_config_result_t  config_set_mode_whitelist       (const char *whitelist);
set_config_result_t  config_set_mode_in_whitelist    (const char *mode, int allowed);
#ifdef SAILFISH_ACCESS_CONTROL
char                *config_get_group_for_mode       (const char *mode);
#endif
set_config_result_t  config_set_network_setting      (const char *config, const char *setting);
char                *config_get_network_setting      (const char *config);
char                *config_get_network_fallback     (const char *config);
static void          config_merge_key                (GKeyFile *dest, GKeyFile *srce, const char *grp, const char *key);
static void          config_merge_group              (GKeyFile *dest, GKeyFile *srce, const char *grp);
static void          config_merge_data               (GKeyFile *dest, GKeyFile *srce);
static void          config_purge_data               (GKeyFile *dest, GKeyFile *srce);
static void          config_purge_empty_groups       (GKeyFile *dest);
static int           config_glob_error_cb            (const char *path, int err);
static bool          config_merge_from_file          (GKeyFile *ini, const char *path);
static void          config_load_static_config       (GKeyFile *ini);
static bool          config_load_legacy_config       (GKeyFile *ini);
static void          config_remove_legacy_config     (void);
static void          config_load_dynamic_config      (GKeyFile *ini);
static void          config_save_dynamic_config      (GKeyFile *ini);
bool                 config_init                     (void);
static GKeyFile     *config_get_settings             (void);
char                *config_get_android_manufacturer (void);
char                *config_get_android_vendor_id    (void);
char                *config_get_android_product      (void);
char                *config_get_android_product_id   (void);
char                *config_get_hidden_modes         (void);
char                *config_get_mode_whitelist       (void);
int                  config_is_roaming_not_allowed   (void);
bool                 config_user_clear               (uid_t uid);

/* ========================================================================= *
 * Functions
 * ========================================================================= */

static int config_validate_ip(const char *ipadd)
{
    LOG_REGISTER_CONTEXT;

    unsigned int b1, b2, b3, b4;
    unsigned char c;

    if (sscanf(ipadd, "%3u.%3u.%3u.%3u%c", &b1, &b2, &b3, &b4, &c) != 4)
        return -1;

    if ((b1 | b2 | b3 | b4) > 255)
        return -1;
    if (strspn(ipadd, "0123456789.") < strlen(ipadd))
        return -1;
    /* all ok */
    return 0;
}

char *config_find_mounts(void)
{
    LOG_REGISTER_CONTEXT;

    char *ret = NULL;

    ret = config_get_conf_string(FS_MOUNT_ENTRY, FS_MOUNT_KEY);
    if(ret == NULL)
    {
        ret = g_strdup(FS_MOUNT_DEFAULT);
        //log_debug("Default mount = %s\n", ret);
    }
    return ret;
}

int config_find_sync(void)
{
    LOG_REGISTER_CONTEXT;

    return config_get_conf_int(FS_SYNC_ENTRY, FS_SYNC_KEY);
}

char * config_find_alt_mount(void)
{
    LOG_REGISTER_CONTEXT;

    return config_get_conf_string(ALT_MOUNT_ENTRY, ALT_MOUNT_KEY);
}

char * config_check_trigger(void)
{
    LOG_REGISTER_CONTEXT;

    return config_get_conf_string(TRIGGER_ENTRY, TRIGGER_PATH_KEY);
}

char * config_get_trigger_subsystem(void)
{
    LOG_REGISTER_CONTEXT;

    return config_get_conf_string(TRIGGER_ENTRY, TRIGGER_UDEV_SUBSYSTEM);
}

char * config_get_trigger_mode(void)
{
    LOG_REGISTER_CONTEXT;

    return config_get_conf_string(TRIGGER_ENTRY, TRIGGER_MODE_KEY);
}

char * config_get_trigger_property(void)
{
    LOG_REGISTER_CONTEXT;

    return config_get_conf_string(TRIGGER_ENTRY, TRIGGER_PROPERTY_KEY);
}

char * config_get_trigger_value(void)
{
    LOG_REGISTER_CONTEXT;

    return config_get_conf_string(TRIGGER_ENTRY, TRIGGER_PROPERTY_VALUE_KEY);
}

static char * config_get_network_ip(void)
{
    LOG_REGISTER_CONTEXT;

    char * ip = config_get_kcmdline_string(NETWORK_IP_KEY);
    if (ip != NULL) {
        if(!config_validate_ip(ip))
            return ip;
        g_free(ip);
    }

    return config_get_conf_string(NETWORK_ENTRY, NETWORK_IP_KEY);
}

static char * config_get_network_interface(void)
{
    LOG_REGISTER_CONTEXT;

    return config_get_conf_string(NETWORK_ENTRY, NETWORK_INTERFACE_KEY);
}

static char * config_get_network_gateway(void)
{
    LOG_REGISTER_CONTEXT;

    char * gw = config_get_kcmdline_string(NETWORK_GATEWAY_KEY);
    if (gw != NULL)
        return gw;

    return config_get_conf_string(NETWORK_ENTRY, NETWORK_GATEWAY_KEY);
}

static char * config_get_network_netmask(void)
{
    LOG_REGISTER_CONTEXT;

    char * netmask = config_get_kcmdline_string(NETWORK_NETMASK_KEY);
    if (netmask != NULL)
        return netmask;

    return config_get_conf_string(NETWORK_ENTRY, NETWORK_NETMASK_KEY);
}

static char * config_get_network_nat_interface(void)
{
    LOG_REGISTER_CONTEXT;

    return config_get_conf_string(NETWORK_ENTRY, NETWORK_NAT_INTERFACE_KEY);
}

static int config_get_conf_int(const gchar *entry, const gchar *key)
{
    LOG_REGISTER_CONTEXT;

    // TODO: use cached values instead of reloading every time?
    GKeyFile *ini = config_get_settings();
    // Note: zero value is returned if key does not exist
    gint val = g_key_file_get_integer(ini, entry, key, 0);
    g_key_file_free(ini);
    //log_debug("key [%s] %s value is: %d\n", entry, key, val);
    return val;
}

char *config_get_conf_string(const gchar *entry, const gchar *key)
{
    LOG_REGISTER_CONTEXT;

    // TODO: use cached values instead of reloading every time?
    GKeyFile *ini = config_get_settings();
    // Note: null value is returned if key does not exist
    gchar *val = g_key_file_get_string(ini, entry, key, 0);
    g_key_file_free(ini);
    //log_debug("key [%s] %s value is: %s\n", entry, key, val ?: "<null>");
    return val;
}

static gchar *config_make_user_key_string(const gchar *base_key, uid_t uid)
{
    LOG_REGISTER_CONTEXT;

    gchar *key = 0;
#ifdef SAILFISH_ACCESS_CONTROL
    /* If uid is for an additional user, construct a key */
    if( uid >= MIN_ADDITIONAL_USER && uid <= MAX_ADDITIONAL_USER )
        key = g_strdup_printf("%s_%d", base_key, (int)uid);
#endif
    return key;
}

gchar *config_get_user_conf_string(const gchar *entry, const gchar *base_key, uid_t uid)
{
    LOG_REGISTER_CONTEXT;

    gchar *value = 0;
    gchar *key = config_make_user_key_string(base_key, uid);
    if( key )
        value = config_get_conf_string(entry, key);
    /* Fallback to global config if user doesn't have a value set */
    if( !value )
        value = config_get_conf_string(entry, base_key);
    g_free(key);
    return value;
}

static char * config_get_kcmdline_string(const char *entry)
{
    LOG_REGISTER_CONTEXT;

    static const char path[]   = "/proc/cmdline";
    static const char prefix[] = "usb_moded_ip=";

    gchar  *ret   = NULL;
    GError *error = NULL;
    gchar  *data  = NULL;
    gsize   size  = 0;
    gchar **argv  = NULL;
    gint    argc  = 0;
    gchar **array = NULL;
    guint   count = 0;

    if( !g_file_get_contents(path, &data, &size, &error) ) {
        log_warning("could not read %s: %s", path, error->message);
        goto EXIT;
    }

    if( !g_shell_parse_argv(data, &argc, &argv, &error) ) {
        log_warning("could not parse %s: %s", path, error->message);
        goto EXIT;
    }

    // Lookup for optional "usb_moded_ip=v1:v2:..." argument
    const gchar *arg;
    for( gint i = 0; (arg = argv[i]); ++i ) {
        if( !g_ascii_strncasecmp(arg, prefix, sizeof prefix - 1) ) {
            arg += sizeof prefix - 1;
            break;
        }
    }

    // Ignore silently if not found at all / has empty value part
    if( !arg || !*arg )
        goto EXIT;

    // <client-ip>:<server-ip>:<gw-ip>:<netmask>:<hostname>:<device>:<autoconf>
    //      0           1         2        3         4         5        6
    guint expected_count = 7;
    if( (array = g_strsplit(arg, ":", expected_count + 1)) )
        count = g_strv_length(array);

    if( count != expected_count )
        log_warning("Command line arg %s%s has %u fields, expected %u",
                    prefix, arg, count, expected_count);

    gchar *hit = NULL;
    if( !strcmp(entry, NETWORK_IP_KEY) ) {
        if( count >= 1 )
            hit = array[0];
    }
    else if( !strcmp(entry, NETWORK_GATEWAY_KEY) ) {
        if( count >= 3 )
            hit = array[2];
    }
    else if( !strcmp(entry, NETWORK_NETMASK_KEY) ) {
        if( count >= 4 )
            hit = array[3];
    }
    else {
        log_warning("Unknown command line entry %s requested", entry);
    }

    if( !hit ) {
        log_warning("Command line %s = %s", entry, "<undef>");
    }
    else {
        if( *g_strstrip(hit) )
            ret = g_strdup(hit);
        log_debug("Command line %s = %s", entry, ret ?: "<null>");
    }

EXIT:
    g_strfreev(array);
    g_strfreev(argv);
    g_free(data);
    g_clear_error(&error);

    return ret;
}

char * config_get_mode_setting(uid_t uid)
{
    LOG_REGISTER_CONTEXT;

    char *mode = config_get_user_conf_string(MODE_SETTING_ENTRY, MODE_SETTING_KEY, uid);

    /* If no default mode is configured, treat it as charging only */
    if( !mode )
        mode = g_strdup(MODE_CHARGING);
    /* If mode is not allowed, i.e. non-existent or not whitelisted or permitted, use MODE_ASK */
    else if( strcmp(mode, MODE_ASK) && (common_valid_mode(mode) || !usbmoded_is_mode_permitted(mode, uid)) ) {
        log_warning("default mode '%s' is not valid for uid '%d', reset to '%s'",
                    mode, (int)uid, MODE_ASK);
        g_free(mode), mode = g_strdup(MODE_ASK);
        config_set_mode_setting(mode, uid);
    }

    return mode;
}

set_config_result_t config_set_config_setting(const char *entry, const char *key, const char *value)
{
    LOG_REGISTER_CONTEXT;

    set_config_result_t ret = SET_CONFIG_UNCHANGED;

    GKeyFile *static_ini = g_key_file_new();
    GKeyFile *active_ini = g_key_file_new();

    gchar *prev = 0;

    /* Load static configuration */
    config_load_static_config(static_ini);

    /* Merge static and dynamic settings */
    config_merge_data(active_ini, static_ini);
    config_load_dynamic_config(active_ini);

    prev = g_key_file_get_string(active_ini, entry, key, 0);
    if( g_strcmp0(prev, value) ) {
        g_key_file_set_string(active_ini, entry, key, value);
        ret = SET_CONFIG_UPDATED;
        umdbus_send_config_signal(entry, key, value);
    }

    /* Filter out dynamic data that matches static values */
    config_purge_data(active_ini, static_ini);

    /* Update data on filesystem if changed */
    config_save_dynamic_config(active_ini);

    g_free(prev);
    g_key_file_free(active_ini);
    g_key_file_free(static_ini);

    return ret;
}

set_config_result_t config_set_user_config_setting(const char *entry, const char *base_key, const char *value, uid_t uid)
{
    LOG_REGISTER_CONTEXT;

    gchar *key = config_make_user_key_string(base_key, uid);
    set_config_result_t ret = config_set_config_setting(entry, key ?: base_key, value);
    g_free(key);
    return ret;
}

set_config_result_t config_set_mode_setting(const char *mode, uid_t uid)
{
    LOG_REGISTER_CONTEXT;

    /* Don't write values that don't exist */
    if (strcmp(mode, MODE_ASK) && common_valid_mode(mode))
        return SET_CONFIG_ERROR;

    /* Don't write values that are not permitted */
    if (!usbmoded_is_mode_permitted(mode, uid))
        return SET_CONFIG_ERROR;

    return config_set_user_config_setting(MODE_SETTING_ENTRY,
                                          MODE_SETTING_KEY, mode, uid);
}

/* Builds the string used for hidden modes, when hide set to one builds the
 * new string of hidden modes when adding one, otherwise it will remove one */
static char * config_make_modes_string(const char *key, const char *mode_name, int include)
{
    LOG_REGISTER_CONTEXT;

    char     *modes_new = 0;
    char     *modes_old = 0;
    gchar   **modes_arr = 0;
    GString  *modes_tmp = 0;
    int i;

    /* Get current comma separated list of hidden modes */
    modes_old = config_get_conf_string(MODE_SETTING_ENTRY, key);
    if(!modes_old)
    {
        modes_old = g_strdup("");
    }

    modes_arr = g_strsplit(modes_old, ",", 0);

    modes_tmp = g_string_new(NULL);

    for(i = 0; modes_arr[i] != NULL; i++)
    {
        if(strlen(modes_arr[i]) == 0)
        {
            /* Skip any empty strings */
            continue;
        }

        if(!strcmp(modes_arr[i], mode_name))
        {
            /* When unhiding, just skip all matching entries */
            if(!include)
                continue;

            /* When hiding, keep the 1st match and ignore the rest */
            include = 0;
        }

        if(modes_tmp->len > 0)
            modes_tmp = g_string_append(modes_tmp, ",");
        modes_tmp = g_string_append(modes_tmp, modes_arr[i]);
    }

    if(include)
    {
        /* Adding a hidden mode and no matching entry was found */
        if(modes_tmp->len > 0)
            modes_tmp = g_string_append(modes_tmp, ",");
        modes_tmp = g_string_append(modes_tmp, mode_name);
    }

    modes_new = g_string_free(modes_tmp, FALSE), modes_tmp = 0;

    g_strfreev(modes_arr), modes_arr = 0;

    g_free(modes_old), modes_old = 0;

    return modes_new;
}

set_config_result_t config_set_hide_mode_setting(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    set_config_result_t ret = SET_CONFIG_UNCHANGED;

    char *hidden_modes = config_make_modes_string(MODE_HIDE_KEY, mode, 1);

    if( hidden_modes ) {
        ret = config_set_config_setting(MODE_SETTING_ENTRY, MODE_HIDE_KEY, hidden_modes);
    }

    if(ret == SET_CONFIG_UPDATED) {
        common_send_hidden_modes_signal();
        common_send_supported_modes_signal();
        common_send_available_modes_signal();
    }

    g_free(hidden_modes);

    return ret;
}

set_config_result_t config_set_unhide_mode_setting(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    set_config_result_t ret = SET_CONFIG_UNCHANGED;

    char *hidden_modes = config_make_modes_string(MODE_HIDE_KEY, mode, 0);

    if( hidden_modes ) {
        ret = config_set_config_setting(MODE_SETTING_ENTRY, MODE_HIDE_KEY, hidden_modes);
    }

    if(ret == SET_CONFIG_UPDATED) {
        common_send_hidden_modes_signal();
        common_send_supported_modes_signal();
        common_send_available_modes_signal();
    }

    g_free(hidden_modes);

    return ret;
}

set_config_result_t config_set_mode_whitelist(const char *whitelist)
{
    LOG_REGISTER_CONTEXT;

    set_config_result_t ret = config_set_config_setting(MODE_SETTING_ENTRY, MODE_WHITELIST_KEY, whitelist);

    if(ret == SET_CONFIG_UPDATED) {
        uid_t current_user = usbmoded_get_current_user();
        char *mode_setting = config_get_mode_setting(current_user);
        if (strcmp(mode_setting, MODE_ASK) && common_valid_mode(mode_setting))
            config_set_mode_setting(MODE_ASK, current_user);
        g_free(mode_setting);

        control_settings_changed();

        umdbus_send_whitelisted_modes_signal(whitelist);
        common_send_available_modes_signal();
    }

    return ret;
}

set_config_result_t config_set_mode_in_whitelist(const char *mode, int allowed)
{
    LOG_REGISTER_CONTEXT;

    set_config_result_t ret = SET_CONFIG_UNCHANGED;

    char *whitelist = config_make_modes_string(MODE_WHITELIST_KEY, mode, allowed);

    ret = config_set_mode_whitelist(whitelist ?: "");

    g_free(whitelist);

    return ret;
}

#ifdef SAILFISH_ACCESS_CONTROL
char *config_get_group_for_mode(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    char *group = config_get_conf_string(MODE_GROUP_ENTRY, mode);

    if (group == NULL)
        group = g_strdup("sailfish-system");

    return group;
}
#endif

/*
 * @param config : the key to be set
 * @param setting : The value to be set
 */
set_config_result_t config_set_network_setting(const char *config, const char *setting)
{
    LOG_REGISTER_CONTEXT;

    if(!strcmp(config, NETWORK_IP_KEY) || !strcmp(config, NETWORK_GATEWAY_KEY))
        if(config_validate_ip(setting) != 0)
            return SET_CONFIG_ERROR;

    if(!strcmp(config, NETWORK_IP_KEY) || !strcmp(config, NETWORK_INTERFACE_KEY) || !strcmp(config, NETWORK_GATEWAY_KEY))
    {
        return config_set_config_setting(NETWORK_ENTRY, config, setting);
    }

    return SET_CONFIG_ERROR;
}

/** Get network setting value
 *
 * @param config  setting key
 *
 * @return setting value string, or NULL
 */
char *config_get_network_setting(const char *config)
{
    LOG_REGISTER_CONTEXT;

    char *ret = 0;

    if( !g_strcmp0(config, NETWORK_IP_KEY) )
        ret = config_get_network_ip();
    else if( !g_strcmp0(config, NETWORK_INTERFACE_KEY))
        ret = config_get_network_interface();
    else if( !g_strcmp0(config, NETWORK_GATEWAY_KEY) )
        ret = config_get_network_gateway();
    else if( !g_strcmp0(config, NETWORK_NETMASK_KEY) )
        ret = config_get_network_netmask();
    else if( !g_strcmp0(config, NETWORK_NAT_INTERFACE_KEY) )
        ret = config_get_network_nat_interface();
    else
        log_warning("unknown network setting '%s' queried", config);

    return ret;
}

/** Get network setting fallback value
 *
 * @param config  setting key
 *
 * @return setting value string, or NULL
 */
char *config_get_network_fallback(const char *config)
{
    LOG_REGISTER_CONTEXT;

    char *ret = 0;

    if( !g_strcmp0(config, NETWORK_IP_KEY) )
        ret = g_strdup(NETWORK_IP_FALLBACK);
    else if( !g_strcmp0(config, NETWORK_INTERFACE_KEY))
        ret = g_strdup(NETWORK_INTERFACE_FALLBACK);
    else if( !g_strcmp0(config, NETWORK_GATEWAY_KEY) )
        ret = g_strdup(NETWORK_GATEWAY_FALLBACK);
    else if( !g_strcmp0(config, NETWORK_NETMASK_KEY) )
        ret = g_strdup(NETWORK_NETMASK_FALLBACK);
    else if( !g_strcmp0(config, NETWORK_NAT_INTERFACE_KEY) )
        ret = g_strdup(NETWORK_NAT_INTERFACE_FALLBACK);
    else
        log_warning("unknown network fallback '%s' queried", config);

    return ret;
}

/**
 * Merge value from one keyfile to another
 *
 * Existing values will be overridden
 *
 * @param dest keyfile to modify
 * @param srce keyfile to merge from
 * @param grp  value group to merge
 * @param key  value key to merge
 */
static void config_merge_key(GKeyFile *dest, GKeyFile *srce,
                             const char *grp, const char *key)
{
    LOG_REGISTER_CONTEXT;

    gchar *val = g_key_file_get_value(srce, grp, key, 0);
    if( val ) {
        //log_debug("[%s] %s = %s", grp, key, val);
        g_key_file_set_value(dest, grp, key, val);
        g_free(val);
    }
}

/**
 * Merge group of values from one keyfile to another
 *
 * @param dest keyfile to modify
 * @param srce keyfile to merge from
 * @param grp  value group to merge
 */
static void config_merge_group(GKeyFile *dest, GKeyFile *srce,
                               const char *grp)
{
    LOG_REGISTER_CONTEXT;

    gchar **key = g_key_file_get_keys(srce, grp, 0, 0);
    if( key ) {
        for( size_t i = 0; key[i]; ++i )
            config_merge_key(dest, srce, grp, key[i]);
        g_strfreev(key);
    }
}

/**
 * Merge all groups and values from one keyfile to another
 *
 * @param dest keyfile to modify
 * @param srce keyfile to merge from
 */
static void config_merge_data(GKeyFile *dest, GKeyFile *srce)
{
    LOG_REGISTER_CONTEXT;

    gchar **grp = g_key_file_get_groups(srce, 0);

    if( grp ) {
        for( size_t i = 0; grp[i]; ++i )
            config_merge_group(dest, srce, grp[i]);
        g_strfreev(grp);
    }
}

static void config_purge_data(GKeyFile *dest, GKeyFile *srce)
{
    LOG_REGISTER_CONTEXT;

    gsize groups = 0;
    gchar **group = g_key_file_get_groups(srce, &groups);
    for( gsize g = 0; g < groups; ++g ) {
        gsize keys = 0;
        gchar **key = g_key_file_get_keys(srce, group[g], &keys, 0);
        for( gsize k = 0; k < keys; ++k ) {
            gchar *cur_val = g_key_file_get_value(dest, group[g], key[k], 0);
            if( !cur_val )
                continue;

            gchar *def_val = g_key_file_get_value(srce, group[g], key[k], 0);

            if( !g_strcmp0(cur_val, def_val) ) {
                log_debug("purge redundant: [%s] %s = %s",
                          group[g], key[k], cur_val);
                g_key_file_remove_key(dest, group[g], key[k], 0);
            }
            g_free(def_val);
            g_free(cur_val);
        }
        g_strfreev(key);
    }
    g_strfreev(group);
}

static void config_purge_empty_groups(GKeyFile *dest)
{
    LOG_REGISTER_CONTEXT;

    gsize groups = 0;
    gchar **group = g_key_file_get_groups(dest, &groups);
    for( gsize g = 0; g < groups; ++g ) {
        gsize keys = 0;
        gchar **key = g_key_file_get_keys(dest, group[g], &keys, 0);
        if( keys == 0 ) {
            log_debug("purge redundant group: [%s]", group[g]);
            g_key_file_remove_group(dest, group[g], 0);
        }
        g_strfreev(key);
    }
    g_strfreev(group);
}

/**
 * Callback function for logging errors within glob()
 *
 * @param path path to file/dir where error occurred
 * @param err  errno that occurred
 *
 * @return 0 (= do not stop glob)
 */
static int config_glob_error_cb(const char *path, int err)
{
    LOG_REGISTER_CONTEXT;

    log_debug("%s: glob: %s", path, g_strerror(err));
    return 0;
}

static bool config_merge_from_file(GKeyFile *ini, const char *path)
{
    LOG_REGISTER_CONTEXT;

    bool      ack = false;
    GError   *err = 0;
    GKeyFile *tmp = g_key_file_new();

    if( !g_key_file_load_from_file(tmp, path, 0, &err) ) {
        log_debug("%s: can't load: %s", path, err->message);
    } else {
        //log_debug("processing %s ...", path);
        config_merge_data(ini, tmp);
        ack = true;
    }
    g_clear_error(&err);
    g_key_file_free(tmp);
    return ack;
}

static void config_load_static_config(GKeyFile *ini)
{
    LOG_REGISTER_CONTEXT;

    static const char pattern[] = USB_MODED_STATIC_CONFIG_DIR"/*.ini";

    glob_t gb = {};

    if( glob(pattern, 0, config_glob_error_cb, &gb) != 0 )
        log_debug("no configuration ini-files found");

    /* Seed with default values */
    g_key_file_set_string(ini, MODE_SETTING_ENTRY, MODE_SETTING_KEY, MODE_ASK);

    /* Override with content from config files */
    for( size_t i = 0; i < gb.gl_pathc; ++i ) {
        const char *path = gb.gl_pathv[i];
        if( strcmp(path, USB_MODED_STATIC_CONFIG_FILE) )
            config_merge_from_file(ini, path);
    }

    globfree(&gb);
}

static bool config_load_legacy_config(GKeyFile *ini)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    /* If the legacy config file does not exist, there is
     * no need to do anything about it.
     */
    if( access(USB_MODED_STATIC_CONFIG_FILE, F_OK) == -1 )
        goto EXIT;

    /* If we have also dynamic settings file, it means that
     * the legacy config file has re-appeared after migration.
     *
     * This could happen e.g. during sw upgrades as long as
     * there are packages that contain the legacy config file.
     *
     * The content must be ignored to avoid overriding user
     * settings, but emit a warning to help diagnosing when
     * and why it might be happening (the legacy file will be
     * removed later on - when there are settings changes to
     * commit).
     */
    if( access(USB_MODED_DYNAMIC_CONFIG_FILE, F_OK) == 0 ) {
        log_warning("%s: has reappeared after settings migration",
                    USB_MODED_STATIC_CONFIG_FILE);
        goto EXIT;
    }

    if( !config_merge_from_file(ini, USB_MODED_STATIC_CONFIG_FILE) )
        goto EXIT;

    /* A mode=ask setting in legacy config can be either
     * something user has selected, or merely configured
     * default. As the latter case interferes with evaluation
     * of priority ordered static configuration files, ignore
     * such settings.
     */
    gchar *val = g_key_file_get_value(ini, MODE_SETTING_ENTRY,
                                      MODE_SETTING_KEY, 0);
    if( val ) {
        if( !g_strcmp0(val, MODE_ASK) ) {
            g_key_file_remove_key(ini, MODE_SETTING_ENTRY,
                                  MODE_SETTING_KEY, 0);
        }
        g_free(val);
    }

    ack = true;

EXIT:
    return ack;
}

static void config_remove_legacy_config(void)
{
    LOG_REGISTER_CONTEXT;

    /* Note: In case of read-only /tmp, unlink attempt leads to
     *       EROFS regardless of whether the file exists or not
     *       -> do a separate existance check 1st.
     */

    if( access(USB_MODED_STATIC_CONFIG_FILE, F_OK) == -1 && errno == ENOENT ) {
        /* nop */
    }
    else if( unlink(USB_MODED_STATIC_CONFIG_FILE) == -1 && errno != ENOENT ) {
        log_warning("%s: can't remove stale config file: %m",
                    USB_MODED_STATIC_CONFIG_FILE);
    }
}

static void config_load_dynamic_config(GKeyFile *ini)
{
    LOG_REGISTER_CONTEXT;

    config_merge_from_file(ini, USB_MODED_DYNAMIC_CONFIG_FILE);
}

static void config_save_dynamic_config(GKeyFile *ini)
{
    LOG_REGISTER_CONTEXT;

    gchar  *current_dta = 0;
    gchar  *previous_dta = 0;

    config_purge_empty_groups(ini);
    current_dta = g_key_file_to_data(ini, 0, 0);

    g_file_get_contents(USB_MODED_DYNAMIC_CONFIG_FILE, &previous_dta, 0, 0);
    if( g_strcmp0(previous_dta, current_dta) ) {
        GError *err = 0;
        if( mkdir(USB_MODED_DYNAMIC_CONFIG_DIR, 0755) == -1 && errno != EEXIST ) {
            log_err("%s: can't create dir: %m", USB_MODED_DYNAMIC_CONFIG_DIR);
        }
        else if( !g_file_set_contents(USB_MODED_DYNAMIC_CONFIG_FILE,
                                      current_dta, -1, &err) ) {
            log_err("%s: can't save: %s", USB_MODED_DYNAMIC_CONFIG_FILE,
                    err->message);
        }
        else {
            log_debug("%s: updated", USB_MODED_DYNAMIC_CONFIG_FILE);

            /* The legacy file is not needed anymore */
            config_remove_legacy_config();
        }
        g_clear_error(&err);
    }

    g_free(current_dta);
    g_free(previous_dta);
}

/**
 * Read the *.ini files and create/overwrite USB_MODED_STATIC_CONFIG_FILE with
 * the merged data.
 *
 * @return 0 on failure
 */
bool config_init(void)
{
    LOG_REGISTER_CONTEXT;

    bool      ack = true;

    GKeyFile *legacy_ini = g_key_file_new();
    GKeyFile *static_ini = g_key_file_new();
    GKeyFile *active_ini = g_key_file_new();

    /* Load static configuration */
    config_load_static_config(static_ini);

    /* Handle legacy settings */
    if( config_load_legacy_config(legacy_ini) ) {
        config_purge_data(legacy_ini, static_ini);
        config_merge_data(active_ini, legacy_ini);
    }

    /* Load dynamic settings */
    config_load_dynamic_config(active_ini);

    /* Filter out dynamic data that matches static values */
    config_purge_data(active_ini, static_ini);

    /* Update data on filesystem if changed */
    config_save_dynamic_config(active_ini);

    g_key_file_free(active_ini);
    g_key_file_free(static_ini);
    g_key_file_free(legacy_ini);

    return ack;
}

static GKeyFile *config_get_settings(void)
{
    LOG_REGISTER_CONTEXT;

    GKeyFile *ini = g_key_file_new();
    config_load_static_config(ini);
    config_load_dynamic_config(ini);
    return ini;
}

char * config_get_android_manufacturer(void)
{
    LOG_REGISTER_CONTEXT;

#ifdef USE_MER_SSU
    /* If SSU can provide manufacturer name, use it. Otherwise fall
     * back to using the name specified in configuration files. */
    char *ssu_name = ssu_get_manufacturer_name();
    if( ssu_name )
    {
        return ssu_name;
    }
#endif

    return config_get_conf_string(ANDROID_ENTRY, ANDROID_MANUFACTURER_KEY);
}

char * config_get_android_vendor_id(void)
{
    LOG_REGISTER_CONTEXT;

    return config_get_conf_string(ANDROID_ENTRY, ANDROID_VENDOR_ID_KEY);
}

char * config_get_android_product(void)
{
    LOG_REGISTER_CONTEXT;

#ifdef USE_MER_SSU
    /* If SSU can provide device model name, use it. Otherwise fall
     * back to using the name specified in configuration files. */
    char *ssu_name = ssu_get_product_name();
    if( ssu_name )
    {
        return ssu_name;
    }
#endif

    return config_get_conf_string(ANDROID_ENTRY, ANDROID_PRODUCT_KEY);
}

char * config_get_android_product_id(void)
{
    LOG_REGISTER_CONTEXT;

    return config_get_conf_string(ANDROID_ENTRY, ANDROID_PRODUCT_ID_KEY);
}

char * config_get_hidden_modes(void)
{
    LOG_REGISTER_CONTEXT;

    return config_get_conf_string(MODE_SETTING_ENTRY, MODE_HIDE_KEY);
}
char * config_get_mode_whitelist(void)
{
    LOG_REGISTER_CONTEXT;

    return config_get_conf_string(MODE_SETTING_ENTRY, MODE_WHITELIST_KEY);
}

int config_is_roaming_not_allowed(void)
{
    LOG_REGISTER_CONTEXT;

    return config_get_conf_int(NETWORK_ENTRY, NO_ROAMING_KEY);
}

/**
 * Remove user configs
 */
bool config_user_clear(uid_t uid)
{
#ifdef SAILFISH_ACCESS_CONTROL
    if (uid < MIN_ADDITIONAL_USER || uid > MAX_ADDITIONAL_USER) {
        log_err("Invalid uid value: %d\n", uid);
        return false;
    }
#endif

    GKeyFile *active_ini = g_key_file_new();
    config_load_dynamic_config(active_ini);

    char *key = config_make_user_key_string(MODE_SETTING_KEY, uid);
    if (key) {
        if (g_key_file_remove_key(active_ini, MODE_SETTING_ENTRY, key, NULL))
            config_save_dynamic_config(active_ini);
        g_free(key);
    }

    g_key_file_free(active_ini);
    return true;
}
