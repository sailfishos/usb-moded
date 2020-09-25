/**
 * @file usb_moded-appsync.c
 *
 * Copyright (c) 2010 Nokia Corporation. All rights reserved.
 * Copyright (c) 2013 - 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Philippe De Swert <phdeswer@lumi.maa>
 * @author Philippe De Swert <philippedeswert@gmail.com>
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author Thomas Perl <m@thp.io>
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

#include "usb_moded-appsync.h"

#include "usb_moded.h"
#include "usb_moded-log.h"
#include "usb_moded-systemd.h"

#include <sys/time.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glob.h>

/* ========================================================================= *
 * Types
 * ========================================================================= */

/** Application activation state
 */
typedef enum app_state_t {
    /** Application is not relevant for the current mode */
    APP_STATE_DONTCARE = 0,
    /** Application should be started */
    APP_STATE_INACTIVE = 1,
    /** Application should be stopped when exiting the mode  */
    APP_STATE_ACTIVE   = 2,
} app_state_t;

/**
 * keep all the needed info together for launching an app
 */
typedef struct application_t
{
    char        *name;     /**< name of the app to launch */
    char        *mode;     /**< mode in which to launch the app */
    char        *launch;   /**< dbus launch command/address */
    app_state_t  state;    /**< marker to check if the app has started sucessfully */
    int          systemd;  /**< marker to know if we start it with systemd or not */
    int          post;     /**< marker to indicate when to start the app */
} application_t;

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * APPLICATION
 * ------------------------------------------------------------------------- */

static bool           application_is_valid  (const application_t *self);
static application_t *application_load      (const char *filename);
static void           application_free      (application_t *self);
static void           application_free_cb   (gpointer self);
static gint           application_compare_cb(gconstpointer a, gconstpointer b);

/* ------------------------------------------------------------------------- *
 * APPLIST
 * ------------------------------------------------------------------------- */

static void   applist_free(GList *list);
static GList *applist_load(const char *conf_dir);

/* ------------------------------------------------------------------------- *
 * APPSYNC
 * ------------------------------------------------------------------------- */

void            appsync_switch_configuration      (void);
void            appsync_free_configuration        (void);
void            appsync_load_configuration        (void);
int             appsync_activate_pre              (const char *mode);
int             appsync_activate_post             (const char *mode);
static int      appsync_mark_active_locked        (const char *name, int post);
int             appsync_mark_active               (const char *name, int post);
#ifdef APP_SYNC_DBUS
static gboolean appsync_enumerate_usb_cb          (gpointer data);
static void     appsync_start_enumerate_usb_timer (void);
static void     appsync_cancel_enumerate_usb_timer(void);
static void     appsync_enumerate_usb             (void);
#endif
static void     appsync_stop_apps                 (int post);
void            appsync_deactivate_pre            (void);
void            appsync_deactivate_post           (void);
void            appsync_deactivate_all            (bool force);

/* ========================================================================= *
 * Data
 * ========================================================================= */

/** Mutex for accessing appsync configuration lists
 */
static pthread_mutex_t appsync_mutex = PTHREAD_MUTEX_INITIALIZER;

#define APPSYNC_LOCKED_ENTER do {\
    if( pthread_mutex_lock(&appsync_mutex) != 0 ) { \
        log_crit("APPSYNC LOCK FAILED");\
        _exit(EXIT_FAILURE);\
    }\
}while(0)

#define APPSYNC_LOCKED_LEAVE do {\
    if( pthread_mutex_unlock(&appsync_mutex) != 0 ) { \
        log_crit("APPSYNC UNLOCK FAILED");\
        _exit(EXIT_FAILURE);\
    }\
}while(0)

/** Currently active application list */
static GList *appsync_apps_curr = NULL;

/** Application list to use from the next mode transition onwards */
static GList *appsync_apps_next = NULL;
static bool appsync_apps_updated = false;

#ifdef APP_SYNC_DBUS
static guint appsync_enumerate_usb_id = 0;
static struct timeval appsync_sync_tv = {0, 0};
static int appsync_no_dbus = 0; // enabled until disabled due to failures
#else
static int appsync_no_dbus = 1; // always disabled
#endif /* APP_SYNC_DBUS */

