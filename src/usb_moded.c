/**
  @file usb_moded.c

  Copyright (C) 2010 Nokia Corporation. All rights reserved.
  Copyright (C) 2012 Jolla. All rights reserved.

  @author: Philippe De Swert <philippe.de-swert@nokia.com>
  @author: Philippe De Swert <philippe.deswert@jollamobile.com>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the Lesser GNU General Public License 
  version 2 as published by the Free Software Foundation. 

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.
 
  You should have received a copy of the Lesser GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
  02110-1301 USA
*/

#define _GNU_SOURCE
#include <getopt.h>
#include <stdio.h>

#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

#include <libkmod.h>
#ifdef SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include "usb_moded.h"
#include "usb_moded-modes.h"
#include "usb_moded-dbus.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded-hw-ab.h"
#include "usb_moded-modules.h"
#include "usb_moded-log.h"
#include "usb_moded-lock.h"
#include "usb_moded-modesetting.h"
#include "usb_moded-modules.h"
#include "usb_moded-appsync.h"
#include "usb_moded-trigger.h"
#include "usb_moded-config.h"
#include "usb_moded-config-private.h"
#include "usb_moded-network.h"
#include "usb_moded-mac.h"
#include "usb_moded-android.h"
#ifdef MEEGOLOCK
#include "usb_moded-dsme.h"
#endif

/* global definitions */

GMainLoop *mainloop = NULL;
extern const char *log_name;
extern int log_level;
extern int log_type;

gboolean rescue_mode = FALSE;
gboolean diag_mode = FALSE;
gboolean hw_fallback = FALSE;
gboolean android_broken_usb = FALSE;
gboolean android_ignore_udev_events = FALSE;
gboolean android_ignore_next_udev_disconnect_event = FALSE;
#ifdef SYSTEMD
static gboolean systemd_notify = FALSE;
#endif

struct usb_mode current_mode;
guint charging_timeout = 0;
static GList *modelist;

/* static helper functions */
static gboolean set_disconnected(gpointer data);
static gboolean set_disconnected_silent(gpointer data);
static void usb_moded_init(void);
static gboolean charging_fallback(gpointer data);
static void usage(void);
static void send_supported_modes_signal(void);


/* ============= Implementation starts here =========================================== */
/** set the usb connection status 
 *
 * @param connected The connection status, true for connected
 * 
 */
void set_usb_connected(gboolean connected)
{
	
  if(connected)
  {
 	/* do not go through the routine if already connected to avoid
           spurious load/unloads due to faulty signalling 
	   NOKIA: careful with devicelock
	*/
	if(current_mode.connected == connected)
		return;

	if(charging_timeout)
	{
		g_source_remove(charging_timeout);
		charging_timeout = 0;
	}
  	current_mode.connected = TRUE;
	/* signal usb connected */
	log_debug("usb connected\n");
	usb_moded_send_signal(USB_CONNECTED);
	set_usb_connected_state();
  }
  else
  {
	if(current_mode.connected == FALSE)
		return;
	if(android_ignore_next_udev_disconnect_event) {
		android_ignore_next_udev_disconnect_event = FALSE;
		return;
	}
	current_mode.connected = FALSE;
	set_disconnected(NULL);
	/* Some android kernels check for an active gadget to enable charging and
	 * cable detection, meaning USB is completely dead unless we keep the gadget
	 * active
	 */
	if(current_mode.android_usb_broken)
		set_android_charging_mode();
	if (android_ignore_udev_events) {
		android_ignore_next_udev_disconnect_event = TRUE;
	}
  }		

}

static gboolean set_disconnected(gpointer data)
{
  /* let usb settle */
  sleep(1);
  /* only disconnect for real if we are actually still disconnected */
  if(!get_usb_connection_state())
	{
		log_debug("usb disconnected\n");
  		/* signal usb disconnected */
		usb_moded_send_signal(USB_DISCONNECTED);
		/* unload modules and general cleanup if not charging */
		if(strcmp(get_usb_mode(), MODE_CHARGING) ||
		   strcmp(get_usb_mode(), MODE_CHARGING_FALLBACK))
			usb_moded_mode_cleanup(get_usb_module());
		/* Nothing else as we do not need to do anything for cleaning up charging mode */
		usb_moded_module_cleanup(get_usb_module());
		set_usb_mode(MODE_UNDEFINED);
	
	}
  return FALSE;
}

