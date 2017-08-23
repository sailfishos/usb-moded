/**
  @file usb_moded.c

  Copyright (C) 2010 Nokia Corporation. All rights reserved.
  Copyright (C) 2012-2016 Jolla. All rights reserved.

  @author: Philippe De Swert <philippe.de-swert@nokia.com>
  @author: Philippe De Swert <philippe.deswert@jollamobile.com>
  @author: Simo Piiroinen <simo.piiroinen@jollamobile.com>

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
#include "usb_moded-systemd.h"

#ifdef MEEGOLOCK
#include "usb_moded-dsme.h"
#endif

/* Wakelogging is noisy, do not log it by default */
#ifndef  VERBOSE_WAKELOCKING
# define VERBOSE_WAKELOCKING 0
#endif

/* global definitions */

static int usb_moded_exitcode = EXIT_FAILURE;
static GMainLoop *usb_moded_mainloop = NULL;

gboolean rescue_mode = FALSE;
gboolean diag_mode = FALSE;
gboolean hw_fallback = FALSE;
gboolean android_broken_usb = FALSE;
gboolean android_ignore_udev_events = FALSE;
gboolean android_ignore_next_udev_disconnect_event = FALSE;
#ifdef SYSTEMD
static gboolean systemd_notify = FALSE;
#endif

/** Default allowed cable detection delay
 *
 * To comply with USB standards, the delay should be
 * less than 2 seconds to ensure timely enumeration.
 *
 * Any value <= zero means no delay.
 */
#define CABLE_CONNECTION_DELAY_DEFAULT 0

/** Maximum allowed cable detection delay
 *
 * Must be shorter than initial probing delay expected by
 * dsme (currently 5 seconds) to avoid reboot loops in
 * act dead mode.
 *
 * And shorter than USB_MODED_SUSPEND_DELAY_DEFAULT_MS to
 * allow the timer to trigger also in display off scenarios.
 */

#define CABLE_CONNECTION_DELAY_MAXIMUM 4000

/** Currently allowed cable detection delay
 */
int cable_connection_delay = CABLE_CONNECTION_DELAY_DEFAULT;

/** Helper for setting allowed cable detection delay
 *
 * Used for implementing --max-cable-delay=<ms> option.
 */
static void set_cable_connection_delay(int delay_ms)
{
	if( delay_ms < CABLE_CONNECTION_DELAY_MAXIMUM )
		cable_connection_delay = delay_ms;
	else {
		cable_connection_delay = CABLE_CONNECTION_DELAY_MAXIMUM;
		log_warning("using maximum connection delay: %d ms",
			    cable_connection_delay);
	}
}

struct usb_mode current_mode;
guint charging_timeout = 0;
static GList *modelist;

/* static helper functions */
static gboolean set_disconnected(gpointer data);
static gboolean set_disconnected_silent(gpointer data);
static void usb_moded_init(void);
static gboolean charging_fallback(gpointer data);
static void usage(void);
static bool init_done_p(void);

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
  usb_moded_sleep(1);
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
/** Check if we can/should leave charging fallback mode
 */
void
rethink_usb_charging_fallback(void)
{
    /* Cable must be connected and suitable usb-mode mode
     * selected for any of this to apply.
     */
    if( !get_usb_connection_state() )
        goto EXIT;

    const char *usb_mode = get_usb_mode();

    if( strcmp(usb_mode, MODE_UNDEFINED) &&
        strcmp(usb_mode, MODE_CHARGING_FALLBACK) )
        goto EXIT;

    /* If device locking is supported, the device must be in
     * unlocked state (or rescue mode must be active).
     */
#ifdef MEEGOLOCK
    if( !usb_moded_get_export_permission() && !rescue_mode ) {
        log_notice("device is locked; stay in %s", usb_mode);
        goto EXIT;
    }
#endif

    /* Device must be in USER state or in rescue mode
     */
#ifdef MEEGOLOCK
    if( !is_in_user_state() && !rescue_mode ) {
        log_notice("device is not in USER mode; stay in %s", usb_mode);
        goto EXIT;
    }
#endif

    log_debug("attempt to leave %s", usb_mode);
    set_usb_connected_state();

EXIT:
    return;
}

