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
waitres_t    common_wait                         (unsigned tot_ms, bool (*ready_cb)(void *aptr), void *aptr);
bool         common_msleep_                      (const char *file, int line, const char *func, unsigned msec);
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

waitres_t
common_wait(unsigned tot_ms, bool (*ready_cb)(void *aptr), void *aptr)
{
    struct timespec ts;

    waitres_t res = WAIT_FAILED;

    for( ;; ) {
        unsigned nap_ms = (tot_ms > 200) ? 200 : tot_ms;

        ts.tv_sec  = (nap_ms / 1000);
        ts.tv_nsec = (nap_ms % 1000);
        ts.tv_nsec *= 1000 * 1000;

        for( ;; ) {
            if( ready_cb && ready_cb(aptr) ) {
                res = WAIT_READY;
                goto EXIT;
            }

            if( tot_ms <= 0 ) {
                res = WAIT_TIMEOUT;
                goto EXIT;
            }

            if( worker_bailing_out() ) {
                log_warning("wait canceled");
                goto EXIT;
            }

            if( nanosleep(&ts, &ts) == 0 )
                break;

            if( errno != EINTR ) {
                log_warning("wait failed: %m");
                goto EXIT;
            }
        }

        tot_ms -= nap_ms;
    }

EXIT:
    return res;
}

/** Wrapper to give visibility to blocking sleeps usb-moded is making
 */
bool
common_msleep_(const char *file, int line, const char *func, unsigned msec)
{
    log_debug("SLEEP %u.%03u seconds; from %s:%d: %s()",
              msec / 1000u, msec % 1000u,file, line, func);
    return common_wait(msec, 0, 0) == WAIT_TIMEOUT;
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
 *
 * @return 0 if mode exists, 1 if it does not exist
 */
int common_valid_mode(const char *mode)
{
    int valid = 1;
    /* MODE_ASK, MODE_CHARGER and MODE_CHARGING_FALLBACK are not modes that are settable seen their special 'internal' status
     * so we only check the modes that are announed outside. Only exception is the built in MODE_CHARGING */
    if(!strcmp(MODE_CHARGING, mode)) {
        valid = 0;
    }
    else
    {
        gchar  *whitelist_value = 0;
        gchar **whitelist_array = 0;

        if( (whitelist_value = config_get_mode_whitelist()) )
            whitelist_array = g_strsplit(whitelist_value, ",", 0);

        for( GList *iter = usbmoded_get_modelist(); iter; iter = g_list_next(iter) ) {
            mode_list_elem_t *data = iter->data;
            if( strcmp(mode, data->mode_name) )
                continue;

            if (!whitelist_array || common_mode_in_list(data->mode_name, whitelist_array))
                valid = 0;
            break;
        }

        g_strfreev(whitelist_array);
        g_free(whitelist_value);
    }
    return valid;
}

/** make a list of all available usb modes
 *
 * @param type The type of list to return. Supported or available.
 *
 * @return a comma-separated list of modes (MODE_ASK not included as it is not a real mode)
 */
gchar *common_get_mode_list(mode_list_type_t type)
{
    GString *mode_list_str = g_string_new(NULL);

    gchar  *hidden_modes_value = 0;
    gchar **hidden_modes_array = 0;

    gchar  *whitelist_value = 0;
    gchar **whitelist_array = 0;

    if( usbmoded_get_diag_mode() )
    {
        /* diag mode. there is only one active mode */
        g_string_append(mode_list_str, MODE_DIAG);
        goto EXIT;
    }

    if( (hidden_modes_value = config_get_hidden_modes()) )
        hidden_modes_array = g_strsplit(hidden_modes_value, ",", 0);

    switch( type ) {
    case SUPPORTED_MODES_LIST:
        /* All modes that are not hidden */
        break;

    case AVAILABLE_MODES_LIST:
        /* All whitelisted modes that are not hidden */
        if( (whitelist_value = config_get_mode_whitelist()) )
            whitelist_array = g_strsplit(whitelist_value, ",", 0);
        break;
    }

    for( GList *iter = usbmoded_get_modelist(); iter; iter = g_list_next(iter) )
    {
        mode_list_elem_t *data = iter->data;

        /* skip items in the hidden list */
        if (common_mode_in_list(data->mode_name, hidden_modes_array))
            continue;

        /* if there is a whitelist skip items not in the list */
        if (whitelist_array && !common_mode_in_list(data->mode_name, whitelist_array))
            continue;

        g_string_append(mode_list_str, data->mode_name);
        g_string_append(mode_list_str, ", ");
    }

    /* End with charging mode */
    g_string_append(mode_list_str, MODE_CHARGING);

EXIT:
    g_strfreev(whitelist_array);
    g_free(whitelist_value);

    g_strfreev(hidden_modes_array);
    g_free(hidden_modes_value);

    return g_string_free(mode_list_str, false);
}