/* ========================================================================= *
 * APPLICATION
 * ========================================================================= */

/** Validity check for loaded application objects
 *
 * To make sense object must specify:
 * - application name
 * - name of the mode that triggers the application
 * - whether its started via systemd or dbus
 *
 * @return true if object is usable, false otherwise
 */
static bool application_is_valid(const application_t *self)
{
    return self && self->name && self->mode && (self->systemd || self->launch);
}

/** Load application object from ini-file
 *
 * @param filename  Path to an ini-file
 *
 * @returns application object pointer, or NULL in case of errors
 */
static application_t *application_load(const char *filename)
{
    LOG_REGISTER_CONTEXT;

    application_t *self   = NULL;
    GKeyFile     *keyfile = NULL;

    log_debug("loading appsync file: %s", filename);

    if( !(keyfile = g_key_file_new()) )
        goto cleanup;

    if( !g_key_file_load_from_file(keyfile, filename, G_KEY_FILE_NONE, NULL) ) {
        log_warning("failed to load appsync file: %s", filename);
        goto cleanup;
    }

    if( !(self = calloc(1, sizeof *self)) )
        goto cleanup;

    self->name = g_key_file_get_string(keyfile, APP_INFO_ENTRY, APP_INFO_NAME_KEY, NULL);
    log_debug("Appname = %s\n", self->name ?: "<unset>");

    self->launch = g_key_file_get_string(keyfile, APP_INFO_ENTRY, APP_INFO_LAUNCH_KEY, NULL);
    log_debug("Launch = %s\n", self->launch ?: "<unset>");

    self->mode = g_key_file_get_string(keyfile, APP_INFO_ENTRY, APP_INFO_MODE_KEY, NULL);
    log_debug("Launch mode = %s\n", self->mode ?: "<unset>");

    self->systemd = g_key_file_get_integer(keyfile, APP_INFO_ENTRY, APP_INFO_SYSTEMD_KEY, NULL);
    log_debug("Systemd control = %d\n", self->systemd);

    self->post = g_key_file_get_integer(keyfile, APP_INFO_ENTRY, APP_INFO_POST, NULL);
    log_debug("post = %d\n", self->post);

    self->state = APP_STATE_DONTCARE;

cleanup:

    if(keyfile)
        g_key_file_free(keyfile);

    /* if a minimum set of required elements is not filled in we discard the list_item */
    if( self && !application_is_valid(self) ) {
        log_warning("discarding invalid appsync file: %s", filename);
        application_free(self),
            self = 0;
    }

    return self;
}

/** Release dynamic memory associated with an application object
 *
 * @param self  Application object, or NULL
 */
static void application_free(application_t *self)
{
    LOG_REGISTER_CONTEXT;

    if( self ) {
        g_free(self->name);
        g_free(self->launch);
        g_free(self->mode);
        free(self);
    }
}

/** GDestroyNotify type object destroy callback
 *
 * @param self  Application object as void pointer, or NULL
 */
static void application_free_cb(gpointer self)
{
    LOG_REGISTER_CONTEXT;

    application_free(self);
}

/** GCompareFunc type object compare callback
 *
 * @param a  Application object as void pointer
 * @param b  Application object as void pointer
 *
 * returns negative value if a < b ; zero if a == b ; positive value if a > b
 */
static gint application_compare_cb(gconstpointer a, gconstpointer b)
{
    LOG_REGISTER_CONTEXT;

    const application_t *application_a = a;
    const application_t *application_b = b;
    return strcasecmp(application_a->name, application_b->name);
}

/* ========================================================================= *
 * APPLIST
 * ========================================================================= */

/** Release a list of application objects
 *
 * @param list  List of objects, or NULL
 */
static void applist_free(GList *list)
{
    g_list_free_full(list, application_free_cb);
}

/** Load a list of application objects
 *
 * @param conf_dir  Path to directory containing ini-files
 *
 * @returns list of application objects, or
 *          NULL if no files were present / could be loaded
 */