/* set disconnected without sending signals. */
static gboolean set_disconnected_silent(gpointer data)
{
if(!get_usb_connection_state())
        {
                log_debug("Resetting connection data after HUP\n");
                /* unload modules and general cleanup if not charging */
                if(strcmp(get_usb_mode(), MODE_CHARGING) ||
		   strcmp(get_usb_mode(), MODE_CHARGING_FALLBACK))
                        usb_moded_mode_cleanup(get_usb_module());
                /* Nothing else as we do not need to do anything for cleaning up charging mode */
                usb_moded_module_cleanup(get_usb_module());
                set_usb_mode(MODE_UNDEFINED);
        }
  return FALSE;

}

/** set and track charger state
 *
 */
void set_charger_connected(gboolean state)
{
  /* check if charger is already connected
     to avoid spamming dbus */
  if(current_mode.connected == state)
                return;

  if(state)
  {
    usb_moded_send_signal(CHARGER_CONNECTED);
    set_usb_mode(MODE_CHARGER);
    current_mode.connected = TRUE;
  }
  else
  {
    current_mode.connected = FALSE;
    usb_moded_send_signal(CHARGER_DISCONNECTED);
    set_usb_mode(MODE_UNDEFINED);
    current_mode.connected = FALSE;
  }
}

/** set the chosen usb state
 *
 */
void set_usb_connected_state(void)
{	
  char *mode_to_set;
#ifdef MEEGOLOCK
  int export = 1; /* assume locked */
#endif /* MEEGOLOCK */

  if(rescue_mode)
  {
	log_debug("Entering rescue mode!\n");
	set_usb_mode(MODE_DEVELOPER);
	return;
  }
  else if(diag_mode)
  {
	log_debug("Entering diagnostic mode!\n");
	if(modelist)
	{
		GList *iter = modelist;
		struct mode_list_elem *data = iter->data;
		set_usb_mode(data->mode_name);
	}
	return;
  }

  mode_to_set = get_mode_setting();

#ifdef MEEGOLOCK
  /* check if we are allowed to export system contents 0 is unlocked */
  export = usb_moded_get_export_permission();
  /* We check also if the device is in user state or not.
     If not we do not export anything. We presume ACT_DEAD charging */
  if(mode_to_set && !export && is_in_user_state())
#else
  if(mode_to_set)
#endif /* MEEGOLOCK */
  {
	/* This is safe to do here as the starting condition is
	MODE_UNDEFINED, and having a devicelock being activated when
	a mode is set will not interrupt it */
	if(!strcmp(mode_to_set, current_mode.mode))
		goto end;

	if(!strcmp(MODE_ASK, mode_to_set))
	{
		/* send signal, mode will be set when the dialog service calls
	  	 the set_mode method call.
	 	*/
		usb_moded_send_signal(USB_CONNECTED_DIALOG_SHOW);
		/* fallback to charging mode after 3 seconds */
		charging_timeout = g_timeout_add_seconds(3, charging_fallback, NULL);
		/* in case there was nobody listening for the UI, they will know 
		   that the UI is needed by requesting the current mode */
		set_usb_mode(MODE_ASK);
	}
	else
		set_usb_mode(mode_to_set);
  }
  else
  {
	/* config is corrupted or we do not have a mode configured, fallback to charging
	   We also fall back here in case the device is locked and we do not 
	   export the system contents. Or if we are in acting dead mode.
	*/
	set_usb_mode(MODE_CHARGING_FALLBACK);
  }
end:
  free(mode_to_set);
}

/** set the usb mode 
 *
 * @param mode The requested USB mode
 * 
 */
