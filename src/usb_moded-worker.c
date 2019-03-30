/**
 * @file usb_moded-worker.c
 *
 * Copyright (C) 2013-2019 Jolla. All rights reserved.
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

#include "usb_moded-worker.h"

#include "usb_moded-android.h"
#include "usb_moded-configfs.h"
#include "usb_moded-control.h"
#include "usb_moded-dyn-config.h"
#include "usb_moded-log.h"
#include "usb_moded-modes.h"
#include "usb_moded-modesetting.h"
#include "usb_moded-modules.h"

#include <sys/eventfd.h>

#include <pthread.h> // NOTRIM
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * WORKER
 * ------------------------------------------------------------------------- */

static bool        worker_thread_p                 (void);
bool               worker_bailing_out              (void);
static bool        worker_mode_is_mtp_mode         (const char *mode);
static bool        worker_is_mtpd_running          (void);
static bool        worker_mtpd_running_p           (void *aptr);
static bool        worker_mtpd_stopped_p           (void *aptr);
static bool        worker_stop_mtpd                (void);
static bool        worker_start_mtpd               (void);
static bool        worker_switch_to_charging       (void);
const char        *worker_get_kernel_module        (void);
bool               worker_set_kernel_module        (const char *module);
void               worker_clear_kernel_module      (void);
mode_list_elem_t  *worker_get_usb_mode_data        (void);
void               worker_set_usb_mode_data        (mode_list_elem_t *data);
static const char *worker_get_activated_mode_locked(void);
static bool        worker_set_activated_mode_locked(const char *mode);
static const char *worker_get_requested_mode_locked(void);
static bool        worker_set_requested_mode_locked(const char *mode);
void               worker_request_hardware_mode    (const char *mode);
void               worker_clear_hardware_mode      (void);
static void        worker_execute                  (void);
void               worker_switch_to_mode           (const char *mode);
static guint       worker_add_iowatch              (int fd, bool close_on_unref, GIOCondition cnd, GIOFunc io_cb, gpointer aptr);
static void       *worker_thread_cb                (void *aptr);
static gboolean    worker_notify_cb                (GIOChannel *chn, GIOCondition cnd, gpointer data);
static bool        worker_start_thread             (void);
static void        worker_stop_thread              (void);
static void        worker_delete_eventfd           (void);
static bool        worker_create_eventfd           (void);
bool               worker_init                     (void);
void               worker_quit                     (void);
void               worker_wakeup                   (void);
static void        worker_notify                   (void);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static pthread_t worker_thread_id = 0;

static pthread_mutex_t  worker_mutex = PTHREAD_MUTEX_INITIALIZER;

/** Flag for: Main thread has changed target mode worker should apply
 *
 * Worker should bailout from synchronous activities related to
 * ongoing activation of a usb mode.
 */
static volatile bool worker_bailout_requested = false;

/** Flag for: Worker thread is cleaning up after abandoning mode switch
 *
 * Asynchronous activities on mode cleanup should be executed without
 * bailing out.
 */
static volatile bool worker_bailout_handled = false;

#define WORKER_LOCKED_ENTER do {\
    if( pthread_mutex_lock(&worker_mutex) != 0 ) { \
        log_crit("WORKER LOCK FAILED");\
        _exit(EXIT_FAILURE);\
    }\
}while(0)

#define WORKER_LOCKED_LEAVE do {\
    if( pthread_mutex_unlock(&worker_mutex) != 0 ) { \
        log_crit("WORKER UNLOCK FAILED");\
        _exit(EXIT_FAILURE);\
    }\
}while(0)

/* ========================================================================= *
 * Functions
 * ========================================================================= */

static bool
worker_thread_p(void)
{
    LOG_REGISTER_CONTEXT;

    return worker_thread_id && worker_thread_id == pthread_self();
}

bool
worker_bailing_out(void)
{
    LOG_REGISTER_CONTEXT;

    // ref: see common_msleep_()
    return (worker_thread_p() &&
            worker_bailout_requested &&
            !worker_bailout_handled);
}

