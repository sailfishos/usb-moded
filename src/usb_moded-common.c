#include "usb_moded-common.h"

#include "usb_moded.h"
#include "usb_moded-config-private.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded-dyn-config.h"
#include "usb_moded-log.h"
#include "usb_moded-modes.h"
#include "usb_moded-worker.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* ========================================================================= *
 * Types
 * ========================================================================= */

/** Mapping usb mode from internal to hardware/broadcast use */
typedef struct modemapping_t
{
    /** Any valid usb mode */
    const char *internal_mode;

    /** Mode to use for usb configuration, or NULL = internal */
    const char *hardware_mode;

    /** Mode to use for D-Bus broadcast, or NULL = internal */
    const char *external_mode;
} modemapping_t;

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- cable -- */

const char *cable_state_repr(cable_state_t state);

/* -- common -- */

const char  *common_map_mode_to_hardware         (const char *internal_mode);
const char  *common_map_mode_to_external         (const char *internal_mode);
void         common_send_supported_modes_signal  (void);
void         common_send_available_modes_signal  (void);
void         common_send_hidden_modes_signal     (void);
void         common_send_whitelisted_modes_signal(void);
static void  common_write_to_sysfs_file          (const char *path, const char *text);
void         common_acquire_wakelock             (const char *wakelock_name);
void         common_release_wakelock             (const char *wakelock_name);
int          common_system_                      (const char *file, int line, const char *func, const char *command);
FILE        *common_popen_                       (const char *file, int line, const char *func, const char *command, const char *type);
void         common_usleep_                      (const char *file, int line, const char *func, useconds_t usec);
static bool  common_mode_in_list                 (const char *mode, char *const *modes);
int          common_valid_mode                   (const char *mode);
gchar       *common_get_mode_list                (mode_list_type_t type);

/* ========================================================================= *
 * Functions
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * CABLE_STATE
 * ------------------------------------------------------------------------- */

const char *cable_state_repr(cable_state_t state)
{
    static const char * const lut[CABLE_STATE_NUMOF] = {
        [CABLE_STATE_UNKNOWN]           = "unknown",
        [CABLE_STATE_DISCONNECTED]      = "disconnected",
        [CABLE_STATE_CHARGER_CONNECTED] = "charger_connected",
        [CABLE_STATE_PC_CONNECTED]      = "pc_connected",
    };
    return lut[state];
}

/* ------------------------------------------------------------------------- *
 * MODE_MAPPING
 * ------------------------------------------------------------------------- */

static const modemapping_t common_modemapping[] =
{
    {
        .internal_mode = MODE_UNDEFINED,
        .hardware_mode = MODE_CHARGING,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_ASK,
        .hardware_mode = MODE_CHARGING,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_MASS_STORAGE,
        .hardware_mode = 0,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_DEVELOPER,
        .hardware_mode = 0,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_MTP,
        .hardware_mode = 0,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_HOST,
        .hardware_mode = 0,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_CONNECTION_SHARING,
        .hardware_mode = 0,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_DIAG,
        .hardware_mode = 0,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_ADB,
        .hardware_mode = 0,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_PC_SUITE,
        .hardware_mode = 0,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_CHARGING,
        .hardware_mode = MODE_CHARGING,
        .external_mode = 0,
    },
    {
        .internal_mode = MODE_CHARGING_FALLBACK,
        .hardware_mode = MODE_CHARGING,
        .external_mode = MODE_ASK,
    },
    {
        .internal_mode = MODE_CHARGER,
        .hardware_mode = MODE_CHARGING,
        .external_mode = 0,
    },
    // sentinel
    {
        .internal_mode = 0,
        .hardware_mode = 0,
        .external_mode = 0,
    }
};

const char *
common_map_mode_to_hardware(const char *internal_mode)
{
    const char *hardware_mode = 0;

    for( size_t i = 0; common_modemapping[i].internal_mode; ++i ) {
        if( strcmp(common_modemapping[i].internal_mode, internal_mode) )
            continue;
        hardware_mode = common_modemapping[i].hardware_mode;
        break;
    }
    return hardware_mode ?: internal_mode;
}

const char *
common_map_mode_to_external(const char *internal_mode)
{
    const char *external_mode = 0;

    for( size_t i = 0; common_modemapping[i].internal_mode; ++i ) {
        if( strcmp(common_modemapping[i].internal_mode, internal_mode) )
            continue;
        external_mode = common_modemapping[i].external_mode;
        break;
    }
    return external_mode ?: internal_mode;
}