void set_usb_mode(const char *mode)
{
  /* set return to 1 to be sure to error out if no matching mode is found either */
  int ret=1, net=0;

#ifdef MEEGOLOCK
  /* Do a second check in case timer suspend causes a race issue */
  int export = 1;
#endif

  log_debug("Setting %s\n", mode);

  /* CHARGING AND FALLBACK CHARGING are always ok to set, so this can be done
     before the optional second device lock check */
  if(!strcmp(mode, MODE_CHARGING) || !strcmp(mode, MODE_CHARGING_FALLBACK))
  {
	check_module_state(MODULE_MASS_STORAGE);
	/* for charging we use a fake file_storage (blame USB certification for this insanity */
	set_usb_module(MODULE_MASS_STORAGE);
	/* MODULE_CHARGING has all the parameters defined, so it will not match the g_file_storage rule in usb_moded_load_module */
	ret = usb_moded_load_module(MODULE_CHARGING);
	/* if charging mode setting did not succeed we might be dealing with android */
	if(ret)
	{
	  if (android_ignore_udev_events) {
	    android_ignore_next_udev_disconnect_event = TRUE;
	  }
	  set_usb_module(MODULE_NONE);
	  ret = set_android_charging_mode();
	}
	goto end;
  }

  /* Dedicated charger mode needs nothing to be done and no user interaction */
  if(!strcmp(mode, MODE_CHARGER))
  {
	ret = 0;
	goto end;
  }

#ifdef MEEGOLOCK
  /* check if we are allowed to export system contents 0 is unlocked */
  /* In ACTDEAD export is always ok */
  if(is_in_user_state())
  {
	export = usb_moded_get_export_permission();

	if(export && !rescue_mode)
	{
		log_debug("Secondary device lock check failed. Not setting mode!\n");
		goto end;
        }
  }
#endif /* MEEGOLOCK */

  /* nothing needs to be done for this mode but signalling.
     Handled here to avoid issues with ask menu and devicelock */
  if(!strcmp(mode, MODE_ASK))
  {
	ret = 0;
	goto end;
  }

  /* go through all the dynamic modes if the modelist exists*/
  if(modelist)
  {
    GList *iter;

    for( iter = modelist; iter; iter = g_list_next(iter) )
    {
      struct mode_list_elem *data = iter->data;
      if(!strcmp(mode, data->mode_name))
      {
	log_debug("Matching mode %s found.\n", mode);
  	check_module_state(data->mode_module);
	set_usb_module(data->mode_module);
	ret = usb_moded_load_module(data->mode_module);
	/* set data before calling any of the dynamic mode functions
	   as they will use the get_usb_mode_data function */
	set_usb_mode_data(data);
	/* check if modules are ok before continuing */
	if(!ret) {
		if (android_ignore_udev_events) {
		  android_ignore_next_udev_disconnect_event = TRUE;
		}
		ret = set_dynamic_mode();
	}
      }
    }
  }

end:
  /* if ret != 0 then usb_module loading failed 
     no mode matched or MODE_UNDEFINED was requested */
  if(ret)
  {
	  set_usb_module(MODULE_NONE);
	  mode = MODE_UNDEFINED;
	  set_usb_mode_data(NULL);
	  log_debug("mode setting failed or device disconnected, mode to set was = %s\n", mode);
  }
  if(net)
    log_debug("Network setting failed!\n");
  free(current_mode.mode);
  current_mode.mode = strdup(mode);
  /* CHARGING_FALLBACK is an internal mode not to be broadcasted outside */
  if(!strcmp(mode, MODE_CHARGING_FALLBACK))
    usb_moded_send_signal(MODE_CHARGING);
  else
    usb_moded_send_signal(get_usb_mode());
}

/** check if a given usb_mode exists
 *
 * @param mode The mode to look for
 * @return 0 if mode exists, 1 if it does not exist
 *
 */
int valid_mode(const char *mode)
{

  /* MODE_ASK, MODE_CHARGER and MODE_CHARGING_FALLBACK are not modes that are settable seen their special 'internal' status 
     so we only check the modes that are announed outside. Only exception is the built in MODE_CHARGING */
  if(!strcmp(MODE_CHARGING, mode))
	return(0);
  else
  {
    /* check dynamic modes */
    if(modelist)
    {
      GList *iter;

      for( iter = modelist; iter; iter = g_list_next(iter) )
      {
        struct mode_list_elem *data = iter->data;
	if(!strcmp(mode, data->mode_name))
		return(0);
      }
    }
  }
  return(1);

}

/** make a list of all available usb modes
 *
 * @return a comma-separated list of modes (MODE_ASK not included as it is not a real mode)
 *
 */