/* ------------------------------------------------------------------------- *
 * MTP_DAEMON
 * ------------------------------------------------------------------------- */

/** Maximum time to wait for mtpd to start [ms]
 *
 * This needs to include time to start systemd unit
 * plus however long it might take for mtpd to scan
 * all files exposed over mtp. On a slow device with
 * lots of files it can easily take over 30 seconds,
 * especially during the 1st mtp connect after reboot.
 *
 * Use two minutes as some kind of worst case estimate.
 */
static unsigned worker_mtp_start_delay = 120 * 1000;

/** Maximum time to wait for mtpd to stop [ms]
 *
 * This is just regular service stop. Expected to
 * take max couple of seconds, but use someting
 * in the ballbark of systemd default i.e. 15 seconds
 */
static unsigned worker_mtp_stop_delay  =  15 * 1000;

/** Flag for: We have started mtp daemon
 *
 * If we have issued systemd unit start, we should also
 * issue systemd unit stop even if probing for mtpd
 * presense gives negative result.
 */
static bool worker_mtp_service_started = false;

static bool worker_mode_is_mtp_mode(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    return mode && !strcmp(mode, "mtp_mode");
}

static bool worker_is_mtpd_running(void)
{
    LOG_REGISTER_CONTEXT;

    /* ep0 becomes available when /dev/mtp is mounted.
     *
     * ep1, ep2, ep3 exist while mtp daemon is running,
     * has ep0 opened and has written config data to it.
     */
    static const char * const lut[] = {
        "/dev/mtp/ep0",
        "/dev/mtp/ep1",
        "/dev/mtp/ep2",
        "/dev/mtp/ep3",
        0
    };

    bool ack = true;

    for( size_t i = 0; lut[i]; ++i ) {
        if( access(lut[i], F_OK) == -1 ) {
            ack = false;
            break;
        }
    }

    return ack;
}

static bool
worker_mtpd_running_p(void *aptr)
{
    LOG_REGISTER_CONTEXT;

    (void)aptr;
    return worker_is_mtpd_running();
}

static bool
worker_mtpd_stopped_p(void *aptr)
{
    LOG_REGISTER_CONTEXT;

    (void)aptr;
    return !worker_is_mtpd_running();
}

static bool
worker_stop_mtpd(void)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if( !worker_mtp_service_started && worker_mtpd_stopped_p(0) ) {
        log_debug("mtp daemon is not running");
        goto SUCCESS;
    }

    int rc = common_system("systemctl-user stop buteo-mtp.service");
    if( rc != 0 ) {
        log_warning("failed to stop mtp daemon; exit code = %d", rc);
        goto FAILURE;
    }

    /* Have succesfully stopped mtp service */
    worker_mtp_service_started = false;

    if( common_wait(worker_mtp_stop_delay, worker_mtpd_stopped_p, 0) != WAIT_READY ) {
        log_warning("failed to stop mtp daemon; giving up");
        goto FAILURE;
    }

    log_debug("mtp daemon has stopped");

SUCCESS:
    ack = true;

FAILURE:
    return ack;
}

static bool
worker_start_mtpd(void)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if( worker_mtpd_running_p(0) ) {
        log_debug("mtp daemon is running");
        goto SUCCESS;
    }

    /* Have attempted to start mtp service */
    worker_mtp_service_started = true;

    int rc = common_system("systemctl-user start buteo-mtp.service");
    if( rc != 0 ) {
        log_warning("failed to start mtp daemon; exit code = %d", rc);
        goto FAILURE;
    }

    if( common_wait(worker_mtp_start_delay, worker_mtpd_running_p, 0) != WAIT_READY ) {
        log_warning("failed to start mtp daemon; giving up");
        goto FAILURE;
    }

    log_debug("mtp daemon has started");

SUCCESS:
    ack = true;

FAILURE:
    return ack;
}