/* ------------------------------------------------------------------------- *
 * DBUS_NOTIFICATIONS
 * ------------------------------------------------------------------------- */

/** Send supported modes signal
 */
void common_send_supported_modes_signal(void)
{
    gchar *mode_list = common_get_mode_list(SUPPORTED_MODES_LIST);
    umdbus_send_supported_modes_signal(mode_list);
    g_free(mode_list);
}

/** Send available modes signal
 */
void common_send_available_modes_signal(void)
{
    gchar *mode_list = common_get_mode_list(AVAILABLE_MODES_LIST);
    umdbus_send_available_modes_signal(mode_list);
    g_free(mode_list);
}

/** Send hidden modes signal
 */
void common_send_hidden_modes_signal(void)
{
    gchar *mode_list = config_get_hidden_modes();
    if(mode_list) {
        // TODO: cleared list not signaled?
        umdbus_send_hidden_modes_signal(mode_list);
        g_free(mode_list);
    }
}

/** Send whitelisted modes signal
 */
void common_send_whitelisted_modes_signal(void)
{
    gchar *mode_list = config_get_mode_whitelist();
    if(mode_list) {
        // TODO: cleared list not signaled?
        umdbus_send_whitelisted_modes_signal(mode_list);
        g_free(mode_list);
    }
}

/* ------------------------------------------------------------------------- *
 * SYSFS_IO
 * ------------------------------------------------------------------------- */

/** Write string to already existing sysfs file
 *
 * Note: Attempts to write to nonexisting files are silently ignored.
 *
 * @param path Where to write
 * @param text What to write
 */
static void common_write_to_sysfs_file(const char *path, const char *text)
{
    int fd = -1;

    if (!path || !text)
        goto EXIT;

    if ((fd = open(path, O_WRONLY)) == -1) {
        if (errno != ENOENT) {
            log_warning("%s: open for writing failed: %m", path);
        }
        goto EXIT;
    }

    if (write(fd, text, strlen(text)) == -1) {
        log_warning("%s: write failed : %m", path);
        goto EXIT;
    }
EXIT:
    if (fd != -1)
        close(fd);
}

/* ------------------------------------------------------------------------- *
 * WAKELOCKS
 * ------------------------------------------------------------------------- */

/** Acquire wakelock via sysfs
 *
 * Wakelock must be released via common_release_wakelock().
 *
 * Automatically terminating wakelock is used, so that we
 * do not block suspend  indefinately in case usb_moded
 * gets stuck or crashes.
 *
 * Note: The name should be unique within the system.
 *
 * @param wakelock_name Wake lock to be acquired
 */
void common_acquire_wakelock(const char *wakelock_name)
{
    char buff[256];
    snprintf(buff, sizeof buff, "%s %lld",
             wakelock_name,
             USB_MODED_SUSPEND_DELAY_MAXIMUM_MS * 1000000LL);
    common_write_to_sysfs_file("/sys/power/wake_lock", buff);

#if VERBOSE_WAKELOCKING
    log_debug("common_acquire_wakelock %s", wakelock_name);
#endif
}

/** Release wakelock via sysfs
 *
 * @param wakelock_name Wake lock to be released
 */
void common_release_wakelock(const char *wakelock_name)
{
#if VERBOSE_WAKELOCKING
    log_debug("common_release_wakelock %s", wakelock_name);
#endif

    common_write_to_sysfs_file("/sys/power/wake_unlock", wakelock_name);
}

/* ------------------------------------------------------------------------- *
 * BLOCKING_OPERATION
 * ------------------------------------------------------------------------- */

/** Wrapper to give visibility to blocking system() calls usb-moded is making
 */
int
common_system_(const char *file, int line, const char *func,
                 const char *command)
{
    log_debug("EXEC %s; from %s:%d: %s()",
              command, file, line, func);

    int rc = system(command);

    if( rc != 0 )
        log_warning("EXEC %s; exit code is %d", command, rc);

    return rc;
}

/** Wrapper to give visibility subprocesses usb-moded is invoking via popen()
 */
FILE *
common_popen_(const char *file, int line, const char *func,
                const char *command, const char *type)
{
    log_debug("EXEC %s; from %s:%d: %s()",
              command, file, line, func);

    return popen(command, type);
}

/** Wrapper to give visibility to blocking sleeps usb-moded is making
 */