static GList *applist_load(const char *conf_dir)
{
    LOG_REGISTER_CONTEXT;

    GList *list = 0;
    gchar *pat  = 0;
    glob_t gb   = {};

    if( !(pat = g_strdup_printf("%s/*.ini", conf_dir)) )
        goto cleanup;

    if( glob(pat, 0, 0, &gb) != 0 ) {
        log_debug("no appsync ini-files found");
        goto cleanup;
    }

    for( size_t i = 0; i < gb.gl_pathc; ++i ) {
        application_t *application = application_load(gb.gl_pathv[i]);
        if( application )
            list = g_list_append(list, application);
    }

    if( list ) {
        /* sort list alphabetically so services for a mode
         * can be run in a certain order */
        list = g_list_sort(list, application_compare_cb);
    }

cleanup:
    globfree(&gb);
    g_free(pat);

    return list;
}

/* ========================================================================= *
 * APPSYNC
 * ========================================================================= */

/** Take previously loaded appsync configuration in use
 *
 */
void appsync_switch_configuration(void)
{
    LOG_REGISTER_CONTEXT;

    APPSYNC_LOCKED_ENTER;

    if( appsync_apps_updated ) {
        appsync_apps_updated = false;
        log_debug("Switch appsync config");
        applist_free(appsync_apps_curr),
            appsync_apps_curr = appsync_apps_next,
            appsync_apps_next = 0;
    }

    APPSYNC_LOCKED_LEAVE;
}

/** Release appsync configuration data
 */
void appsync_free_configuration(void)
{
    LOG_REGISTER_CONTEXT;

    APPSYNC_LOCKED_ENTER;

    if( appsync_apps_curr ) {
        log_debug("Release current appsync config");
        applist_free(appsync_apps_curr),
            appsync_apps_curr = 0;
    }

    if( appsync_apps_next ) {
        log_debug("Release future appsync config");
        applist_free(appsync_apps_next),
            appsync_apps_next = 0;
    }

    APPSYNC_LOCKED_LEAVE;
}

/** Load appsync configuration data
 *
 * Appsync configuration files are read on usb-moded startup and whenever
 * SIGHUP is sent to usb-moded.
 *
 * Appsync configuration data is stateful and accessed both from worker
 * and control threads. Due to this special care must be taken when
 * configuration changes due to SIGHUP. Freshly loaded data is set aside
 * and taken in use by calling appsync_switch_configuration() in an
 * apprioriate time - presently when worker thread is executing mode
 * transition and has cleaned up previously active usb mode.
 */
void appsync_load_configuration(void)
{
    LOG_REGISTER_CONTEXT;

    GList *applist = applist_load(usbmoded_get_diag_mode() ?
                                  CONF_DIR_DIAG_PATH : CONF_DIR_PATH);

    APPSYNC_LOCKED_ENTER;

    if( !appsync_apps_curr ) {
        log_debug("Update current appsync config");
        appsync_apps_curr = applist;

        applist_free(appsync_apps_next),
            appsync_apps_next = 0;
        appsync_apps_updated = false;
    }
    else {
        log_debug("Update future appsync config");
        applist_free(appsync_apps_next),
            appsync_apps_next = applist;
        appsync_apps_updated = true;
    }

    if( appsync_apps_curr ) {
        log_debug("Sync list available");
        /* set up session bus connection if app sync in use
         * so we do not need to make the time consuming connect
         * operation at enumeration time ... */
#ifdef APP_SYNC_DBUS
        dbusappsync_init_connection();
#endif
    }

    APPSYNC_LOCKED_LEAVE;
}

/** Activate pre-enum applications for given mode
 *
 * Starts all configured applications that have matching
 * mode trigger and are scheduled to occur before usb enumeration.
 *
 * @param mode  Name of usb-mode
 *
 * @return 0 on succes, or 1 in case of failures
 */