static bool worker_switch_to_charging(void)
{
    LOG_REGISTER_CONTEXT;

    bool ack = true;

    if( android_set_charging_mode() )
        goto SUCCESS;

    if( configfs_set_charging_mode() )
        goto SUCCESS;

    if( modules_in_use() ) {
        if( worker_set_kernel_module(MODULE_MASS_STORAGE) )
            goto SUCCESS;
        worker_set_kernel_module(MODULE_NONE);
    }

    log_err("switch to charging mode failed");

    ack = false;
SUCCESS:
    return ack;
}

/* ------------------------------------------------------------------------- *
 * KERNEL_MODULE
 * ------------------------------------------------------------------------- */

/** The module name for the specific mode */
static char *worker_kernel_module = NULL;

/** get the supposedly loaded module
 *
 * @return The name of the loaded module
 *
 */
const char * worker_get_kernel_module(void)
{
    LOG_REGISTER_CONTEXT;

    return worker_kernel_module ?: MODULE_NONE;
}

/** set the loaded module
 *
 * @param module The module name for the requested mode
 *
 */
bool worker_set_kernel_module(const char *module)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if( !module )
        module = MODULE_NONE;

    const char *current = worker_get_kernel_module();

    log_debug("current module: %s -> %s", current, module);

    if( !g_strcmp0(current, module) )
        goto SUCCESS;

    if( modules_unload_module(current) != 0 )
        goto EXIT;

    free(worker_kernel_module), worker_kernel_module = 0;

    if( modules_load_module(module) != 0 )
        goto EXIT;

    if( g_strcmp0(module, MODULE_NONE) )
        worker_kernel_module = strdup(module);

SUCCESS:
    ack = true;
EXIT:
    return ack;
}

void worker_clear_kernel_module(void)
{
    LOG_REGISTER_CONTEXT;

    free(worker_kernel_module), worker_kernel_module = 0;
}

/* ------------------------------------------------------------------------- *
 * MODE_DATA
 * ------------------------------------------------------------------------- */

/** Contains the mode data */
static mode_list_elem_t *worker_mode_data = NULL;

/** get the usb mode data
 *
 * @return a pointer to the usb mode data
 *
 */
mode_list_elem_t *worker_get_usb_mode_data(void)
{
    LOG_REGISTER_CONTEXT;

    return worker_mode_data;
}

/** set the mode_list_elem_t data
 *
 * @param data mode_list_element pointer
 *
 */
void worker_set_usb_mode_data(mode_list_elem_t *data)
{
    LOG_REGISTER_CONTEXT;

    worker_mode_data = data;
}

/* ------------------------------------------------------------------------- *
 * HARDWARE_MODE
 * ------------------------------------------------------------------------- */

/* The hardware mode name
 *
 * How the usb hardware has been configured.
 *
 * For example internal_mode=MODE_ASK gets
 * mapped to hardware_mode=MODE_CHARGING */
static gchar *worker_requested_mode = NULL;

static gchar *worker_activated_mode = NULL;

static const char *
worker_get_activated_mode_locked(void)
{
    LOG_REGISTER_CONTEXT;

    return worker_activated_mode ?: MODE_UNDEFINED;
}

static bool
worker_set_activated_mode_locked(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    bool changed = false;
    const char *prev = worker_get_activated_mode_locked();

    if( !g_strcmp0(prev, mode) )
        goto EXIT;

    log_debug("activated_mode: %s -> %s", prev, mode);
    g_free(worker_activated_mode),
        worker_activated_mode =  g_strdup(mode);
    changed = true;

EXIT:
    return changed;
}

static const char *
worker_get_requested_mode_locked(void)
{
    LOG_REGISTER_CONTEXT;

    return worker_requested_mode ?: MODE_UNDEFINED;
}

static bool
worker_set_requested_mode_locked(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    bool changed = false;
    const char *prev = worker_get_requested_mode_locked();

    if( !g_strcmp0(prev, mode) )
        goto EXIT;

    log_debug("requested_mode: %s -> %s", prev, mode);
    g_free(worker_requested_mode),
        worker_requested_mode =  g_strdup(mode);
    changed = true;

EXIT:
    return changed;
}

