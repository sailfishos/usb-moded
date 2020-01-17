#ifndef  USB_MODED_COMMON_H_
# define USB_MODED_COMMON_H_

# include <stdio.h>
# include <stdbool.h>
# include <glib.h>

/* ========================================================================= *
 * Types
 * ========================================================================= */

/** Mode list types
 */
typedef enum mode_list_type_t {
    /** All configured modes */
    SUPPORTED_MODES_LIST,
    /** Configured modes that can be activated */
    AVAILABLE_MODES_LIST
} mode_list_type_t;

typedef enum {
    CABLE_STATE_UNKNOWN,
    CABLE_STATE_DISCONNECTED,
    CABLE_STATE_CHARGER_CONNECTED,
    CABLE_STATE_PC_CONNECTED,
    CABLE_STATE_NUMOF
} cable_state_t;

typedef enum waitres_t
{
    WAIT_FAILED,
    WAIT_READY,
    WAIT_TIMEOUT,
} waitres_t;

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * CABLE_STATE
 * ------------------------------------------------------------------------- */

const char *cable_state_repr(cable_state_t state);

/* ------------------------------------------------------------------------- *
 * COMMON
 * ------------------------------------------------------------------------- */

const char *common_map_mode_to_hardware         (const char *internal_mode);
const char *common_map_mode_to_external         (const char *internal_mode);
void        common_send_supported_modes_signal  (void);
void        common_send_available_modes_signal  (void);
void        common_send_hidden_modes_signal     (void);
void        common_send_whitelisted_modes_signal(void);
void        common_acquire_wakelock             (const char *wakelock_name);
void        common_release_wakelock             (const char *wakelock_name);
int         common_system_                      (const char *file, int line, const char *func, const char *command);
FILE       *common_popen_                       (const char *file, int line, const char *func, const char *command, const char *type);
waitres_t   common_wait                         (unsigned tot_ms, bool (*ready_cb)(void *aptr), void *aptr);
bool        common_msleep_                      (const char *file, int line, const char *func, unsigned msec);
bool        common_modename_is_internal         (const char *modename);
int         common_valid_mode                   (const char *mode);
gchar      *common_get_mode_list                (mode_list_type_t type, uid_t uid);

/* ========================================================================= *
 * Macros
 * ========================================================================= */

# define               common_system(command)      common_system_(__FILE__,__LINE__,__FUNCTION__,(command))
# define               common_popen(command, type) common_popen_(__FILE__,__LINE__,__FUNCTION__,(command),(type))
# define               common_msleep(msec)         common_msleep_(__FILE__,__LINE__,__FUNCTION__,(msec))
# define               common_sleep(sec)           common_msleep_(__FILE__,__LINE__,__FUNCTION__,(sec)*1000)

#endif /* USB_MODED_COMMON_H_ */
