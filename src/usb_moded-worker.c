/**
 * @file usb_moded-worker.c
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

#include "usb_moded-worker.h"

#include "usb_moded.h"
#include "usb_moded-android.h"
#include "usb_moded-configfs.h"
#include "usb_moded-log.h"
#include "usb_moded-modes.h"
#include "usb_moded-modesetting.h"
#include "usb_moded-modules.h"
#include "usb_moded-control.h"
#include "usb_moded-common.h"

#include <sys/eventfd.h>

#include <pthread.h>
#include <stdint.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* -- worker -- */

static bool            worker_thread_p                 (void);
bool                   worker_bailing_out              (void);
static bool            worker_mode_is_mtp_mode         (const char *mode);
static bool            worker_is_mtpd_running          (void);
static bool            worker_stop_mtpd                (void);
static bool            worker_start_mtpd               (void);
static bool            worker_switch_to_charging       (void);
const char            *worker_get_usb_module           (void);
bool                   worker_set_usb_module           (const char *module);
void                   worker_clear_usb_module         (void);
struct mode_list_elem *worker_get_usb_mode_data        (void);
void                   worker_set_usb_mode_data        (struct mode_list_elem *data);
static const char     *worker_get_activated_mode_locked(void);
static bool            worker_set_activated_mode_locked(const char *mode);
static const char     *worker_get_requested_mode_locked(void);
static bool            worker_set_requested_mode_locked(const char *mode);
void                   worker_request_hardware_mode    (const char *mode);
void                   worker_clear_hardware_mode      (void);
static void            worker_execute                  (void);
void                   worker_switch_to_mode           (const char *mode);
static guint           worker_add_iowatch              (int fd, bool close_on_unref, GIOCondition cnd, GIOFunc io_cb, gpointer aptr);
static void           *worker_thread_cb                (void *aptr);
static gboolean        worker_notify_cb                (GIOChannel *chn, GIOCondition cnd, gpointer data);
static bool            worker_start_thread             (void);
static void            worker_stop_thread              (void);
static void            worker_delete_eventfd           (void);
static bool            worker_create_eventfd           (void);
bool                   worker_init                     (void);
void                   worker_quit                     (void);
void                   worker_wakeup                   (void);
static void            worker_notify                   (void);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static pthread_t worker_thread_id = 0;

static pthread_mutex_t  worker_mutex = PTHREAD_MUTEX_INITIALIZER;

static int worker_bailout = 0;

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
    return worker_thread_id && worker_thread_id == pthread_self();
}

bool
worker_bailing_out(void)
{
    // ref: see common_usleep_()
    return worker_thread_p() && worker_bailout > 0;
}

/* ------------------------------------------------------------------------- *
 * MTP_DAEMON
 * ------------------------------------------------------------------------- */

static bool worker_mode_is_mtp_mode(const char *mode)
{
    return mode && !strcmp(mode, "mtp_mode");
}