void worker_request_hardware_mode(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    WORKER_LOCKED_ENTER;

    if( !worker_set_requested_mode_locked(mode) )
        goto EXIT;

    worker_wakeup();

EXIT:
    WORKER_LOCKED_LEAVE;
    return;
}

void worker_clear_hardware_mode(void)
{
    LOG_REGISTER_CONTEXT;

    WORKER_LOCKED_ENTER;
    g_free(worker_requested_mode), worker_requested_mode = 0;
    WORKER_LOCKED_LEAVE;
}

static void
worker_execute(void)
{
    LOG_REGISTER_CONTEXT;

    WORKER_LOCKED_ENTER;

    const char *activated = worker_get_activated_mode_locked();
    const char *requested = worker_get_requested_mode_locked();
    const char *activate  = common_map_mode_to_hardware(requested);

    log_debug("activated = %s", activated);
    log_debug("requested = %s", requested);
    log_debug("activate = %s",   activate);

    bool changed = g_strcmp0(activated, activate) != 0;
    gchar *mode  = g_strdup(activate);

    WORKER_LOCKED_LEAVE;

    if( changed )
        worker_switch_to_mode(mode);
    else
        worker_notify();

    g_free(mode);

    return;
}

/* ------------------------------------------------------------------------- *
 * MODE_SWITCH
 * ------------------------------------------------------------------------- */

void
worker_switch_to_mode(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    const char *override = 0;

    /* set return to 1 to be sure to error out if no matching mode is found either */

    log_debug("Cleaning up previous mode");

    /* Either mtp daemon is not needed, or it must be *started* in
     * correct phase of gadget configuration when entering mtp mode.
     */
    worker_stop_mtpd();

    if( worker_get_usb_mode_data() ) {
        modesetting_leave_dynamic_mode();
        worker_set_usb_mode_data(NULL);
    }

    log_debug("Setting %s\n", mode);

    /* Mode mapping should mean we only see MODE_CHARGING here, but just
     * in case redirect fixed charging related things to charging ... */

    if( !strcmp(mode, MODE_CHARGING) ||
        !strcmp(mode, MODE_CHARGING_FALLBACK) ||
        !strcmp(mode, MODE_CHARGER) ||
        !strcmp(mode, MODE_UNDEFINED) ||
        !strcmp(mode, MODE_ASK)) {
        goto CHARGE;
    }

    if( !usbmoded_can_export() ) {
        log_warning("Policy does not allow mode: %s", mode);
        goto FAILED;
    }

    /* go through all the dynamic modes if the modelist exists*/
    for( GList *iter = usbmoded_get_modelist(); iter; iter = g_list_next(iter) )
    {
        mode_list_elem_t *data = iter->data;
        if( strcmp(mode, data->mode_name) )
            continue;

        log_debug("Matching mode %s found.\n", mode);

        /* set data before calling any of the dynamic mode functions
         * as they will use the worker_get_usb_mode_data function */
        worker_set_usb_mode_data(data);

        /* When dealing with configfs, we can't enable UDC without
         * already having mtpd running */
        if( worker_mode_is_mtp_mode(mode) && configfs_in_use() ) {
            if( !worker_start_mtpd() )
                goto FAILED;
        }

        if( !worker_set_kernel_module(data->mode_module) )
            goto FAILED;

        if( !modesetting_enter_dynamic_mode() )
            goto FAILED;

        /* When dealing with android usb, it must be enabled before
         * we can start mtpd. Assumption is that the same applies
         * when using kernel modules. */
        if( worker_mode_is_mtp_mode(mode) && !configfs_in_use() ) {
            if( !worker_start_mtpd() )
                goto FAILED;
        }

        goto SUCCESS;
    }

    log_warning("Matching mode %s was not found.", mode);

FAILED:
    worker_bailout_handled = true;

    /* Undo any changes we might have might have already done */
    if( worker_get_usb_mode_data() ) {
        log_debug("Cleaning up failed mode switch");
        worker_stop_mtpd();
        modesetting_leave_dynamic_mode();
        worker_set_usb_mode_data(NULL);
    }

    /* From usb configuration point of view MODE_UNDEFINED and
     * MODE_CHARGING are the same, but for the purposes of exposing
     * a sane state over D-Bus we need to differentiate between
     * "failure to set mode" and "aborting mode setting due to cable
     * disconnect" by inspecting whether target mode has been
     * switched to undefined.
     */
    WORKER_LOCKED_ENTER;
    const char *requested = worker_get_requested_mode_locked();
    if( !g_strcmp0(requested, MODE_UNDEFINED) )
        override = MODE_UNDEFINED;
    else
        override = MODE_CHARGING;
    WORKER_LOCKED_LEAVE;
    log_warning("mode setting failed, try %s", override);

CHARGE:
    if( worker_switch_to_charging() )
        goto SUCCESS;

    log_crit("failed to activate charging, all bets are off");

    /* FIXME: double check this error path */

    /* If we get here then usb_module loading failed,
     * no mode matched, and charging setup failed too.
     */

    override = MODE_UNDEFINED;
    log_warning("mode setting failed, fallback to %s", override);
    worker_set_kernel_module(MODULE_NONE);

SUCCESS:

    WORKER_LOCKED_ENTER;
    if( override ) {
        worker_set_requested_mode_locked(override);
        override = common_map_mode_to_hardware(override);
        worker_set_activated_mode_locked(override);
    }
    else {
        worker_set_activated_mode_locked(mode);
    }
    WORKER_LOCKED_LEAVE;

    worker_notify();

    return;
}