gchar *get_mode_list(void)
{

  GString *modelist_str;

  modelist_str = g_string_new(NULL);

  if(!diag_mode)
  {
    /* check dynamic modes */
    if(modelist)
    {
      GList *iter;

      for( iter = modelist; iter; iter = g_list_next(iter) )
      {
        struct mode_list_elem *data = iter->data;
	modelist_str = g_string_append(modelist_str, data->mode_name);
	modelist_str = g_string_append(modelist_str, ", ");
      }
    }

    /* end with charging mode */
    g_string_append(modelist_str, MODE_CHARGING);
    return(g_string_free(modelist_str, FALSE));
  }
  else
  {
    /* diag mode. there is only one active mode */
    g_string_append(modelist_str, MODE_DIAG);
    return(g_string_free(modelist_str, FALSE));
  }
}

/** get the usb mode 
 *
 * @return the currently set mode
 *
 */
inline const char * get_usb_mode(void)
{
  return(current_mode.mode);
}

/** set the loaded module 
 *
 * @param module The module name for the requested mode
 *
 */
void set_usb_module(const char *module)
{
  free(current_mode.module);
  current_mode.module = strdup(module);
}

/** get the supposedly loaded module
 *
 * @return The name of the loaded module
 *
 */
inline const char * get_usb_module(void)
{
  return(current_mode.module);
}

/** get if the cable is connected or not
 *
 * @ return A boolean value for connected (TRUE) or not (FALSE)
 *
 */
inline gboolean get_usb_connection_state(void)
{
	return current_mode.connected;
}

/** set connection status for some corner cases
 *
 * @param state The connection status that needs to be set. Connected (TRUE)
 *
 */
inline void set_usb_connection_state(gboolean state)
{
	current_mode.connected = state;
}

/** set the mode_list_elem data
 *
 * @param data mode_list_element pointer
 *
*/
void set_usb_mode_data(struct mode_list_elem *data)
{
  current_mode.data = data;
}

/** get the usb mode data 
 *
 * @return a pointer to the usb mode data
 *
 */
inline struct mode_list_elem * get_usb_mode_data(void)
{
  return(current_mode.data);
}

/*================  Static functions ===================================== */

/* set default values for usb_moded */
static void usb_moded_init(void)
{
  current_mode.connected = FALSE;
  current_mode.mounted = FALSE;
  current_mode.mode = strdup(MODE_UNDEFINED);
  current_mode.module = strdup(MODULE_NONE);

  if(android_broken_usb)
	current_mode.android_usb_broken = TRUE;

  /* check config, merge or create if outdated */
  if(conf_file_merge() != 0)
  {
    log_err("Cannot create or find a valid configuration. Exiting.\n");
    exit(1);
  }

#ifdef APP_SYNC
  readlist(diag_mode);
  /* make sure all services are down when starting */
  appsync_stop();
  modelist = read_mode_list(diag_mode);
#endif /* APP_SYNC */

  if(check_trigger())
	trigger_init();

  /* Set-up mac address before kmod */
  if(access("/etc/modprobe.d/g_ether.conf", F_OK) != 0)
  {
    generate_random_mac();  	
  }

  /* kmod init */
  usb_moded_module_ctx_init();

  /* Android specific stuff */
  if(android_settings())
  	android_init_values();
  /* TODO: add more start-up clean-up and init here if needed */
}	

/* charging fallback handler */
static gboolean charging_fallback(gpointer data)
{
  /* if a mode has been set we don't want it changed to charging
   * after 5 seconds. We set it to ask, so anything different 
   * means a mode has been set */
  if(strcmp(get_usb_mode(), MODE_ASK) != 0)
		  return FALSE;

  set_usb_mode(MODE_CHARGING_FALLBACK);
  /* since this is the fallback, we keep an indication
     for the UI, as we are not really in charging mode.
  */
  free(current_mode.mode);
  current_mode.mode = strdup(MODE_ASK);
  current_mode.data = NULL;
  charging_timeout = 0;
  log_info("Falling back on charging mode.\n");
	
  return(FALSE);
}