void
common_usleep_(const char *file, int line, const char *func,
                 useconds_t usec)
{
    struct timespec ts = {
        .tv_sec  = (usec / 1000000),
        .tv_nsec = (usec % 1000000) * 1000
    };

    long ms = (ts.tv_nsec + 1000000 - 1) / 1000000;

    if( !ms ) {
        log_debug("SLEEP %ld seconds; from %s:%d: %s()",
                  (long)ts.tv_sec, file, line, func);
    }
    else if( ts.tv_sec ) {
        log_debug("SLEEP %ld.%03ld seconds; from %s:%d: %s()",
                  (long)ts.tv_sec, ms, file, line, func);
    }
    else {
        log_debug("SLEEP %ld milliseconds; from %s:%d: %s()",
                  ms, file, line, func);
    }

    do {
        if( worker_bailing_out() ) {
            log_warning("SLEEP %ld milliseconds - ignored; from %s:%d: %s()",
                        ms, file, line, func);
          break;
        }

    } while( nanosleep(&ts, &ts) == -1 && errno != EINTR );
}

/* ------------------------------------------------------------------------- *
 * MISC
 * ------------------------------------------------------------------------- */

/* check if a mode is in a list */
static bool common_mode_in_list(const char *mode, char * const *modes)
{
    int i;

    if (!modes)
        return false;

    for(i = 0; modes[i] != NULL; i++)
    {
        if(!strcmp(modes[i], mode))
            return true;
    }
    return false;
}

/** check if a given usb_mode exists
 *
 * @param mode The mode to look for
 * @return 0 if mode exists, 1 if it does not exist
 *
 */
int common_valid_mode(const char *mode)
{
    int valid = 1;
    /* MODE_ASK, MODE_CHARGER and MODE_CHARGING_FALLBACK are not modes that are settable seen their special 'internal' status
     * so we only check the modes that are announed outside. Only exception is the built in MODE_CHARGING */
    if(!strcmp(MODE_CHARGING, mode))
        valid = 0;
    else
    {
        char *whitelist;
        gchar **whitelist_split = NULL;

        whitelist = config_get_mode_whitelist();
        if (whitelist)
        {
            whitelist_split = g_strsplit(whitelist, ",", 0);
            g_free(whitelist);
        }

        /* check dynamic modes */
        if(usbmoded_modelist)
        {
            GList *iter;

            for( iter = usbmoded_modelist; iter; iter = g_list_next(iter) )
            {
                struct mode_list_elem *data = iter->data;
                if(!strcmp(mode, data->mode_name))
                {
                    if (!whitelist_split || common_mode_in_list(data->mode_name, whitelist_split))
                        valid = 0;
                    break;
                }
            }

            g_strfreev(whitelist_split);
        }
    }
    return valid;

}

/** make a list of all available usb modes
 *
 * @param type The type of list to return. Supported or available.
 * @return a comma-separated list of modes (MODE_ASK not included as it is not a real mode)
 *
 */
gchar *common_get_mode_list(mode_list_type_t type)
{
    GString *modelist_str;

    modelist_str = g_string_new(NULL);

    if(!usbmoded_diag_mode)
    {
        /* check dynamic modes */
        if(usbmoded_modelist)
        {
            GList *iter;
            char *hidden_modes_list, *whitelist;
            gchar **hidden_mode_split = NULL, **whitelist_split = NULL;

            hidden_modes_list = config_get_hidden_modes();
            if(hidden_modes_list)
            {
                hidden_mode_split = g_strsplit(hidden_modes_list, ",", 0);
                g_free(hidden_modes_list);
            }

            if (type == AVAILABLE_MODES_LIST)
            {
                whitelist = config_get_mode_whitelist();
                if (whitelist)
                {
                    whitelist_split = g_strsplit(whitelist, ",", 0);
                    g_free(whitelist);
                }
            }

            for( iter = usbmoded_modelist; iter; iter = g_list_next(iter) )
            {
                struct mode_list_elem *data = iter->data;

                /* skip items in the hidden list */
                if (common_mode_in_list(data->mode_name, hidden_mode_split))
                    continue;

                /* if there is a whitelist skip items not in the list */
                if (whitelist_split && !common_mode_in_list(data->mode_name, whitelist_split))
                    continue;

                modelist_str = g_string_append(modelist_str, data->mode_name);
                modelist_str = g_string_append(modelist_str, ", ");
            }

            g_strfreev(hidden_mode_split);
            g_strfreev(whitelist_split);
        }

        /* end with charging mode */
        g_string_append(modelist_str, MODE_CHARGING);
        return g_string_free(modelist_str, false);
    }
    else
    {
        /* diag mode. there is only one active mode */
        g_string_append(modelist_str, MODE_DIAG);
        return g_string_free(modelist_str, false);
    }
}