int appsync_activate_pre(const char *mode)
{
    LOG_REGISTER_CONTEXT;
    int ret = 0; // assume success
    int count = 0;

    log_debug("activate-pre mode=%s", mode);

    APPSYNC_LOCKED_ENTER;

#ifdef APP_SYNC_DBUS
    /* Get start of activation timestamp */
    gettimeofday(&appsync_sync_tv, 0);
#endif

    if( appsync_apps_curr == 0 )
    {
        log_debug("No sync list!");
#ifdef APP_SYNC_DBUS
        appsync_enumerate_usb();
#endif
        goto cleanup;
    }

    /* Count apps that need to be activated for this mode and
     * mark them as currently inactive */
    for( GList *iter = appsync_apps_curr; iter; iter = g_list_next(iter) )
    {
        application_t *application = iter->data;

        if(!strcmp(application->mode, mode))
        {
            ++count;
            application->state = APP_STATE_INACTIVE;
        }
        else
        {
            application->state = APP_STATE_DONTCARE;
        }
    }

    /* If there is nothing to activate, enumerate immediately */
    if(count <= 0)
    {
        log_debug("Nothing to launch\n");
#ifdef APP_SYNC_DBUS
        appsync_enumerate_usb();
#endif
        goto cleanup;
    }

#ifdef APP_SYNC_DBUS
    /* check dbus initialisation, skip dbus activated services if this fails */
    if(!appsync_no_dbus && !dbusappsync_init())
    {
        log_debug("dbus setup failed => skipping dbus launched apps");
        appsync_no_dbus = 1;
    }

    /* start timer */
    appsync_start_enumerate_usb_timer();
#endif

    /* go through list and launch apps */
    for( GList *iter = appsync_apps_curr; iter; iter = g_list_next(iter) )
    {
        application_t *application = iter->data;
        if(!strcmp(mode, application->mode))
        {
            /* do not launch items marked as post, will be launched after usb is up */
            if(application->post)
            {
                continue;
            }
            log_debug("launching pre-enum-app %s", application->name);
            if(application->systemd)
            {
                if(!systemd_control_service(application->name, SYSTEMD_START)) {
                    log_debug("systemd pre-enum-app %s failed", application->name);
                    ret = 1;
                    goto cleanup;
                }
                appsync_mark_active_locked(application->name, 0);
            }
            else if(application->launch)
            {
                /* skipping if dbus session bus is not available,
                 * or not compiled in */
                if( appsync_no_dbus ) {
                    log_debug("dbus pre-enum-app %s ignored", application->name);
                    /* FIXME: feigning success here allows pre-enum actions
                     *        to be "completed" despite of failures or lack
                     *        of support for installed configuration items.
                     *        Does that make any sense?
                     */
                    appsync_mark_active_locked(application->name, 0);
                    continue;
                }
#ifdef APP_SYNC_DBUS
                if( dbusappsync_launch_app(application->launch) != 0 ) {
                    log_debug("dbus pre-enum-app %s failed", application->name);
                    ret = 1;
                    goto cleanup;
                }
                appsync_mark_active_locked(application->name, 0);
#endif /* APP_SYNC_DBUS */
            }
        }
    }

cleanup:
    APPSYNC_LOCKED_LEAVE;

    return ret;
}

/** Activate post-enum applications for given mode
 *
 * Starts all configured applications that have matching
 * mode trigger and are scheduled to occur after usb enumeration.
 *
 * @param mode  Name of usb-mode
 *
 * @return 0 on succes, or 1 in case of failures
 */
