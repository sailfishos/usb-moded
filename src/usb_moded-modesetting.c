/**
 * @file usb_moded-modesetting.c
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Copyright (C) 2013-2018 Jolla Ltd.
 *
 * @author: Philippe De Swert <philippe.de-swert@nokia.com>
 * @author: Philippe De Swert <phdeswer@lumi.maa>
 * @author: Philippe De Swert <philippedeswert@gmail.com>
 * @author: Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author: Bernd Wachter <bernd.wachter@jollamobile.com>
 * @author: Slava Monich <slava.monich@jolla.com>
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

#include "usb_moded-modesetting.h"

#include "usb_moded.h"

#include "usb_moded-android.h"
#include "usb_moded-appsync.h"
#include "usb_moded-common.h"
#include "usb_moded-config-private.h"
#include "usb_moded-configfs.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded-log.h"
#include "usb_moded-modules.h"
#include "usb_moded-network.h"
#include "usb_moded-worker.h"

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- modesetting -- */

static void      modesetting_track_value                (const char *path, const char *text);
void             modesetting_verify_values              (void);

static char     *modesetting_strip                      (char *str);
static char     *modesetting_read_from_file             (const char *path, size_t maxsize);
int              modesetting_write_to_file_real         (const char *file, int line, const char *func, const char *path, const char *text);

static gboolean  modesetting_network_retry_cb           (gpointer data);

static bool      modesetting_enter_mass_storage_mode    (mode_list_elem_t *data);
static int       modesetting_leave_mass_storage_mode    (mode_list_elem_t *data);
static void      modesetting_report_mass_storage_blocker(const char *mountpoint, int try);

bool             modesetting_enter_dynamic_mode         (void);
void             modesetting_leave_dynamic_mode         (void);

void             modesetting_init                       (void);
void             modesetting_quit                       (void);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static GHashTable *tracked_values = 0;
static guint modesetting_network_retry_id = 0;

/* ========================================================================= *
 * Functions
 * ========================================================================= */

static void modesetting_track_value(const char *path, const char *text)
{
    if( !tracked_values || !path )
        goto EXIT;

    if( text )
        g_hash_table_replace(tracked_values, g_strdup(path), g_strdup(text));
    else
        g_hash_table_remove(tracked_values, path);

EXIT:
    return;
}

void modesetting_verify_values(void)
{
    GHashTableIter iter;
    gpointer key, value;

    if( !tracked_values )
        goto EXIT;

    g_hash_table_iter_init(&iter, tracked_values);
    while( g_hash_table_iter_next(&iter, &key, &value) )
    {
        const char *path = key;
        const char *text = value;

        char *curr = modesetting_read_from_file(path, 0x1000);

        if( g_strcmp0(text, curr) ) {
            /* There might be case mismatch between hexadecimal
             * values used in configuration files vs what we get
             * back when reading from kernel interfaces. */
            if( text && curr && !g_ascii_strcasecmp(text, curr) ) {
                log_debug("unexpected change '%s' : '%s' -> '%s' (case diff only)", path,
                          text ?: "???",
                          curr ?: "???");
            }
            else {
                log_warning("unexpected change '%s' : '%s' -> '%s'", path,
                            text ?: "???",
                            curr ?: "???");
            }
            modesetting_track_value(path, curr);
        }

        free(curr);
    }

EXIT:
    return;
}

