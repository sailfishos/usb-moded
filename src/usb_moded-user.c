/**
 * @file usb_moded-user.c
 *
 * Copyright (c) 2021 Open Mobile Platform LLC.
 * Copyright (c) 2021 Jolla Ltd.
 *
 * @author Mike Salmela <mike.salmela@jolla.com>
 * @author Simo Piiroinen <simo.piiroinen@jolla.com>
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

#include "usb_moded-user.h"
#include "usb_moded-control.h"
#include "usb_moded-log.h"

#include <systemd/sd-login.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * USER
 * ------------------------------------------------------------------------- */

static void user_update_current_user(void);
static void user_set_current_user   (uid_t uid);
uid_t       user_get_current_user   (void);

/* ------------------------------------------------------------------------- *
 * USER_WATCH
 * ------------------------------------------------------------------------- */

static gboolean user_watch_monitor_event_cb(GIOChannel *iochannel, GIOCondition cond, gpointer data);
static bool     user_watch_connect         (void);
static void     user_watch_disconnect      (void);
bool            user_watch_init            (void);
void            user_watch_stop            (void);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static sd_login_monitor    *user_watch_monitor = NULL;
static guint                user_change_watch_id = 0;
static uid_t                user_current_uid = UID_UNKNOWN;

/* ========================================================================= *
 * Functions
 * ========================================================================= */

/**
 * Update the user using the device
 *
 * When built without Sailfish access control support,
 * the user is set to root's uid (0) unconditionally.
 */
static void user_update_current_user(void)
{
#ifdef SAILFISH_ACCESS_CONTROL
    uid_t  active_uid = UID_UNKNOWN;
    char **sessions   = NULL;

    int rc;

    if( (rc = sd_get_sessions(&sessions)) < 0 ) {
        log_warning("sd_get_sessions: %s", strerror(-rc));
        goto EXIT;
    }

    if( rc < 1 || !sessions )
        goto EXIT;

    for( size_t i = 0; active_uid == UID_UNKNOWN && sessions[i]; ++i ) {
        uid_t  uid    = UID_UNKNOWN;
        char  *seat   = NULL;
        char  *state  = NULL;

        if( (rc = sd_session_get_uid(sessions[i], &uid)) < 0 ) {
            log_warning("sd_session_get_uid(%s): %s",
                        sessions[i], strerror(-rc));
        }
        else if( (rc = sd_session_get_state(sessions[i], &state)) < 0 ) {
            log_warning("sd_session_get_state(%s): %s",
                        sessions[i], strerror(-rc));
        }
        else if( (rc = sd_session_get_seat(sessions[i], &seat)) < 0 ) {
            /* NB: It is normal to have sessions without a seat, but
             *     sd_session_get_seat() reports error on such cases
             *     and we do not want that to cause logging noise.
             */
        }
        else if( state && seat && !strcmp(seat, "seat0") ) {
            log_debug("session: seat=%s uid=%d state=%s",  seat, (int)uid, state);
            if( !strcmp(state, "active") || !strcmp(state, "online") )
                active_uid = uid;
        }

        free(state);
        free(seat);
    }

EXIT:
    if( sessions ) {
        for( size_t i = 0; sessions[i]; ++i )
            free(sessions[i]);
        free(sessions);
    }

    user_set_current_user(active_uid);
#else
    user_set_current_user(0);
#endif
}

static void user_set_current_user(uid_t uid)
{
    // User changed
    if ( user_current_uid != uid ) {
        log_debug("user_current_uid: %d -> %d",
                  (int)user_current_uid, (int)uid);
        user_current_uid = uid;
        control_user_changed();
    }
}

/** Get the user using the device
 *
 * When built without Sailfish access control support,
 * this returns root's uid (0) unconditionally.
 *
 * Note: Whether this function is available or not depends on
 *       configure options -> #usbmoded_get_current_user()
 *       can be used to avoid excessive ifdef blocks.
 *
 * @return current user on seat0 or UID_UNKNOWN if it can not be determined
 */
uid_t user_get_current_user(void)
{
    return user_current_uid;
}

/** User change callback
 */
static gboolean user_watch_monitor_event_cb(GIOChannel *iochannel G_GNUC_UNUSED, GIOCondition cond,
                                            gpointer data G_GNUC_UNUSED)
{
    LOG_REGISTER_CONTEXT;
    bool success = true;

    if( cond & (G_IO_ERR | G_IO_HUP | G_IO_NVAL) ) {
        log_crit("user watch hangup/error");
        success = false;
        goto EXIT;
    }
    user_update_current_user();

    sd_login_monitor_flush(user_watch_monitor);

EXIT:
    if ( !success ) {
        user_change_watch_id = 0;
        user_watch_disconnect();
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static bool user_watch_connect(void)
{
    LOG_REGISTER_CONTEXT;

    bool success = false;
    GIOChannel *iochan = NULL;

    if ( sd_login_monitor_new("session", &user_watch_monitor) < 0 ) {
        log_err("Failed to create login monitor\n");
        goto EXIT;
    }

    user_update_current_user();

    iochan = g_io_channel_unix_new(sd_login_monitor_get_fd(user_watch_monitor));
    if ( !iochan ) {
        log_err("Failed to setup I/O channel for sd_login_monitor\n");
        goto EXIT;
    }

    user_change_watch_id = g_io_add_watch(iochan,
                                          G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                                          user_watch_monitor_event_cb,
                                          NULL);
    if ( user_change_watch_id )
        success = true;

EXIT:
    if ( iochan )
        g_io_channel_unref(iochan);
    if ( !success )
        user_watch_disconnect();
    return success;
}

static void user_watch_disconnect(void)
{
    LOG_REGISTER_CONTEXT;

    if ( user_change_watch_id ) {
        g_source_remove(user_change_watch_id);
        user_change_watch_id = 0;
    }
    if ( user_watch_monitor ) {
        sd_login_monitor_unref(user_watch_monitor);
        user_watch_monitor = NULL;
    }
}

bool user_watch_init(void)
{
    LOG_REGISTER_CONTEXT;

    return user_watch_connect();
}

void user_watch_stop(void)
{
    LOG_REGISTER_CONTEXT;

    user_watch_disconnect();
}