/* ------------------------------------------------------------------------- *
 * WORKER_THREAD
 * ------------------------------------------------------------------------- */

/** eventfd descriptor for waking up worker thread after adding new jobs */
static int              worker_req_evfd  = -1;

/** eventfd descriptor for waking up main thread after executing jobs */
static int              worker_rsp_evfd  = -1;

/** I/O watch identifier for worker_rsp_evfd */
static guint            worker_rsp_wid   = 0;

static guint
worker_add_iowatch(int fd, bool close_on_unref,
               GIOCondition cnd, GIOFunc io_cb, gpointer aptr)
{
    LOG_REGISTER_CONTEXT;

    guint         wid = 0;
    GIOChannel   *chn = 0;

    if( !(chn = g_io_channel_unix_new(fd)) )
        goto cleanup;

    g_io_channel_set_close_on_unref(chn, close_on_unref);

    cnd |= G_IO_ERR | G_IO_HUP | G_IO_NVAL;

    if( !(wid = g_io_add_watch(chn, cnd, io_cb, aptr)) )
        goto cleanup;

cleanup:
    if( chn != 0 ) g_io_channel_unref(chn);

    return wid;

}

static void *worker_thread_cb(void *aptr)
{
    LOG_REGISTER_CONTEXT;

    (void)aptr;

    /* Async cancellation, but disabled */
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0);

    /* Leave INT/TERM signal processing up to the main thread */
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGINT);
    sigaddset(&ss, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &ss, 0);

    /* Loop until explicitly canceled */
    for( ;; ) {
        /* Async cancellation point at wait() */
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
        uint64_t cnt = 0;
        int rc = read(worker_req_evfd, &cnt, sizeof cnt);
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);

        if( rc == -1 ) {
            if( errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK )
                continue;
            log_err("read: %m");
            goto EXIT;
        }

        if( rc != sizeof cnt )
            continue;

        if( cnt > 0 ) {
            worker_bailout_requested = false;
            worker_bailout_handled = false;
            worker_execute();
        }

    }
EXIT:
    return 0;
}