static char *modesetting_strip(char *str)
{
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

static char *modesetting_read_from_file(const char *path, size_t maxsize)
{
    int      fd   = -1;
    ssize_t  done = 0;
    char    *data = 0;
    char    *text = 0;

    if((fd = open(path, O_RDONLY)) == -1)
    {
        /* Silently ignore things that could result
         * from missing / read-only files */
        if( errno != ENOENT && errno != EACCES )
            log_warning("%s: open: %m", path);
        goto cleanup;
    }

    if( !(data = malloc(maxsize + 1)) )
        goto cleanup;

    if((done = read(fd, data, maxsize)) == -1)
    {
        log_warning("%s: read: %m", path);
        goto cleanup;
    }

    text = realloc(data, done + 1), data = 0;
    text[done] = 0;
    modesetting_strip(text);

cleanup:
    free(data);
    if(fd != -1) close(fd);
    return text;
}

int modesetting_write_to_file_real(const char *file, int line, const char *func,
                                   const char *path, const char *text)
{
    int err = -1;
    int fd = -1;
    size_t todo = 0;
    char *prev = 0;
    bool  clear = false;
    gchar *repr = 0;

    /* if either path or the text to be written are not there
     * we return an error */
    if(!text || !path)
        goto cleanup;

    /* When attempting to clear ffs function list, writing an
     * empty string is ignored and accomplishes nothing - while
     * writing non-existing function clears the list but returns
     * write error.
     *
     * Treat "none" (which is used as place-holder value in both
     * configuration files and usb-moded sources) and "" similarly:
     * - Write invalid function name to sysfs
     * - Ignore resulting write error under default logging level
     * - Assume reading from sysfs will result in empty string
     */
    if( !strcmp(path, ANDROID0_FUNCTIONS) ) {
        if( !strcmp(text, "") || !strcmp(text, "none") ) {
            text = "none";
            clear = true;
        }
    }

    repr = g_strdup(text);
    modesetting_strip(repr);

    /* If the file can be read, it also means we can later check that
     * the file retains the value we are about to write here. */
    if( (prev = modesetting_read_from_file(path, 0x1000)) )
        modesetting_track_value(path, clear ? "" : repr);

    log_debug("%s:%d: %s(): WRITE '%s' : '%s' --> '%s'",
              file, line, func,
              path, prev ?: "???", repr);

    todo  = strlen(text);

    /* no O_CREAT -> writes only to already existing files */
    if( (fd = TEMP_FAILURE_RETRY(open(path, O_WRONLY))) == -1 )
    {
        log_warning("open(%s): %m", path);
        goto cleanup;
    }

    while( todo > 0 )
    {
        ssize_t n = TEMP_FAILURE_RETRY(write(fd, text, todo));
        if( n < 0 )
        {
            if( clear && errno == EINVAL )
                log_debug("write(%s): %m (expected failure)", path);
            else
                log_warning("write(%s): %m", path);
            goto cleanup;
        }
        todo -= n;
        text += n;
    }

    err = 0;

cleanup:

    if( fd != -1 ) TEMP_FAILURE_RETRY(close(fd));

    free(prev);
    free(repr);

    return err;
}

static gboolean modesetting_network_retry_cb(gpointer data)
{
    modesetting_network_retry_id = 0;
    network_up(data);
    return(FALSE);
}

#include <mntent.h>

typedef struct storage_info_t
{
    gchar *si_mountpoint;
    gchar *si_mountdevice;;
} storage_info_t;

static void modesetting_free_storage_info(storage_info_t *info);
static storage_info_t * modesetting_get_storage_info(size_t *pcount);

bool  modesetting_is_mounted(const char *mountpoint);
bool  modesetting_mount(const char *mountpoint);
bool  modesetting_unmount(const char *mountpoint);
char *modesetting_mountdev(const char *mountpoint);

bool modesetting_is_mounted(const char *mountpoint)
{
    char cmd[256];
    snprintf(cmd, sizeof cmd, "/bin/mountpoint -q '%s'", mountpoint);
    return common_system(cmd) == 0;
}

bool modesetting_mount(const char *mountpoint)
{
    char cmd[256];
    snprintf(cmd, sizeof cmd, "/bin/mount '%s'", mountpoint);
    return common_system(cmd) == 0;
}

bool modesetting_unmount(const char *mountpoint)
{
    char cmd[256];
    snprintf(cmd, sizeof cmd, "/bin/umount '%s'", mountpoint);
    return common_system(cmd) == 0;
}

gchar *modesetting_mountdev(const char *mountpoint)
{
    char *res = 0;
    FILE *fh  = 0;
    struct mntent *me;

    if( !(fh = setmntent("/etc/fstab", "r")) )
        goto EXIT;

    while( (me = getmntent(fh)) ) {
        if( !strcmp(me->mnt_dir, mountpoint) ) {
            res = g_strdup(me->mnt_fsname);
            break;
        }
    }

EXIT:
    if( fh )
        endmntent(fh);

    log_debug("%s -> %s", mountpoint, res);

    return res;
}

static void
modesetting_free_storage_info(storage_info_t *info)
{
    if( info ) {
        for( size_t i = 0; info[i].si_mountpoint; ++i ) {
            g_free(info[i].si_mountpoint);
            g_free(info[i].si_mountdevice);
        }
        g_free(info);
    }
}

static storage_info_t *
modesetting_get_storage_info(size_t *pcount)
{
    bool             ack     = false;
    storage_info_t  *info    = 0;
    size_t           count   = 0;
    char            *setting = 0;
    gchar          **array   = 0;

    /* Get comma separated mountpoint list from config */
    if( !(setting = config_find_mounts()) ) {
        log_warning("no mount points configuration");
        goto EXIT;
    }

    /* Split mountpoint list to array and count number of items */
    array = g_strsplit(setting, ",", 0);
    while( array[count] )
        ++count;

    if( count < 1 ) {
        log_warning("no mount points configured");
        goto EXIT;
    }

    /* Convert into array of storage_info_t objects */
    info = g_new0(storage_info_t, count + 1);

    for( size_t i = 0; i < count; ++i ) {
        const gchar *mountpnt = info[i].si_mountpoint = g_strdup(array[i]);

        if( access(mountpnt, F_OK) == -1 ) {
            log_warning("mountpoint %s does not exist", mountpnt);
            goto EXIT;
        }

        const gchar *mountdev = info[i].si_mountdevice = modesetting_mountdev(mountpnt);

        if( !mountdev ) {
            log_warning("can't find device for %s", mountpnt);
            goto EXIT;
        }

        if( access(mountdev, F_OK) == -1 ) {
            log_warning("mount device %s does not exist", mountdev);
            goto EXIT;
        }
    }

    ack = true;

EXIT:
    if( !ack )
        modesetting_free_storage_info(info), info = 0, count = 0;

    g_strfreev(array);
    g_free(setting);

    return *pcount = count, info;
}

static bool modesetting_enter_mass_storage_mode(mode_list_elem_t *data)
{
    bool            ack   = false;
    size_t          count = 0;
    storage_info_t *info  = 0;
    int             nofua = 0;

    char tmp[256];

    /* Get mountpoint info */
    if( !(info = modesetting_get_storage_info(&count)) )
        goto EXIT;

    /* send unmount signal so applications can release their grasp on the fs, do this here so they have time to act */
    umdbus_send_state_signal(USB_PRE_UNMOUNT);

    /* Get "No Force Unit Access" from config */
    nofua = config_find_sync();

    /* Android usb mass-storage is expected to support only onle lun */
    if( android_in_use()&& count > 1 ) {
        log_warning("ignoring excess mountpoints");
        count = 1;
    }

    /* Umount filesystems */
    for( size_t i = 0 ; i < count; ++i )
    {
        const gchar *mountpnt = info[i].si_mountpoint;
        for( int tries = 0; ; ) {

            if( !modesetting_is_mounted(mountpnt) ) {
                log_debug("%s is not mounted", mountpnt);
                break;
            }

            if( modesetting_unmount(mountpnt) ) {
                log_debug("unmounted %s", mountpnt);
                break;
            }

            if( ++tries == 3 ) {
                log_err("failed to unmount %s - giving up", mountpnt);
                modesetting_report_mass_storage_blocker(mountpnt, 2);
                umdbus_send_error_signal(UMOUNT_ERROR);
                goto EXIT;
            }

            log_warning("failed to unmount %s - wait a bit", mountpnt);
            modesetting_report_mass_storage_blocker(mountpnt, 1);
            common_sleep(1);
        }
    }

    /* Backend specific actions */
    if( android_in_use() ) {
        const gchar *mountdev = info[0].si_mountdevice;
        android_set_enabled(false);
        android_set_function("mass_storage");
        android_set_attr("f_mass_storage", "lun/nofua", nofua ? "1" : "0");
        android_set_attr("f_mass_storage", "lun/file", mountdev);
        android_set_enabled(true);
    }
    else if( configfs_in_use() ) {
        configfs_set_udc(false);
        configfs_set_function(0);

        for( size_t i = 0 ; i < count; ++i ) {
            const gchar *mountdev = info[i].si_mountdevice;
            if( configfs_add_mass_storage_lun(i) ) {
                configfs_set_mass_storage_attr(i, "cdrom", "0");
                configfs_set_mass_storage_attr(i, "nofua", nofua ? "1" : "0");
                configfs_set_mass_storage_attr(i, "removable", "1");
                configfs_set_mass_storage_attr(i, "ro", "0");
                configfs_set_mass_storage_attr(i, "file", mountdev);
            }
        }
        configfs_set_function("mass_storage");
        configfs_set_udc(true);
    }
    else if( modules_in_use() ) {
        /* check if the file storage module has been loaded with sufficient luns in the parameter,
         * if not, unload and reload or load it. Since  mountpoints start at 0 the amount of them is one more than their id */

        snprintf(tmp, sizeof tmp,
                 "/sys/devices/platform/musb_hdrc/gadget/gadget-lun%zd/file",
                 count - 1);

        if( access(tmp, R_OK) == -1 )
        {
            log_debug("%s does not exist, unloading and reloading mass_storage\n", tmp);
            modules_unload_module(MODULE_MASS_STORAGE);
            snprintf(tmp, sizeof tmp, "modprobe %s luns=%zd \n", MODULE_MASS_STORAGE, count);
            log_debug("usb-load command = %s \n", tmp);
            if( common_system(tmp) != 0 )
                goto EXIT;
        }

        /* activate mounts after sleeping 1s to be sure enumeration happened and autoplay will work in windows*/
        common_sleep(1);

        for( size_t i = 0 ; i < count; ++i ) {
            const gchar *mountdev = info[i].si_mountdevice;

            snprintf(tmp, sizeof tmp, "/sys/devices/platform/musb_hdrc/gadget/gadget-lun%zd/nofua", i);
            write_to_file(tmp, nofua ? "1" : "0");

            snprintf(tmp, sizeof tmp, "/sys/devices/platform/musb_hdrc/gadget/gadget-lun%zd/file", i);
            write_to_file(tmp, mountdev);
            log_debug("usb lun = %s active\n", mountdev);
        }
    }
    else {
        log_err("no suitable backend for mass-storage mode");
        goto EXIT;
    }

    /* Success */
    ack = true;

EXIT:

    modesetting_free_storage_info(info);

    if( ack ) {
        /* only send data in use signal in case we actually succeed */
        umdbus_send_state_signal(DATA_IN_USE);
    }
    else {
        /* Try to undo any unmounts we might have managed to make */
        modesetting_leave_mass_storage_mode(data);
    }

    return ack;
}

static int modesetting_leave_mass_storage_mode(mode_list_elem_t *data)
{
    (void)data;

    bool            ack      = false;
    size_t          count    = 0;
    storage_info_t *info     = 0;
    gchar          *alt_path = 0;

    char tmp[256];

    /* Get mountpoint info */
    if( !(info = modesetting_get_storage_info(&count)) )
        goto EXIT;

    /* Backend specific actions */
    if( android_in_use() ) {
        log_debug("Disable android mass storage\n");
        android_set_enabled(false);
        android_set_attr("f_mass_storage", "lun/file", "");
    }
    else if( configfs_in_use() ) {
        log_debug("Disable configfs mass storage\n");
        configfs_set_udc(false);
        configfs_set_function(0);

        // reset lun0, remove the rest altogether
        for( size_t i = 0 ; i < count; ++i ) {
            // reset
            configfs_set_mass_storage_attr(i, "cdrom", "0");
            configfs_set_mass_storage_attr(i, "nofua", "0");
            configfs_set_mass_storage_attr(i, "removable", "1");
            configfs_set_mass_storage_attr(i, "ro", "0");
            configfs_set_mass_storage_attr(i, "file", "");
            // remove
            if( i > 0 )
                configfs_remove_mass_storage_lun(i);
        }
    }
    else if( modules_in_use() ) {
        for( size_t i = 0 ; i < count; ++i ) {
            snprintf(tmp, sizeof tmp, "/sys/devices/platform/musb_hdrc/gadget/gadget-lun%zd/file", i);
            write_to_file(tmp, "");
            log_debug("usb lun = %s inactive\n", tmp);
        }
    }
    else {
        log_err("no suitable backend for mass-storage mode");
    }

    /* Assume success i.e. all the mountpoints that could have been
     * unmounted due to mass-storage mode are mounted again. */
    ack = true;

    /* Remount filesystems */

    /* TODO FIXME list of mountpoints, but singular alt mountpoint? */
    alt_path = config_find_alt_mount();

    for( size_t i = 0 ; i < count; ++i ) {
        const char *mountpnt = info[i].si_mountpoint;

        if( modesetting_is_mounted(mountpnt) ) {
            log_debug("%s is already mounted", mountpnt);
            continue;
        }

        if( modesetting_mount(mountpnt) ) {
            log_debug("mounted %s", mountpnt);
            continue;
        }

        /* At least one mountpoint could not be restored = failure */
        ack = false;

        if( !alt_path ) {
            log_err("failed to mount %s - no alt mountpoint defined", mountpnt);
        }
        else {
            // TODO what is the point of this all???
            log_err("failed to mount %s - trying ro tmpfs as %s", mountpnt, alt_path);
            snprintf(tmp, sizeof tmp, "mount -t tmpfs tmpfs -o ro --size=512K %s", alt_path);
        }
    }

EXIT:
    modesetting_free_storage_info(info);
    free(alt_path);

    if( !ack )
        umdbus_send_error_signal(RE_MOUNT_FAILED);

    return ack;
}

static void modesetting_report_mass_storage_blocker(const char *mountpoint, int try)
{
    FILE *stream = 0;
    gchar *lsof_command = 0;
    int count = 0;

    lsof_command = g_strconcat("lsof ", mountpoint, NULL);

    if( (stream = common_popen(lsof_command, "r")) )
    {
        char *text = 0;
        size_t size = 0;

        while( getline(&text, &size, stream) >= 0 )
        {
            /* skip the first line as it does not contain process info */
            if(count != 0)
            {
                gchar **split = 0;
                split = g_strsplit((const gchar*)text, " ", 2);
                log_err("Mass storage blocked by process %s\n", split[0]);
                umdbus_send_error_signal(split[0]);
                g_strfreev(split);
            }
            count++;
        }
        pclose(stream);
        free(text);
    }
    g_free(lsof_command);
    if(try == 2)
        log_err("Setting Mass storage blocked. Giving up.\n");

}

bool modesetting_enter_dynamic_mode(void)
{
    bool ack = false;

    mode_list_elem_t *data;
    int network = 1;

    log_debug("DYNAMIC MODE: SETUP");

    /* - - - - - - - - - - - - - - - - - - - *
     * Is a dynamic mode?
     * - - - - - - - - - - - - - - - - - - - */

    if( !(data = worker_get_usb_mode_data()) ) {
        log_debug("No dynamic mode data to setup");
        goto EXIT;
    }

    log_debug("data->mass_storage = %d", data->mass_storage);
    log_debug("data->connman_tethering = %d", data->connman_tethering);
    log_debug("data->appsync = %d", data->appsync);
    log_debug("data->network = %d", data->network);

    /* - - - - - - - - - - - - - - - - - - - *
     * Is a mass storage dynamic mode?
     * - - - - - - - - - - - - - - - - - - - */

    if( data->mass_storage ) {
        log_debug("Dynamic mode is mass storage");
        ack = modesetting_enter_mass_storage_mode(data);
        goto EXIT;
    }

    /* - - - - - - - - - - - - - - - - - - - *
     * Start pre-enum app sync
     * - - - - - - - - - - - - - - - - - - - */

#ifdef APP_SYNC
    if( data->appsync ) {
        log_debug("Dynamic mode is appsync: do pre actions");
        if( appsync_activate_sync(data->mode_name) != 0 ) {
            log_debug("Appsync failure");
            goto EXIT;
        }
    }
#endif

    /* - - - - - - - - - - - - - - - - - - - *
     * Configure gadget
     * - - - - - - - - - - - - - - - - - - - */

    if( configfs_in_use() ) {
        /* Configfs based gadget configuration */
        configfs_set_function(data->sysfs_value);
        configfs_set_productid(data->idProduct);
        char *id = config_get_android_vendor_id();
        configfs_set_vendorid(data->idVendorOverride ?: id);
        free(id);
        if( !configfs_set_udc(true) )
            goto EXIT;
    }
    else if( android_in_use() ) {
        /* Android USB based gadget configuration */
        android_set_function(data->sysfs_value);
        android_set_productid(data->idProduct);
        char *id = config_get_android_vendor_id();
        android_set_vendorid(data->idVendorOverride ?: id);
        free(id);
        write_to_file(data->android_extra_sysfs_path, data->android_extra_sysfs_value);
        write_to_file(data->android_extra_sysfs_path2, data->android_extra_sysfs_value2);
        if( !android_set_enabled(true) )
            goto EXIT;
    }
    else if( modules_in_use() ) {
        /* Assume relevant module has already been successfully loaded
         * from somewhere else.
         */
        // nop
    }
    else {
        log_crit("no backend is selected, can't set dynamic mode");
        goto EXIT;
    }

    /* - - - - - - - - - - - - - - - - - - - *
     * Setup network
     * - - - - - - - - - - - - - - - - - - - */

    /* functionality should be enabled, so we can enable the network now */
    if(data->network)
    {
        log_debug("Dynamic mode is network");
#ifdef DEBIAN
        char command[256];

        g_snprintf(command, 256, "ifdown %s ; ifup %s", data->network_interface, data->network_interface);
        common_system(command);
#else
        network_down(data);
        network = network_up(data);
#endif /* DEBIAN */
    }

    /* try a second time to bring up the network if it failed the first time,
     * this can happen with functionfs based gadgets (which is why we sleep for a bit */
    if(network != 0 && data->network)
    {
        log_debug("Retry setting up the network later\n");
        if(modesetting_network_retry_id)
            g_source_remove(modesetting_network_retry_id);
        modesetting_network_retry_id = g_timeout_add_seconds(3, modesetting_network_retry_cb, data);
    }

    /* Needs to be called before application post synching so
     * that the dhcp server has the right config */
    if(data->nat || data->dhcp_server)
        network_set_up_dhcpd(data);

    /* - - - - - - - - - - - - - - - - - - - *
     * Start post-enum app sync
     * - - - - - - - - - - - - - - - - - - - */

    /* no need to execute the post sync if there was an error setting the mode */
    if(data->appsync )
    {
        log_debug("Dynamic mode is appsync: do post actions");
        /* let's sleep for a bit (350ms) to allow interfaces to settle before running postsync */
        common_msleep(350);
        appsync_activate_sync_post(data->mode_name);
    }

    /* - - - - - - - - - - - - - - - - - - - *
     * Start tethering
     * - - - - - - - - - - - - - - - - - - - */

#ifdef CONNMAN
    if( data->connman_tethering ) {
        log_debug("Dynamic mode is tethering");
        connman_set_tethering(data->connman_tethering, TRUE);
    }
#endif

    ack = true;

EXIT:
    if( !ack )
        umdbus_send_error_signal(MODE_SETTING_FAILED);
    return ack;
}

void modesetting_leave_dynamic_mode(void)
{
    log_debug("DYNAMIC MODE: CLEANUP");

    mode_list_elem_t *data;

    data = worker_get_usb_mode_data();

    /* - - - - - - - - - - - - - - - - - - - *
     * Do not leave timers behind
     * - - - - - - - - - - - - - - - - - - - */

    if(modesetting_network_retry_id)
    {
        g_source_remove(modesetting_network_retry_id);
        modesetting_network_retry_id = 0;
    }

    /* - - - - - - - - - - - - - - - - - - - *
     * Is a dynamic mode?
     * - - - - - - - - - - - - - - - - - - - */
    if( !data ) {
        log_debug("No dynamic mode data to cleanup");
        goto EXIT;
    }

    log_debug("data->mass_storage = %d", data->mass_storage);
    log_debug("data->connman_tethering = %d", data->connman_tethering);
    log_debug("data->appsync = %d", data->appsync);
    log_debug("data->network = %d", data->network);

    /* - - - - - - - - - - - - - - - - - - - *
     * Is a mass storage dynamic mode?
     * - - - - - - - - - - - - - - - - - - - */

    if( data->mass_storage ) {
        log_debug("Dynamic mode is mass storage");
        modesetting_leave_mass_storage_mode(data);
        goto EXIT;
    }

    /* - - - - - - - - - - - - - - - - - - - *
     * Stop tethering
     * - - - - - - - - - - - - - - - - - - - */

#ifdef CONNMAN
    if( data->connman_tethering ) {
        log_debug("Dynamic mode was tethering");
        connman_set_tethering(data->connman_tethering, FALSE);
    }
#endif

    /* - - - - - - - - - - - - - - - - - - - *
     * Stop post-enum app sync
     * - - - - - - - - - - - - - - - - - - - */

    if(data->appsync ) {
        log_debug("Dynamic mode was appsync: undo post actions");
        /* Just stop post enum appsync apps */
        appsync_stop_apps(1);
    }

    /* - - - - - - - - - - - - - - - - - - - *
     * Teardown network
     * - - - - - - - - - - - - - - - - - - - */

    if( data->network ) {
        log_debug("Dynamic mode was network");
        network_down(data);
    }

    /* - - - - - - - - - - - - - - - - - - - *
     * Configure gadget
     * - - - - - - - - - - - - - - - - - - - */

    if( configfs_in_use() ) {
        /* Leave as is. We will reprogram wnen mode is
         * set, not when it is unset.
         */
    }
    else if( android_in_use() ) {
        /* Leave as is. We will reprogram wnen mode is
         * set, not when it is unset.
         */
    }
    else if( modules_in_use() ) {
        /* Assume unloading happens somewhere else */
    }
    else {
        log_crit("no backend is selected, can't unset dynamic mode");
    }

    /* - - - - - - - - - - - - - - - - - - - *
     * Stop pre-enum app sync
     * - - - - - - - - - - - - - - - - - - - */

#ifdef APP_SYNC
    if( data->appsync ) {
        log_debug("Dynamic mode was appsync: undo all actions");
        /* Do full appsync cleanup */
        appsync_stop(false);
    }
#endif

EXIT:
    return;
}

/** Allocate modesetting related dynamic resouces
 */
void modesetting_init(void)
{
    if( !tracked_values ) {
        tracked_values = g_hash_table_new_full(g_str_hash, g_str_equal,
                                               g_free, g_free);
    }
}

/** Release modesetting related dynamic resouces
 */
void modesetting_quit(void)
{
    if( tracked_values ) {
        g_hash_table_unref(tracked_values), tracked_values = 0;
    }
}
