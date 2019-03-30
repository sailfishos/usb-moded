/**
 * @file usb_moded-sigpipe.c
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Copyright (C) 2012-2019 Jolla. All rights reserved.
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

#include "usb_moded-sigpipe.h"

#include "usb_moded.h"
#include "usb_moded-log.h"

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * SIGPIPE
 * ------------------------------------------------------------------------- */

static gboolean sigpipe_read_signal_cb(GIOChannel *channel, GIOCondition condition, gpointer data);
static void     sigpipe_trap_signal_cb(int sig);
static bool     sigpipe_crate_pipe    (void);
static void     sigpipe_trap_signals  (void);
bool            sigpipe_init          (void);

/* ========================================================================= *
 * Data
 * ========================================================================= */

/** Pipe fd for transferring signals to mainloop context */
static int sigpipe_fd = -1;

/** Glib io watch callback for reading signals from signal pipe
 *
 * @param channel   glib io channel
 * @param condition wakeup reason
 * @param data      user data (unused)
 *
 * @return TRUE to keep the iowatch, or FALSE to disable it
 */
static gboolean
sigpipe_read_signal_cb(GIOChannel *channel,
                       GIOCondition condition,
                       gpointer data)
{
    LOG_REGISTER_CONTEXT;

    gboolean keep_watch = FALSE;

    int fd, rc, sig;

    (void)data;

    /* Should never happen, but we must disable the io watch
     * if the pipe fd still goes into unexpected state ... */
    if( condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL) )
        goto EXIT;

    if( (fd = g_io_channel_unix_get_fd(channel)) == -1 )
        goto EXIT;

    /* If the actual read fails, terminate with core dump */
    rc = TEMP_FAILURE_RETRY(read(fd, &sig, sizeof sig));
    if( rc != (int)sizeof sig )
        abort();

    /* handle the signal */
    usbmoded_handle_signal(sig);

    keep_watch = TRUE;

EXIT:
    if( !keep_watch )
        log_crit("disabled signal handler io watch\n");

    return keep_watch;
}

/** Async signal handler for writing signals to signal pipe
 *
 * @param sig the signal number to pass to mainloop via pipe
 */
static void
sigpipe_trap_signal_cb(int sig)
{
    LOG_REGISTER_CONTEXT;

    /* NOTE: This function *MUST* be kept async-signal-safe! */

    static volatile int exit_tries = 0;

    int rc;

    /* Restore signal handler */
    signal(sig, sigpipe_trap_signal_cb);

    switch( sig )
    {
    case SIGINT:
    case SIGQUIT:
    case SIGTERM:
        /* If we receive multiple signals that should have
         * caused the process to exit, assume that mainloop
         * is stuck and terminate with core dump. */
        if( ++exit_tries >= 2 )
            abort();
        break;

    default:
        break;
    }

    /* Transfer the signal to mainloop via pipe ... */
    rc = TEMP_FAILURE_RETRY(write(sigpipe_fd, &sig, sizeof sig));

    /* ... or terminate with core dump in case of failures */
    if( rc != (int)sizeof sig )
        abort();
}

/** Create a pipe and io watch for handling signal from glib mainloop
 *
 * @return true on success, or false in case of errors
 */
static bool
sigpipe_crate_pipe(void)
{
    LOG_REGISTER_CONTEXT;

    bool        res    = false;
    GIOChannel *chn    = 0;
    int         pfd[2] = { -1, -1 };

    if( pipe2(pfd, O_CLOEXEC) == -1 )
        goto EXIT;

    if( (chn = g_io_channel_unix_new(pfd[0])) == 0 )
        goto EXIT;

    if( !g_io_add_watch(chn, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                        sigpipe_read_signal_cb, 0) )
        goto EXIT;

    g_io_channel_set_close_on_unref(chn, true), pfd[0] = -1;
    sigpipe_fd = pfd[1], pfd[1] = -1;

    res = true;

EXIT:
    if( chn ) g_io_channel_unref(chn);
    if( pfd[0] != -1 ) close(pfd[0]);
    if( pfd[1] != -1 ) close(pfd[1]);

    return res;
}

/** Install async signal handlers
 */
static void
sigpipe_trap_signals(void)
{
    LOG_REGISTER_CONTEXT;

    static const int sig[] =
    {
        SIGINT,
        SIGQUIT,
        SIGTERM,
        SIGHUP,
        -1
    };

    for( size_t i = 0; sig[i] != -1; ++i )
    {
        signal(sig[i], sigpipe_trap_signal_cb);
    }
}

/** Initialize signal trapping
 *
 * @return true on success, or false in case of errors
 */
bool
sigpipe_init(void)
{
    LOG_REGISTER_CONTEXT;

    bool success = false;

    if( !sigpipe_crate_pipe() )
        goto EXIT;

    sigpipe_trap_signals();

    success = true;

EXIT:
    return success;
}