static void handle_exit(void)
{
  /* exiting and clean-up when mainloop ended */
  appsync_stop();
  hwal_cleanup();
  usb_moded_dbus_cleanup();
#ifdef MEEGOLOCK
  stop_devicelock_listener();
#endif /* MEEGOLOCK */

  free_mode_list(modelist);
  usb_moded_module_ctx_cleanup();

#ifdef APP_SYNC
  free_appsync_list();
#ifdef APP_SYNC_DBUS
  usb_moded_appsync_cleanup();
#endif /* APP_SYNC_DBUS */
#endif /* APP_SYNC */
  /* dbus_shutdown(); This causes exit(1) and don't seem
     to behave as documented */

  /* If the mainloop is initialised, unreference it */
  if (mainloop != NULL)
  {
	g_main_loop_quit(mainloop);
	g_main_loop_unref(mainloop);
  }
  free(current_mode.mode);
  free(current_mode.module);

  log_debug("All resources freed. Exiting!\n");

  exit(0);
}

static void sigint_handler(int signum)
{
  struct mode_list_elem *data;

  if(signum == SIGINT || signum == SIGTERM)
	handle_exit();
  if(signum == SIGHUP)
  {
	/* clean up current mode */
	data = get_usb_mode_data();
	set_disconnected_silent(data);
	/* clear existing data to be sure */
	set_usb_mode_data(NULL);
	/* free and read in modelist again */
	free_mode_list(modelist);

	modelist = read_mode_list(diag_mode);

        send_supported_modes_signal();
  }
}

/* Display usage information */
static void usage(void)
{
        fprintf(stdout,
                "Usage: usb_moded [OPTION]...\n"
                  "USB mode daemon\n"
                  "\n"
		  "  -a,  --android_usb_broken \tkeep gadget active on broken android kernels\n"
		  "  -i,  --android_usb_broken_udev_events \tignore incorrect disconnect events after mode setting\n"
		  "  -f,  --fallback	  \tassume always connected\n"
                  "  -s,  --force-syslog  \t\tlog to syslog\n"
                  "  -T,  --force-stderr  \t\tlog to stderr\n"
                  "  -D,  --debug	  \t\tturn on debug printing\n"
		  "  -d,  --diag	  \t\tturn on diag mode\n"
                  "  -h,  --help          \t\tdisplay this help and exit\n"
		  "  -r,  --rescue	  \t\trescue mode\n"
#ifdef SYSTEMD
		  "  -n,  --systemd       \t\tnotify systemd when started up\n"
#endif
                  "  -v,  --version       \t\toutput version information and exit\n"
                  "\n");
}