static gboolean
worker_notify_cb(GIOChannel *chn, GIOCondition cnd, gpointer data)
{
    LOG_REGISTER_CONTEXT;

    (void)data;

    gboolean keep_going = FALSE;

    if( !worker_rsp_wid )
        goto cleanup_nak;

    int fd = g_io_channel_unix_get_fd(chn);

    if( fd < 0 )
        goto cleanup_nak;

    if( cnd & ~G_IO_IN )
        goto cleanup_nak;

    if( !(cnd & G_IO_IN) )
        goto cleanup_ack;

    uint64_t cnt = 0;

    int rc = read(fd, &cnt, sizeof cnt);

    if( rc == 0 ) {
        log_err("unexpected eof");
        goto cleanup_nak;
    }

    if( rc == -1 ) {
        if( errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK )
            goto cleanup_ack;

        log_err("read error: %m");
        goto cleanup_nak;
    }

    if( rc != sizeof cnt )
        goto cleanup_nak;

    {
        WORKER_LOCKED_ENTER;
        const char *mode = worker_get_requested_mode_locked();
        gchar *work = g_strdup(mode);
        WORKER_LOCKED_LEAVE;

        control_mode_switched(work);
        g_free(work);
    }

cleanup_ack:
    keep_going = TRUE;

cleanup_nak:

    if( !keep_going ) {
        worker_rsp_wid = 0;
        log_crit("worker notifications disabled");
    }

    return keep_going;
}

static bool
worker_start_thread(void)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;
    int err = pthread_create(&worker_thread_id, 0, worker_thread_cb, 0);
    if( err ) {
        worker_thread_id = 0;
        log_err("failed to start worker thread");
    }
    else {
        ack = true;
        log_debug("worker thread started");
    }

    return ack;
}

static void
worker_stop_thread(void)
{
    LOG_REGISTER_CONTEXT;

    if( !worker_thread_id )
        goto EXIT;

    log_debug("stopping worker thread");
    int err = pthread_cancel(worker_thread_id);
    if( err ) {
        log_err("failed to cancel worker thread");
    }
    else {
        log_debug("waiting for worker thread to exit ...");
        void *ret = 0;
        struct timespec tmo = { 0, 0};
        clock_gettime(CLOCK_REALTIME, &tmo);
        tmo.tv_sec += 3;
        err = pthread_timedjoin_np(worker_thread_id, &ret, &tmo);
        if( err ) {
            log_err("worker thread did not exit");
        }
        else {
            log_debug("worker thread terminated");
            worker_thread_id = 0;
        }
    }

    if( worker_thread_id ) {
        /* Orderly exit is not safe, just die */
        _exit(EXIT_FAILURE);
    }

EXIT:
    return;
}

static void
worker_delete_eventfd(void)
{
    LOG_REGISTER_CONTEXT;

    if( worker_req_evfd != -1 )
        close(worker_req_evfd), worker_req_evfd = -1;

    if( worker_rsp_wid )
        g_source_remove(worker_rsp_wid), worker_rsp_wid = 0;

    if( worker_rsp_evfd != -1 )
        close(worker_rsp_evfd), worker_req_evfd = -1;
}

static bool
worker_create_eventfd(void)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    /* Setup notify pipeline */

    if( (worker_rsp_evfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) == -1 )
        goto EXIT;

    worker_rsp_wid = worker_add_iowatch(worker_rsp_evfd, false, G_IO_IN,
                                worker_notify_cb, 0);
    if( !worker_rsp_wid )
        goto EXIT;

    /* Setup request pipeline */

    if( (worker_req_evfd = eventfd(0, EFD_CLOEXEC)) == -1 )
        goto EXIT;

    ack = true;

EXIT:

    return ack;
}

bool
worker_init(void)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if( !worker_create_eventfd() )
        goto EXIT;

    if( !worker_start_thread() )
        goto EXIT;

    ack = true;

EXIT:
    if( !ack )
        worker_quit();

    return ack;
}

void
worker_quit(void)
{
    LOG_REGISTER_CONTEXT;

    worker_stop_thread();
    worker_delete_eventfd();
}

void
worker_wakeup(void)
{
    LOG_REGISTER_CONTEXT;

    worker_bailout_requested = true;

    uint64_t cnt = 1;
    if( write(worker_req_evfd, &cnt, sizeof cnt) == -1 ) {
        log_err("failed to signal requested: %m");
    }
}

static void
worker_notify(void)
{
    LOG_REGISTER_CONTEXT;

    uint64_t cnt = 1;
    if( write(worker_rsp_evfd, &cnt, sizeof cnt) == -1 ) {
        log_err("failed to signal handled: %m");
    }
}