static bool worker_is_mtpd_running(void)
{
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
worker_stop_mtpd(void)
{
    bool ack = !worker_is_mtpd_running();

    if( ack ) {
        log_debug("mtp daemon is not running");
        goto EXIT;
    }

    int rc = common_system("systemctl-user stop buteo-mtp.service");
    if( rc != 0 ) {
        log_warning("failed to stop mtp daemon; exit code = %d", rc);
        goto EXIT;
    }

    for( int attempts = 3; ; ) {
        if( (ack = !worker_is_mtpd_running()) ) {
            log_debug("mtp daemon has stopped");
            break;
        }

        if( --attempts <= 0) {
            log_warning("failed to stop mtp daemon; giving up");
            break;
        }

        log_debug("waiting for mtp daemon to stop");
        common_msleep(2000);
    }
EXIT:

    return ack;
}

static bool
worker_start_mtpd(void)
{
    bool ack = worker_is_mtpd_running();

    if( ack ) {
        log_debug("mtp daemon is not running");
        goto EXIT;
    }

    int rc = common_system("systemctl-user start buteo-mtp.service");
    if( rc != 0 ) {
        log_warning("failed to start mtp daemon; exit code = %d", rc);
        goto EXIT;
    }

    for( int attempts = 15; ; ) {
        if( (ack = worker_is_mtpd_running()) ) {
            log_debug("mtp daemon has started");
            break;
        }

        if( --attempts <= 0) {
            log_warning("failed to start mtp daemon; giving up");
            break;
        }

        log_debug("waiting for mtp daemon to start");
        common_msleep(2000);
    }
EXIT:

    return ack;
}

static bool worker_switch_to_charging(void)
{
    bool ack = true;

    if( android_set_charging_mode() )
        goto SUCCESS;

    if( configfs_set_charging_mode() )
        goto SUCCESS;

    if( modules_in_use() ) {
        if( worker_set_usb_module(MODULE_MASS_STORAGE) )
            goto SUCCESS;
        worker_set_usb_module(MODULE_NONE);
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
char *usbmoded_module = NULL;

/** get the supposedly loaded module
 *
 * @return The name of the loaded module
 *
 */
const char * worker_get_usb_module(void)
{
    return usbmoded_module ?: MODULE_NONE;
}

/** set the loaded module
 *
 * @param module The module name for the requested mode
 *
 */
bool worker_set_usb_module(const char *module)
{
    bool ack = false;

    if( !module )
        module = MODULE_NONE;

    const char *current = worker_get_usb_module();

    log_debug("current module: %s -> %s", current, module);

    if( !g_strcmp0(current, module) )
        goto SUCCESS;

    if( modules_unload_module(current) != 0 )
        goto EXIT;

    free(usbmoded_module), usbmoded_module = 0;

    if( modules_load_module(module) != 0 )
        goto EXIT;

    if( g_strcmp0(module, MODULE_NONE) )
        usbmoded_module = strdup(module);

SUCCESS:
    ack = true;
EXIT:
    return ack;
}

void worker_clear_usb_module(void)
{
    free(usbmoded_module), usbmoded_module = 0;
}

/* ------------------------------------------------------------------------- *
 * MODE_DATA
 * ------------------------------------------------------------------------- */

/** Contains the mode data */
struct mode_list_elem *usbmoded_data = NULL;

/** get the usb mode data
 *
 * @return a pointer to the usb mode data
 *
 */
struct mode_list_elem * worker_get_usb_mode_data(void)
{
    return usbmoded_data;
}

/** set the mode_list_elem data
 *
 * @param data mode_list_element pointer
 *
 */
void worker_set_usb_mode_data(struct mode_list_elem *data)
{
    usbmoded_data = data;
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
gchar *worker_requested_mode = NULL;

gchar *worker_activated_mode = NULL;

static const char *
worker_get_activated_mode_locked(void)
{
    return worker_activated_mode ?: MODE_UNDEFINED;
}

static bool
worker_set_activated_mode_locked(const char *mode)
{
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
    return worker_requested_mode ?: MODE_UNDEFINED;
}

static bool
worker_set_requested_mode_locked(const char *mode)
{
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
    WORKER_LOCKED_ENTER;
    g_free(worker_requested_mode), worker_requested_mode = 0;
    WORKER_LOCKED_LEAVE;
}

static void
worker_execute(void)
{
    WORKER_LOCKED_ENTER;

    const char *activated = worker_get_activated_mode_locked();
    const char *requested = worker_get_requested_mode_locked();
    const char *activate  = common_map_mode_to_hardware(requested);

    log_debug("activated = %s", activated);
    log_debug("requested = %s", requested);
    log_debug("activate = %s",   activate);

    bool changed = g_strcmp0(activated, activate) != 0;

    WORKER_LOCKED_LEAVE;

    if( changed )
        worker_switch_to_mode(activate);
    else
        worker_notify();

    return;
}

/* ------------------------------------------------------------------------- *
 * MODE_SWITCH
 * ------------------------------------------------------------------------- */

void
worker_switch_to_mode(const char *mode)
{
    const char *override = 0;

    /* set return to 1 to be sure to error out if no matching mode is found either */

    log_debug("Cleaning up previous mode");

    if( !worker_mode_is_mtp_mode(mode) )
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
    for( GList *iter = usbmoded_modelist; iter; iter = g_list_next(iter) )
    {
        struct mode_list_elem *data = iter->data;
        if( strcmp(mode, data->mode_name) )
            continue;

        log_debug("Matching mode %s found.\n", mode);

        /* set data before calling any of the dynamic mode functions
         * as they will use the worker_get_usb_mode_data function */
        worker_set_usb_mode_data(data);

        if( worker_mode_is_mtp_mode(mode) ) {
            if( !worker_start_mtpd() )
                goto FAILED;
        }

        if( !worker_set_usb_module(data->mode_module) )
            goto FAILED;

        if( !modesetting_enter_dynamic_mode() )
            goto FAILED;

        goto SUCCESS;
    }

    log_warning("Matching mode %s was not found.", mode);

FAILED:
    override = MODE_CHARGING;
    log_warning("mode setting failed, try %s", override);
    worker_set_usb_mode_data(NULL);

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
    worker_set_usb_module(MODULE_NONE);

SUCCESS:

    WORKER_LOCKED_ENTER;
    if( override ) {
        worker_set_activated_mode_locked(override);
        worker_set_requested_mode_locked(override);
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
            --worker_bailout;
            log_debug("worker_bailout -> %d", worker_bailout);
            worker_execute();
        }

    }
EXIT:
    return 0;
}

static gboolean
worker_notify_cb(GIOChannel *chn, GIOCondition cnd, gpointer data)
{
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
    worker_stop_thread();
    worker_delete_eventfd();
}

void
worker_wakeup(void)
{
    ++worker_bailout;
    log_debug("worker_bailout -> %d", worker_bailout);

    uint64_t cnt = 1;
    if( write(worker_req_evfd, &cnt, sizeof cnt) == -1 ) {
        log_err("failed to signal requested: %m");
    }
}

static void
worker_notify(void)
{
    uint64_t cnt = 1;
    if( write(worker_rsp_evfd, &cnt, sizeof cnt) == -1 ) {
        log_err("failed to signal handled: %m");
    }
}