int appsync_activate_post(const char *mode)
{
    LOG_REGISTER_CONTEXT;

    int ret = 0; // assume success

    log_debug("activate-post mode=%s", mode);

    APPSYNC_LOCKED_ENTER;

    if( !appsync_apps_curr ) {
        log_debug("No sync list! skipping post sync");
        goto cleanup;
    }

#ifdef APP_SYNC_DBUS
    /* check dbus initialisation, skip dbus activated services if this fails */
    if(!appsync_no_dbus && !dbusappsync_init())
    {
        log_debug("dbus setup failed => skipping dbus launched apps");
        appsync_no_dbus = 1;
    }
#endif /* APP_SYNC_DBUS */

    /* go through list and launch apps */
    for( GList *iter = appsync_apps_curr; iter; iter = g_list_next(iter) )
    {
        application_t *application = iter->data;

        if( !strcmp(application->mode, mode) ) {
            /* launch only items marked as post, others are already running */
            if(!application->post)
                continue;

            log_debug("launching post-enum-app %s\n", application->name);
            if( application->systemd ) {
                if(!systemd_control_service(application->name, SYSTEMD_START)) {
                    log_err("systemd post-enum-app %s failed", application->name);
                    ret = 1;
                    break;
                }
                appsync_mark_active_locked(application->name, 1);
            }
            else if( application->launch ) {
                /* skipping if dbus session bus is not available,
                 * or not compiled in */
                if( appsync_no_dbus ) {
                    log_debug("dbus pre-enum-app %s ignored", application->name);
                    continue;
                }
#ifdef APP_SYNC_DBUS
                if( dbusappsync_launch_app(application->launch) != 0 ) {
                    log_err("dbus post-enum-app %s failed", application->name);
                    ret = 1;
                    break;
                }
                appsync_mark_active_locked(application->name, 1);
#endif /* APP_SYNC_DBUS */
            }
        }
    }

cleanup:
    APPSYNC_LOCKED_LEAVE;

    return ret;
}

/** Set application state as successfully started
 *
 * @param name  Application name
 * @param post  0=pre-enum app, or 1=post-enum app
 *
 * @note Assumes that appsync configuration data is already locked.
 *
 * @see #appsync_mark_active() for details.
 *
 * @return 0 on success, or -1 on failure
 */
static int appsync_mark_active_locked(const char *name, int post)
{
    LOG_REGISTER_CONTEXT;

    int ret = -1; // assume name not found
    int missing = 0;

    log_debug("%s-enum-app %s is started\n", post ? "post" : "pre", name);

    for( GList *iter = appsync_apps_curr; iter; iter = g_list_next(iter) )
    {
        application_t *application = iter->data;

        if(!strcmp(application->name, name))
        {
            /* TODO: do we need to worry about duplicate names in the list? */
            ret = (application->state != APP_STATE_ACTIVE);
            application->state = APP_STATE_ACTIVE;

            /* updated + missing -> not going to enumerate */
            if( missing ) break;
        }
        else if( application->state == APP_STATE_INACTIVE && application->post == post )
        {
            missing = 1;

            /* updated + missing -> not going to enumerate */
            if( ret != -1 ) break;
        }
    }
    if( !post && !missing )
    {
        log_debug("All pre-enum-apps active");
#ifdef APP_SYNC_DBUS
        appsync_enumerate_usb();
#endif
    }

    /* -1=not found, 0=already active, 1=activated now */
    return ret;
}

/** Set application state as successfully started
 *
 * @param name  Application name
 * @param post  0=pre-enum app, or 1=post-enum app
 *
 * Update bookkeeping so that applications that are started when a mode
 * is activated can be stopped when the mode is deactivated later on.
 *
 * @see #appsync_deactivate_pre(),
 *      #appsync_deactivate_post(), and
 *      #appsync_deactivate_all()
 *
 * Note: If usb-moded is configured to use APP_SYNC_DBUS, usb enumeration
 *       actions are triggered when the last pre-enum app gets marked as
 *       active.
 *
 * @return 0 on success, or -1 on failure
 */
int appsync_mark_active(const char *name, int post)
{
    LOG_REGISTER_CONTEXT;

    APPSYNC_LOCKED_ENTER;
    int ret = appsync_mark_active_locked(name, post);
    APPSYNC_LOCKED_LEAVE;

    return ret;
}

#ifdef APP_SYNC_DBUS
static gboolean appsync_enumerate_usb_cb(gpointer data)
{
    LOG_REGISTER_CONTEXT;

    (void)data;
    appsync_enumerate_usb_id = 0;
    log_debug("handling enumeration timeout");
    appsync_enumerate_usb();
    /* return false to stop the timer from repeating */
    return FALSE;
}