/** set the chosen usb state
 *
 */
void set_usb_connected_state(void)
{	
  char *mode_to_set;

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
  gboolean can_export = usb_moded_get_export_permission();
  /* We check also if the device is in user state or not.
     If not we do not export anything. We presume ACT_DEAD charging */
  if(mode_to_set && can_export && is_in_user_state())
#else
  if(mode_to_set)
#endif /* MEEGOLOCK */
  {
	/* This is safe to do here as the starting condition is
	MODE_UNDEFINED, and having a devicelock being activated when
	a mode is set will not interrupt it */
	if(!strcmp(mode_to_set, current_mode.mode))
		goto end;

	if (!strcmp(MODE_ASK, mode_to_set))
	{
		/*! If charging mode is the only available selection, don't ask
 		 just select it */
		gchar *available_modes = get_mode_list(AVAILABLE_MODES_LIST);
		if (!strcmp(MODE_CHARGING, available_modes)) {
			gchar *temp = mode_to_set;
			mode_to_set = available_modes;
			available_modes = temp;
		}
		g_free(available_modes);
	}

	if(!strcmp(MODE_ASK, mode_to_set))
	{
		/* send signal, mode will be set when the dialog service calls
	  	 the set_mode method call.
	 	*/
		usb_moded_send_signal(USB_CONNECTED_DIALOG_SHOW);
		/* fallback to charging mode after 3 seconds */
		if( charging_timeout )
			g_source_remove(charging_timeout);
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
	gboolean can_export = usb_moded_get_export_permission();

	if(!can_export && !rescue_mode)
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

/* check if a mode is in a list */
static bool mode_in_list(const char *mode, char * const *modes)
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
int valid_mode(const char *mode)
{
  int valid = 1;
  /* MODE_ASK, MODE_CHARGER and MODE_CHARGING_FALLBACK are not modes that are settable seen their special 'internal' status 
     so we only check the modes that are announed outside. Only exception is the built in MODE_CHARGING */
  if(!strcmp(MODE_CHARGING, mode))
        valid = 0;
  else
  {
    char *whitelist;
    gchar **whitelist_split = NULL;

    whitelist = get_mode_whitelist();
    if (whitelist)
    {
      whitelist_split = g_strsplit(whitelist, ",", 0);
      g_free(whitelist);
    }

    /* check dynamic modes */
    if(modelist)
    {
      GList *iter;

      for( iter = modelist; iter; iter = g_list_next(iter) )
      {
        struct mode_list_elem *data = iter->data;
        if(!strcmp(mode, data->mode_name))
        {
          if (!whitelist_split || mode_in_list(data->mode_name, whitelist_split))
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
gchar *get_mode_list(mode_list_type_t type)
{
  GString *modelist_str;

  modelist_str = g_string_new(NULL);

  if(!diag_mode)
  {
    /* check dynamic modes */
    if(modelist)
    {
      GList *iter;
      char *hidden_modes_list, *whitelist;
      gchar **hidden_mode_split = NULL, **whitelist_split = NULL;

      hidden_modes_list = get_hidden_modes();
      if(hidden_modes_list)
      {
        hidden_mode_split = g_strsplit(hidden_modes_list, ",", 0);
        g_free(hidden_modes_list);
      }

      if (type == AVAILABLE_MODES_LIST)
      {
        whitelist = get_mode_whitelist();
        if (whitelist)
        {
          whitelist_split = g_strsplit(whitelist, ",", 0);
          g_free(whitelist);
        }
      }

      for( iter = modelist; iter; iter = g_list_next(iter) )
      {
        struct mode_list_elem *data = iter->data;

        /* skip items in the hidden list */
        if (mode_in_list(data->mode_name, hidden_mode_split))
          continue;

        /* if there is a whitelist skip items not in the list */
        if (whitelist_split && !mode_in_list(data->mode_name, whitelist_split))
          continue;

	modelist_str = g_string_append(modelist_str, data->mode_name);
	modelist_str = g_string_append(modelist_str, ", ");
      }

      g_strfreev(hidden_mode_split);
      g_strfreev(whitelist_split);
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
#endif

  /* always read dyn modes even if appsync is not used */
  modelist = read_mode_list(diag_mode);

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

/** Release resources allocated by usb_moded_init()
 */
static void usb_moded_cleanup(void)
{
    /* Undo usb_moded_module_ctx_init() */
    usb_moded_module_ctx_cleanup();

    /* Undo trigger_init() */
    trigger_stop();

    /* Undo read_mode_list() */
    free_mode_list(modelist);

#ifdef APP_SYNC
    /* Undo readlist() */
    free_appsync_list();
#endif

    /* Release dynamic memory */
    free(current_mode.module),
        current_mode.module = 0;

    free(current_mode.mode),
        current_mode.mode = 0;
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

static void sigint_handler(int signum)
{
    log_debug("handle signal: %s\n", strsignal(signum));

    if( signum == SIGTERM )
    {
        /* Assume: Stopped by init process */
        usb_moded_stop(EXIT_SUCCESS);
    }
    else if( signum == SIGHUP )
    {
        struct mode_list_elem *data;

        /* clean up current mode */
        data = get_usb_mode_data();
        set_disconnected_silent(data);
        /* clear existing data to be sure */
        set_usb_mode_data(NULL);
        /* free and read in modelist again */
        free_mode_list(modelist);

        modelist = read_mode_list(diag_mode);

        send_supported_modes_signal();
        send_available_modes_signal();
    }
    else
    {
        usb_moded_stop(EXIT_FAILURE);
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
                  "  -l,  --log-line-info \t\tlog to stderr and show origin of logging\n"
                  "  -D,  --debug	  \t\tturn on debug printing\n"
		  "  -d,  --diag	  \t\tturn on diag mode\n"
                  "  -h,  --help          \t\tdisplay this help and exit\n"
		  "  -r,  --rescue	  \t\trescue mode\n"
#ifdef SYSTEMD
		  "  -n,  --systemd       \t\tnotify systemd when started up\n"
#endif
                  "  -v,  --version       \t\toutput version information and exit\n"
                  "  -m,  --max-cable-delay=<ms>\tmaximum delay before accepting cable connection\n"
                  "\n");
}

void send_supported_modes_signal(void)
{
    /* Send supported modes signal */
    gchar *mode_list = get_mode_list(SUPPORTED_MODES_LIST);
    usb_moded_send_supported_modes_signal(mode_list);
    g_free(mode_list);
}

void send_available_modes_signal(void)
{
    gchar *mode_list = get_mode_list(AVAILABLE_MODES_LIST);
    usb_moded_send_available_modes_signal(mode_list);
    g_free(mode_list);
}

void send_hidden_modes_signal(void)
{
    /* Send hidden modes signal */
    gchar *mode_list = get_hidden_modes();
    if(mode_list) {
        usb_moded_send_hidden_modes_signal(mode_list);
        g_free(mode_list);
    }
}

void send_whitelisted_modes_signal(void)
{
    gchar *mode_list = get_mode_whitelist();
    if(mode_list) {
        usb_moded_send_whitelisted_modes_signal(mode_list);
        g_free(mode_list);
    }
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

/** Write string to already existing sysfs file
 *
 * Note: Attempts to write to nonexisting files are silently ignored.
 *
 * @param path Where to write
 * @param text What to write
 */
static void write_to_sysfs_file(const char *path, const char *text)
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

/** Acquire wakelock via sysfs
 *
 * Wakelock must be released via release_wakelock().
 *
 * Automatically terminating wakelock is used, so that we
 * do not block suspend  indefinately in case usb_moded
 * gets stuck or crashes.
 *
 * Note: The name should be unique within the system.
 *
 * @param wakelock_name Wake lock to be acquired
 */
void acquire_wakelock(const char *wakelock_name)
{
	char buff[256];
	snprintf(buff, sizeof buff, "%s %lld",
		 wakelock_name,
		 USB_MODED_SUSPEND_DELAY_MAXIMUM_MS * 1000000LL);
	write_to_sysfs_file("/sys/power/wake_lock", buff);

#if VERBOSE_WAKELOCKING
	log_debug("acquire_wakelock %s", wakelock_name);
#endif
}

/** Release wakelock via sysfs
 *
 * @param wakelock_name Wake lock to be released
 */
void release_wakelock(const char *wakelock_name)
{
#if VERBOSE_WAKELOCKING
	log_debug("release_wakelock %s", wakelock_name);
#endif

	write_to_sysfs_file("/sys/power/wake_unlock", wakelock_name);
}

/** Flag for: USB_MODED_WAKELOCK_STATE_CHANGE has been acquired */
static bool blocking_suspend = false;

/** Timer for releasing USB_MODED_WAKELOCK_STATE_CHANGE */
static guint allow_suspend_id = 0;

/** Release wakelock acquired via delay_suspend()
 *
 * Meant to be called on usb-moded exit so that wakelocks
 * are not left behind.
 */
void allow_suspend(void)
{
	if( allow_suspend_id ) {
		g_source_remove(allow_suspend_id),
			allow_suspend_id = 0;
	}

	if( blocking_suspend ) {
		blocking_suspend = false;
		release_wakelock(USB_MODED_WAKELOCK_STATE_CHANGE);
	}
}

/** Timer callback for releasing wakelock acquired via delay_suspend()
 *
 * @param aptr callback argument (not used)
 */
static gboolean allow_suspend_cb(gpointer aptr)
{
	(void)aptr;

	allow_suspend_id = 0;

	allow_suspend();

	return FALSE;
}

/** Block suspend briefly
 *
 * Meant to be called in situations usb activity might have woken
 * up the device (cable connect while display is off), or could
 * allow device to suspend (cable disconnect while display is off).
 *
 * Allows usb moded some time to finish asynchronous activity and
 * other processes to receive and process state changes broadcast
 * by usb-moded.
 */
void delay_suspend(void)
{
	/* Use of automatically terminating wakelocks also means we need
	 * to renew the wakelock when extending the suspend delay. */
	acquire_wakelock(USB_MODED_WAKELOCK_STATE_CHANGE);

	blocking_suspend = true;

	if( allow_suspend_id )
		g_source_remove(allow_suspend_id);

	allow_suspend_id = g_timeout_add(USB_MODED_SUSPEND_DELAY_DEFAULT_MS,
					 allow_suspend_cb, 0);
}

/** Check if system has already been successfully booted up
 *
 * @return true if init-done has been reached, or false otherwise
 */
static bool init_done_p(void)
{
	return access("/run/systemd/boot-status/init-done", F_OK) == 0;
}

/** Request orderly exit from mainloop
 */
void usb_moded_stop(int exitcode)
{
	/* In case multiple exit request get done, retain the
	 * highest exit code used. */
	if( usb_moded_exitcode < exitcode )
		usb_moded_exitcode = exitcode;

	/* If there is no mainloop to exit, terminate immediately */
	if( !usb_moded_mainloop )
	{
		log_warning("exit requested outside mainloop; exit(%d) now",
			    usb_moded_exitcode);
		exit(usb_moded_exitcode);
	}

	log_debug("stopping usb-moded mainloop");
	g_main_loop_quit(usb_moded_mainloop);
}

/** Wrapper to give visibility to blocking system() calls usb-moded is making
 */
int
usb_moded_system_(const char *file, int line, const char *func,
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
usb_moded_popen_(const char *file, int line, const char *func,
		 const char *command, const char *type)
{
	log_debug("EXEC %s; from %s:%d: %s()",
		  command, file, line, func);

	return popen(command, type);
}

/** Wrapper to give visibility to blocking sleeps usb-moded is making
 */
void
usb_moded_usleep_(const char *file, int line, const char *func,
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

	do { } while( nanosleep(&ts, &ts) == -1 && errno != EINTR );
}

int main(int argc, char* argv[])
{
        int opt = 0, opt_idx = 0;

	struct option const options[] = {
                { "android_usb_broken", no_argument, 0, 'a' },
                { "android_usb_broken_udev_events", no_argument, 0, 'i' },
                { "fallback", no_argument, 0, 'd' },
                { "force-syslog", no_argument, 0, 's' },
                { "force-stderr", no_argument, 0, 'T' },
                { "log-line-info", no_argument, 0, 'l' },
                { "debug", no_argument, 0, 'D' },
                { "diag", no_argument, 0, 'd' },
                { "help", no_argument, 0, 'h' },
		{ "rescue", no_argument, 0, 'r' },
		{ "systemd", no_argument, 0, 'n' },
                { "version", no_argument, 0, 'v' },
                { "max-cable-delay", required_argument, 0, 'm' },
                { 0, 0, 0, 0 }
        };

	log_init();
	log_set_name(basename(*argv));

	/* - - - - - - - - - - - - - - - - - - - *
	 * OPTIONS
	 * - - - - - - - - - - - - - - - - - - - */

	 /* Parse the command-line options */
        while ((opt = getopt_long(argc, argv, "aifsTlDdhrnvm:", options, &opt_idx)) != -1)
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
                        	log_set_type(LOG_TO_SYSLOG);
                        	break;

                	case 'T':
                        	log_set_type(LOG_TO_STDERR);
                        	break;

                	case 'D':
                        	log_set_level(LOG_DEBUG);
                        	break;

			case 'l':
				log_set_type(LOG_TO_STDERR);
				log_set_lineinfo(true);
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

			case 'm':
				set_cable_connection_delay(strtol(optarg, 0, 0));
				break;

	                default:
        	                usage();
				exit(0);
                }
        }

	fprintf(stderr, "usb_moded %s starting\n", VERSION);
	fflush(stderr);

	/* - - - - - - - - - - - - - - - - - - - *
	 * INITIALIZE
	 * - - - - - - - - - - - - - - - - - - - */

	/* silence usb_moded_system() calls */
	if( log_get_type() != LOG_TO_STDERR && log_get_level() != LOG_DEBUG )
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

	/* Must be the 1st libdbus call that is made */
	dbus_threads_init_default();

	/* signal handling */
	if( !sigpipe_init() )
	{
		log_crit("signal handler init failed\n");
		goto EXIT;
	}

	if (rescue_mode && init_done_p())
	{
		rescue_mode = FALSE;
		log_warning("init done passed; rescue mode ignored");
	}

	/* Connect to SystemBus */
	if( !usb_moded_dbus_init_connection() )
	{
		log_crit("dbus systembus connection failed\n");
		goto EXIT;
	}

	/* Start DBus trackers that do async initialization
	 * so that initial method calls are on the way while
	 * we do initialization actions that might block. */

	/* DSME listener maintains in-user-mode state and is relevant
	 * only when MEEGOLOCK configure option has been chosen. */
#ifdef MEEGOLOCK
	if( !dsme_listener_start() ) {
		log_crit("dsme tracking could not be started");
		goto EXIT;
	}
#endif
	/* Devicelock listener maintains devicelock state and is relevant
	 * only when MEEGOLOCK configure option has been chosen. */
#ifdef MEEGOLOCK
	if( !start_devicelock_listener() ) {
		log_crit("devicelock tracking could not be started");
		goto EXIT;
	}
#endif

	/* Set daemon config/state data to sane state */
	usb_moded_mode_init();
	usb_moded_init();

	/* Allos making systemd control ipc */
	if( !systemd_control_start() ) {
		log_crit("systemd control could not be started");
		goto EXIT;
	}

	/* If usb-moded happens to crash, it could leave appsync processes
	 * running. To make sure things are in the order expected by usb-moded
	 * force stopping of appsync processes during usb-moded startup.
	 *
	 * The exception is: When usb-moded starts as a part of bootup. Then
	 * we can be relatively sure that usb-moded has not been running yet
	 * and therefore no appsync processes have been started and we can
	 * skip the blocking ipc required to stop the appsync systemd units. */
#ifdef APP_SYNC
	if( init_done_p() )
	{
		log_warning("usb-moded started after init-done; "
			    "forcing appsync stop");
		appsync_stop(TRUE);
	}
#endif

	/* Claim D-Bus service name before proceeding with things that
	 * could result in dbus signals from usb-moded interfaces to
	 * be broadcast */
	if( !usb_moded_dbus_init_service() )
	{
		log_crit("usb-moded dbus service init failed\n");
		goto EXIT;
	}

	/* Initialize udev listener. Can cause mode changes.
	 *
	 * Failing here is allowed if '--fallback' commandline option is used. */
	if( !hwal_init() && !hw_fallback )
	{
		log_crit("hwal init failed\n");
		goto EXIT;
	}

	/* Broadcast supported / hidden modes */
	// TODO: should this happen before hwal_init()?
        send_supported_modes_signal();
        send_available_modes_signal();
        send_hidden_modes_signal();
        send_whitelisted_modes_signal();

	/* Act on '--fallback' commandline option */
	if(hw_fallback)
	{
		log_warning("Forcing USB state to connected always. ASK mode non functional!\n");
		/* Since there will be no disconnect signals coming from hw the state should not change */
		set_usb_connected(TRUE);
	}

	/* - - - - - - - - - - - - - - - - - - - *
	 * EXECUTE
	 * - - - - - - - - - - - - - - - - - - - */

	/* Tell systemd that we have started up */
#ifdef SYSTEMD
	if( systemd_notify )
	{
		log_debug("notifying systemd\n");
		sd_notify(0, "READY=1");
	}
#endif

	/* init succesful, run main loop */
	usb_moded_exitcode = EXIT_SUCCESS;
	usb_moded_mainloop = g_main_loop_new(NULL, FALSE);

	log_debug("enter usb-moded mainloop");
	g_main_loop_run(usb_moded_mainloop);
	log_debug("leave usb-moded mainloop");

	g_main_loop_unref(usb_moded_mainloop),
		usb_moded_mainloop = 0;

	/* - - - - - - - - - - - - - - - - - - - *
	 * CLEANUP
	 * - - - - - - - - - - - - - - - - - - - */
EXIT:
	/* Detach from SystemBus. Components that hold reference to the
	 * shared bus connection can still perform cleanup tasks, but new
	 * references can't be obtained anymore and usb-moded method call
	 * processing no longer occurs. */
	usb_moded_dbus_cleanup();

	/* Stop appsync processes that have been started by usb-moded */
#ifdef APP_SYNC
	appsync_stop(FALSE);
#endif

	/* Deny making systemd control ipc */
	systemd_control_stop();

	/* Stop tracking devicelock status */
#ifdef MEEGOLOCK
	stop_devicelock_listener();
#endif
	/* Stop tracking device state */
#ifdef MEEGOLOCK
	dsme_listener_stop();
#endif

	/* Stop udev listener */
	hwal_cleanup();

	/* Release dynamically allocated config/state data */
	usb_moded_cleanup();
	usb_moded_mode_quit();

	/* Detach from SessionBus connection used for APP_SYNC_DBUS.
	 *
	 * Can be handled separately from SystemBus side wind down. */
#ifdef APP_SYNC
# ifdef APP_SYNC_DBUS
	usb_moded_appsync_cleanup();
# endif
#endif

	/* Must be done just before exit to make sure no more wakelocks
	 * are taken and left behind on exit path */
	allow_suspend();

	log_debug("usb-moded return from main, with exit code %d",
		  usb_moded_exitcode);
	return usb_moded_exitcode;
}
