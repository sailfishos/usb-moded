#ifndef  USB_MODED_COMMON_H_
# define USB_MODED_COMMON_H_

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

/* ========================================================================= *
 * Functions
 * ========================================================================= */

/* -- cable -- */

const char *cable_state_repr(cable_state_t state);

/* -- common -- */

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
void        common_usleep_                      (const char *file, int line, const char *func, useconds_t usec);
int         common_valid_mode                   (const char *mode);
gchar      *common_get_mode_list                (mode_list_type_t type);

/* ========================================================================= *
 * Macros
 * ========================================================================= */

# define               common_system(command)      common_system_(__FILE__,__LINE__,__FUNCTION__,(command))
# define               common_popen(command, type) common_popen_(__FILE__,__LINE__,__FUNCTION__,(command),(type))
# define               common_usleep(usec)         common_usleep_(__FILE__,__LINE__,__FUNCTION__,(usec))
# define               common_msleep(msec)         common_usleep_(__FILE__,__LINE__,__FUNCTION__,(msec)*1000)
# define               common_sleep(sec)           common_usleep_(__FILE__,__LINE__,__FUNCTION__,(sec)*1000000)

#endif /* USB_MODED_COMMON_H_ */