static void appsync_start_enumerate_usb_timer(void)
{
    LOG_REGISTER_CONTEXT;

    log_debug("scheduling enumeration timeout");
    if( appsync_enumerate_usb_id )
        g_source_remove(appsync_enumerate_usb_id), appsync_enumerate_usb_id = 0;
    /* NOTE: This was effectively hazard free before blocking mode switch
     *       was offloaded to a worker thread - if APP_SYNC_DBUS is ever
     *       enabled again, this needs to be revisited to avoid timer
     *       scheduled from worker thread getting triggered in mainloop
     *       context before the mode switch activity is finished.
     */
    appsync_enumerate_usb_id = g_timeout_add_seconds(2, appsync_enumerate_usb_cb, NULL);
}

static void appsync_cancel_enumerate_usb_timer(void)
{
    LOG_REGISTER_CONTEXT;

    if( appsync_enumerate_usb_id )
    {
        log_debug("canceling enumeration timeout");
        g_source_remove(appsync_enumerate_usb_id), appsync_enumerate_usb_id = 0;
    }
}

static void appsync_enumerate_usb(void)
{
    LOG_REGISTER_CONTEXT;

    struct timeval tv;

    log_debug("Enumerating");

    /* Stop the timer in case of explicit enumeration call */
    appsync_cancel_enumerate_usb_timer();

    /* Debug: how long it took from sync start to get here */
    gettimeofday(&tv, 0);
    timersub(&tv, &appsync_sync_tv, &tv);
    log_debug("sync to enum: %.3f seconds", tv.tv_sec + tv.tv_usec * 1e-6);

    /* remove dbus service */
    dbusappsync_cleanup();
}
#endif /* APP_SYNC_DBUS */

/* Internal helper for stopping pre/post apps
 *
 * @param post  0=stop pre-apps, or 1=stop post-apps
 *
 * @note Assumes that appsync configuration data is already locked.
 */
static void appsync_stop_apps(int post)
{
    LOG_REGISTER_CONTEXT;

    for( GList *iter = appsync_apps_curr; iter; iter = g_list_next(iter) )
    {
        application_t *application = iter->data;

        if( application->post  == post &&
            application->state == APP_STATE_ACTIVE ) {

            log_debug("stopping %s-enum-app %s", post ? "post" : "pre",
                      application->name);

            if( application->systemd ) {
                if( !systemd_control_service(application->name, SYSTEMD_STOP) )
                    log_debug("Failed to stop %s\n", application->name);
            }
            else if( application->launch ) {
                // NOP
            }
            application->state = APP_STATE_DONTCARE;
        }
    }

}

/** Stop all applications that were started in pre-enum phase
 */
void appsync_deactivate_pre(void)
{
    APPSYNC_LOCKED_ENTER;
    appsync_stop_apps(0);
    APPSYNC_LOCKED_LEAVE;
}

/** Stop all applications that were started in post-enum phase
 */
void appsync_deactivate_post(void)
{
    APPSYNC_LOCKED_ENTER;
    appsync_stop_apps(1);
    APPSYNC_LOCKED_LEAVE;
}

/** Stop all applications that (could) have been started by usb-moded
 *
 * @param force 0=started apps, 1=all configured apps
 *
 * Normally, when force=0 is used, this function is used on mode exit
 * to stop applications that are known to have been started on mode entry.
 *
 * Using force=1 param is mainly useful during usb-moded startup, as
 * a way to cleanup applications that might have been left running as
 * a concequence of for example usb-moded crash.
 */
void appsync_deactivate_all(bool force)
{
    LOG_REGISTER_CONTEXT;

    APPSYNC_LOCKED_ENTER;

    /* If force arg is used, stop all applications that
     * could have been started by usb-moded */
    if(force)
    {
        log_debug("assuming all applications are active");

        for( GList *iter = appsync_apps_curr; iter; iter = g_list_next(iter) )
        {
            application_t *application = iter->data;
            application->state = APP_STATE_ACTIVE;
        }
    }

    /* Stop post-apps 1st */
    appsync_stop_apps(1);

    /* Then pre-apps */
    appsync_stop_apps(0);

    /* Do not leave active timers behind */
#ifdef APP_SYNC_DBUS
    appsync_cancel_enumerate_usb_timer();
#endif

    APPSYNC_LOCKED_LEAVE;
}