static void send_supported_modes_signal(void)
{
    /* Send supported modes signal */
    gchar *mode_list = get_mode_list();
    usb_moded_send_supported_modes_signal(mode_list);
    g_free(mode_list);
}

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
static gboolean sigpipe_read_signal_cb(GIOChannel *channel,
                                       GIOCondition condition,
                                       gpointer data)
{
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
        log_warning("handle signal: %s\n", strsignal(sig));
        sigint_handler(sig);

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
static void sigpipe_write_signal_cb(int sig)
{
        /* NOTE: This function *MUST* be kept async-signal-safe! */

        static volatile int exit_tries = 0;

        int rc;

        /* Restore signal handler */
        signal(sig, sigpipe_write_signal_cb);

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
 * @return TRUE on success, or FALSE in case of errors
 */
static gboolean sigpipe_crate_pipe(void)
{
        gboolean    res    = FALSE;
        GIOChannel *chn    = 0;
        int         pfd[2] = { -1, -1 };

        if( pipe2(pfd, O_CLOEXEC) == -1 )
                goto EXIT;

        if( (chn = g_io_channel_unix_new(pfd[0])) == 0 )
                goto EXIT;

        if( !g_io_add_watch(chn, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                            sigpipe_read_signal_cb, 0) )
                goto EXIT;

        g_io_channel_set_close_on_unref(chn, TRUE), pfd[0] = -1;
        sigpipe_fd = pfd[1], pfd[1] = -1;

        res = TRUE;

EXIT:
        if( chn ) g_io_channel_unref(chn);
        if( pfd[0] != -1 ) close(pfd[0]);
        if( pfd[1] != -1 ) close(pfd[1]);

        return res;
}

/** Install async signal handlers
 */
static void sigpipe_trap_signals(void)
{
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
                signal(sig[i], sigpipe_write_signal_cb);
        }
}

/** Initialize signal trapping
 *
 * @return TRUE on success, or FALSE in case of errors
 */
static gboolean sigpipe_init(void)
{
        gboolean success = FALSE;

        if( !sigpipe_crate_pipe() )
                goto EXIT;

        sigpipe_trap_signals();

        success = TRUE;

EXIT:
        return success;
}

int main(int argc, char* argv[])
{
	int result = EXIT_FAILURE;
        int opt = 0, opt_idx = 0;

	struct option const options[] = {
                { "android_usb_broken", no_argument, 0, 'a' },
                { "android_usb_broken_udev_events", no_argument, 0, 'i' },
                { "fallback", no_argument, 0, 'd' },
                { "force-syslog", no_argument, 0, 's' },
                { "force-stderr", no_argument, 0, 'T' },
                { "debug", no_argument, 0, 'D' },
                { "diag", no_argument, 0, 'd' },
                { "help", no_argument, 0, 'h' },
		{ "rescue", no_argument, 0, 'r' },
		{ "systemd", no_argument, 0, 'n' },
                { "version", no_argument, 0, 'v' },
                { 0, 0, 0, 0 }
        };

	log_name = basename(*argv);

	 /* Parse the command-line options */
        while ((opt = getopt_long(argc, argv, "aifsTDdhrnv", options, &opt_idx)) != -1)
	{
                switch (opt) 
		{
			case 'a':
				android_broken_usb = TRUE;
				break;
			case 'i':
				android_ignore_udev_events = TRUE;
				break;
			case 'f':
				hw_fallback = TRUE;
				break;
		        case 's':
                        	log_type = LOG_TO_SYSLOG;
                        	break;

                	case 'T':
                        	log_type = LOG_TO_STDERR;
                        	break;

                	case 'D':
                        	log_level = LOG_DEBUG;
                        	break;

			case 'd':
				diag_mode = TRUE;
				break;

                	case 'h':
                        	usage();
				exit(0);

			case 'r':
				rescue_mode = TRUE;
				break;
#ifdef SYSTEMD
			case 'n':
				systemd_notify = TRUE;
				break;
#endif	
	                case 'v':
				printf("USB mode daemon version: %s\n", VERSION);
				exit(0);

	                default:
        	                usage();
				exit(0);
                }
        }

	printf("usb_moded %s starting\n", VERSION);
	/* silence system() calls */
	if(log_type != LOG_TO_STDERR || log_level != LOG_DEBUG )	
	{
		freopen("/dev/null", "a", stdout);
		freopen("/dev/null", "a", stderr);
	}
#if !GLIB_CHECK_VERSION(2, 36, 0)
        g_type_init();
#endif
#if !GLIB_CHECK_VERSION(2, 31, 0)
	g_thread_init(NULL);
#endif
	mainloop = g_main_loop_new(NULL, FALSE);

	/* init daemon into a clean state first, then dbus and hw_abstraction last */
	usb_moded_init();
	if( !usb_moded_dbus_init() )
	{
		log_crit("dbus ipc init failed\n");
		goto EXIT;
	}
	if( !hwal_init() )
	{
		/* if hw_fallback is active we can live with a failed hwal_init */
		if(!hw_fallback)
		{
			log_crit("hwal init failed\n");
			goto EXIT;
		}
	}
#ifdef MEEGOLOCK
	start_devicelock_listener();
#endif /* MEEGOLOCK */

	/* signal handling */
	if( !sigpipe_init() )
	{
		log_crit("signal handler init failed\n");
		goto EXIT;
	}

#ifdef SYSTEMD
	/* Tell systemd that we have started up */
	if( systemd_notify ) 
	{
		log_debug("notifying systemd\n");
		sd_notify(0, "READY=1");
	}
#endif /* SYSTEMD */

        send_supported_modes_signal();

	if(hw_fallback)
	{
		log_warning("Forcing USB state to connected always. ASK mode non functional!\n");
		/* Since there will be no disconnect signals coming from hw the state should not change */
		set_usb_connected(TRUE);
	}

	/* init succesful, run main loop */
	result = EXIT_SUCCESS;  
	g_main_loop_run(mainloop);
EXIT:
	handle_exit();

	return result;
}
