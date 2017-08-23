/**
  @file usb_moded-config.c
 
  Copyright (C) 2010 Nokia Corporation. All rights reserved.
  Copyright (C) 2012-2015 Jolla. All rights reserved.

  @author: Philippe De Swert <philippe.de-swert@nokia.com>

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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>
#include <glob.h>
/*
#include <glib/gkeyfile.h>
#include <glib/gstdio.h>
*/
#include "usb_moded.h"
#include "usb_moded-config.h"
#include "usb_moded-config-private.h"
#include "usb_moded-log.h"
#include "usb_moded-modes.h"
#include "usb_moded-modesetting.h"

#ifdef USE_MER_SSU
# include "usb_moded-ssu.h"
#endif

static int get_conf_int(const gchar *entry, const gchar *key);
static char * get_conf_string(const gchar *entry, const gchar *key);
static char * get_kcmdline_string(const char *entry);

static int validate_ip(const char *ipadd)
{
  unsigned int b1, b2, b3, b4;
  unsigned char c;

  if (sscanf(ipadd, "%3u.%3u.%3u.%3u%c", &b1, &b2, &b3, &b4, &c) != 4)
        return(-1);

  if ((b1 | b2 | b3 | b4) > 255)
        return(-1);
  if (strspn(ipadd, "0123456789.") < strlen(ipadd))
        return(-1);
  /* all ok */
  return 0;
}

char *find_mounts(void)
{
  
  char *ret = NULL;

  ret = get_conf_string(FS_MOUNT_ENTRY, FS_MOUNT_KEY);
  if(ret == NULL)
  {
      ret = g_strdup(FS_MOUNT_DEFAULT);	  
      log_debug("Default mount = %s\n", ret);
  }
  return(ret);
}

int find_sync(void)
{

  return(get_conf_int(FS_SYNC_ENTRY, FS_SYNC_KEY));
}

char * find_alt_mount(void)
{
  return(get_conf_string(ALT_MOUNT_ENTRY, ALT_MOUNT_KEY));
}

char * find_udev_path(void)
{
  return(get_conf_string(UDEV_PATH_ENTRY, UDEV_PATH_KEY));
}

char * find_udev_subsystem(void)
{
  return(get_conf_string(UDEV_PATH_ENTRY, UDEV_SUBSYSTEM_KEY));
}

char * check_trigger(void)
{
  return(get_conf_string(TRIGGER_ENTRY, TRIGGER_PATH_KEY));
}

char * get_trigger_subsystem(void)
{
  return(get_conf_string(TRIGGER_ENTRY, TRIGGER_UDEV_SUBSYSTEM));
}

char * get_trigger_mode(void)
{
  return(get_conf_string(TRIGGER_ENTRY, TRIGGER_MODE_KEY));
}

char * get_trigger_property(void)
{
  return(get_conf_string(TRIGGER_ENTRY, TRIGGER_PROPERTY_KEY));
}

char * get_trigger_value(void)
{
  return(get_conf_string(TRIGGER_ENTRY, TRIGGER_PROPERTY_VALUE_KEY));
}

static char * get_network_ip(void)
{
  char * ip = get_kcmdline_string(NETWORK_IP_KEY);
  if (ip != NULL)
    if(!validate_ip(ip))
	return(ip);

  return(get_conf_string(NETWORK_ENTRY, NETWORK_IP_KEY));
}

static char * get_network_interface(void)
{
  return(get_conf_string(NETWORK_ENTRY, NETWORK_INTERFACE_KEY));
}

static char * get_network_gateway(void)
{
  char * gw = get_kcmdline_string(NETWORK_GATEWAY_KEY);
  if (gw != NULL)
    return(gw);

  return(get_conf_string(NETWORK_ENTRY, NETWORK_GATEWAY_KEY));
}

static char * get_network_netmask(void)
{
  char * netmask = get_kcmdline_string(NETWORK_NETMASK_KEY);
  if (netmask != NULL)
    return(netmask);

  return(get_conf_string(NETWORK_ENTRY, NETWORK_NETMASK_KEY));
}

static char * get_network_nat_interface(void)
{
  return(get_conf_string(NETWORK_ENTRY, NETWORK_NAT_INTERFACE_KEY));
}

/* create basic conffile with sensible defaults */
static void create_conf_file(void)
{
  GKeyFile *settingsfile;
  gchar *keyfile;
  int dir = 1;
  struct stat dir_stat;

  /* since this function can also be called when the dir exists we only create
     it if it is missing */
  if(stat(CONFIG_FILE_DIR, &dir_stat))
  {
	dir = mkdir(CONFIG_FILE_DIR, 0755);
	if(dir < 0)
	{
		log_warning("Could not create confdir, continuing without configuration!\n");
		/* no point in trying to generate the config file if the dir cannot be created */
		return;
	}
  }

  settingsfile = g_key_file_new();

  g_key_file_set_string(settingsfile, MODE_SETTING_ENTRY, MODE_SETTING_KEY, MODE_DEVELOPER );
  keyfile = g_key_file_to_data (settingsfile, NULL, NULL);
  if(g_file_set_contents(FS_MOUNT_CONFIG_FILE, keyfile, -1, NULL) == 0)
	log_debug("Conffile creation failed. Continuing without configuration!\n");
  free(keyfile);
  g_key_file_free(settingsfile);
}

static int get_conf_int(const gchar *entry, const gchar *key)
{
  GKeyFile *settingsfile;
  gboolean test = FALSE;
  gchar **keys, **origkeys;
  int ret = 0;

  settingsfile = g_key_file_new();
  test = g_key_file_load_from_file(settingsfile, FS_MOUNT_CONFIG_FILE, G_KEY_FILE_NONE, NULL);
  if(!test)
  {
      log_debug("no conffile, Creating\n");
      create_conf_file();
  }
  keys = g_key_file_get_keys (settingsfile, entry, NULL, NULL);
  if(keys == NULL)
        return ret;
  origkeys = keys;
  while (*keys != NULL)
  {
        if(!strcmp(*keys, key))
        {
                ret = g_key_file_get_integer(settingsfile, entry, *keys, NULL);
                log_debug("%s key value  = %d\n", key, ret);
        }
        keys++;
  }
  g_strfreev(origkeys);
  g_key_file_free(settingsfile);
  return(ret);

}

static char * get_conf_string(const gchar *entry, const gchar *key)
{
  GKeyFile *settingsfile;
  gboolean test = FALSE;
  gchar **keys, **origkeys, *tmp_char = NULL;
  settingsfile = g_key_file_new();
  test = g_key_file_load_from_file(settingsfile, FS_MOUNT_CONFIG_FILE, G_KEY_FILE_NONE, NULL);
  if(!test)
  {
      log_debug("No conffile. Creating\n");
      create_conf_file();
      /* should succeed now */
      g_key_file_load_from_file(settingsfile, FS_MOUNT_CONFIG_FILE, G_KEY_FILE_NONE, NULL);
  }
  keys = g_key_file_get_keys (settingsfile, entry, NULL, NULL);
  if(keys == NULL)
        goto end;
  origkeys = keys;
  while (*keys != NULL)
  {
        if(!strcmp(*keys, key))
        {
                tmp_char = g_key_file_get_string(settingsfile, entry, *keys, NULL);
                if(tmp_char)
                {
                        log_debug("key %s value  = %s\n", key, tmp_char);
                }
        }
        keys++;
  }
  g_strfreev(origkeys);
end:
  g_key_file_free(settingsfile);
  return(tmp_char);

}

static char * get_kcmdline_string(const char *entry)
{
  int fd;
  char cmdLine[1024];
  char *ret = NULL;
  int len;
  gint argc = 0;
  gchar **argv = NULL;
  gchar **arg_tokens = NULL, **network_tokens = NULL;
  GError *optErr = NULL;
  int i;

  if ((fd = open("/proc/cmdline", O_RDONLY)) < 0)
  {
    log_debug("could not read /proc/cmdline");
    return(ret);
  }

  len = read(fd, cmdLine, sizeof(cmdLine) - 1);
  close(fd);

  if (len <= 0)
  {
    log_debug("kernel command line was empty");
    return(ret);
  }

  cmdLine[len] = '\0';

  /* we're looking for a piece of the kernel command line matching this:
    ip=192.168.3.100::192.168.3.1:255.255.255.0::usb0:on */
  if (!g_shell_parse_argv(cmdLine, &argc, &argv, &optErr)) 
  {
    g_error_free(optErr);
    return(ret);
  }

  /* find the ip token */
  for (i=0; i < argc; i++) 
  {
    arg_tokens = g_strsplit(argv[i], "=", 2);
    if (!g_ascii_strcasecmp(arg_tokens[0], "usb_moded_ip"))
    {
      network_tokens = g_strsplit(arg_tokens[1], ":", 7);
      /* check if it is for the usb or rndis interface */
      if(g_strrstr(network_tokens[5], "usb")|| (g_strrstr(network_tokens[5], "rndis")))
      {
	if(!strcmp(entry, NETWORK_IP_KEY))
	{
		g_free(ret), ret = g_strdup(network_tokens[0]);
		log_debug("Command line ip = %s\n", ret);
	}
	if(!strcmp(entry, NETWORK_GATEWAY_KEY))
	{
		/* gateway might be empty, so we do not want to return an empty string */
		if(strlen(network_tokens[2]) > 2)
		{
		  g_free(ret), ret = g_strdup(network_tokens[2]);
		  log_debug("Command line gateway = %s\n", ret);
		}
	}
	if(!strcmp(entry, NETWORK_NETMASK_KEY))
	{
		g_free(ret), ret = g_strdup(network_tokens[3]);
		log_debug("Command line netmask = %s\n", ret);
	}
      } 
    }
    g_strfreev(arg_tokens);
  }
  g_strfreev(argv);
  g_strfreev(network_tokens);

  return(ret);
}

char * get_mode_setting(void)
{
  char * mode = get_kcmdline_string(MODE_SETTING_KEY);
  if (mode != NULL)
    return(mode);

  return(get_conf_string(MODE_SETTING_ENTRY, MODE_SETTING_KEY));
}
/*
 *  @param settingsfile: already opened settingsfile we want to read an entry from
 *  @param entry: entry we want to read
 *  @param key: key value of the entry we want to read
 *  @new_value: potentially new value we want to compare against
 *
 *  @return: 0 when the old value is the same as the new one, 1 otherwise
 */
int config_value_changed(GKeyFile *settingsfile, const char *entry, const char *key, const char *new_value)
{
  char *old_value = g_key_file_get_string(settingsfile, entry, key, NULL);
  int changed = (g_strcmp0(old_value, new_value) != 0);
  g_free(old_value);
  return changed;
}

set_config_result_t set_config_setting(const char *entry, const char *key, const char *value)
{
  GKeyFile *settingsfile;
  gboolean test = FALSE;
  set_config_result_t ret = SET_CONFIG_ERROR;
  gchar *keyfile;

  settingsfile = g_key_file_new();
  test = g_key_file_load_from_file(settingsfile, FS_MOUNT_CONFIG_FILE, G_KEY_FILE_NONE, NULL);
  if(test)
  {
      if(!config_value_changed(settingsfile, entry, key, value))
      {
              g_key_file_free(settingsfile);
              return SET_CONFIG_UNCHANGED;
      }
  }
  else
  {
      log_debug("No conffile. Creating.\n");
      create_conf_file();
  }

  g_key_file_set_string(settingsfile, entry, key, value);
  keyfile = g_key_file_to_data (settingsfile, NULL, NULL); 
  /* free the settingsfile before writing things out to be sure 
     the contents will be correctly written to file afterwards.
     Just a precaution. */
  g_key_file_free(settingsfile);
  if (g_file_set_contents(FS_MOUNT_CONFIG_FILE, keyfile, -1, NULL))
      ret = SET_CONFIG_UPDATED;
  g_free(keyfile);
  
  return (ret);
}

set_config_result_t set_mode_setting(const char *mode)
{
  if (strcmp(mode, MODE_ASK) && valid_mode(mode))
    return SET_CONFIG_ERROR;
  return (set_config_setting(MODE_SETTING_ENTRY, MODE_SETTING_KEY, mode));
}

/* Builds the string used for hidden modes, when hide set to one builds the
   new string of hidden modes when adding one, otherwise it will remove one */
static char * make_modes_string(const char *key, const char *mode_name, int include)
{
  char     *modes_new = 0;
  char     *modes_old = 0;
  gchar   **modes_arr = 0;
  GString  *modes_tmp = 0;
  int i;

  /* Get current comma separated list of hidden modes */
  modes_old = get_conf_string(MODE_SETTING_ENTRY, key);
  if(!modes_old)
  {
    modes_old = g_strdup("");
  }

  modes_arr = g_strsplit(modes_old, ",", 0);

  modes_tmp = g_string_new(NULL);

  for(i = 0; modes_arr[i] != NULL; i++)
  {
    if(strlen(modes_arr[i]) == 0)
    {
      /* Skip any empty strings */
      continue;
    }

    if(!strcmp(modes_arr[i], mode_name))
    {
      /* When unhiding, just skip all matching entries */
      if(!include)
        continue;

      /* When hiding, keep the 1st match and ignore the rest */
      include = 0;
    }

    if(modes_tmp->len > 0)
      modes_tmp = g_string_append(modes_tmp, ",");
    modes_tmp = g_string_append(modes_tmp, modes_arr[i]);
  }

  if(include)
  {
    /* Adding a hidden mode and no matching entry was found */
    if(modes_tmp->len > 0)
      modes_tmp = g_string_append(modes_tmp, ",");
    modes_tmp = g_string_append(modes_tmp, mode_name);
  }

  modes_new = g_string_free(modes_tmp, FALSE), modes_tmp = 0;

  g_strfreev(modes_arr), modes_arr = 0;

  g_free(modes_old), modes_old = 0;

  return modes_new;
}

set_config_result_t set_hide_mode_setting(const char *mode)
{
  set_config_result_t ret = SET_CONFIG_UNCHANGED;

  char *hidden_modes = make_modes_string(MODE_HIDE_KEY, mode, 1);

  if( hidden_modes ) {
    ret = set_config_setting(MODE_SETTING_ENTRY, MODE_HIDE_KEY, hidden_modes);
  }

  if(ret == SET_CONFIG_UPDATED) {
      send_hidden_modes_signal();
      send_supported_modes_signal();
      send_available_modes_signal();
  }

  g_free(hidden_modes);

  return(ret);
}

set_config_result_t set_unhide_mode_setting(const char *mode)
{
  set_config_result_t ret = SET_CONFIG_UNCHANGED;

  char *hidden_modes = make_modes_string(MODE_HIDE_KEY, mode, 0);

  if( hidden_modes ) {
    ret = set_config_setting(MODE_SETTING_ENTRY, MODE_HIDE_KEY, hidden_modes);
  }

  if(ret == SET_CONFIG_UPDATED) {
      send_hidden_modes_signal();
      send_supported_modes_signal();
      send_available_modes_signal();
  }

  g_free(hidden_modes);

  return(ret);
}

set_config_result_t set_mode_whitelist(const char *whitelist)
{
  set_config_result_t ret = set_config_setting(MODE_SETTING_ENTRY, MODE_WHITELIST_KEY, whitelist);

  if(ret == SET_CONFIG_UPDATED) {
    char *mode_setting;
    const char *current_mode;

    mode_setting = get_mode_setting();
    if (strcmp(mode_setting, MODE_ASK) && valid_mode(mode_setting))
      set_mode_setting(MODE_ASK);
    g_free(mode_setting);

    current_mode = get_usb_mode();
    if (strcmp(current_mode, MODE_CHARGING_FALLBACK) && strcmp(current_mode, MODE_ASK) && valid_mode(current_mode)) {
      usb_moded_mode_cleanup(get_usb_module());
      set_usb_mode(MODE_CHARGING_FALLBACK);
    }

    usb_moded_send_whitelisted_modes_signal(whitelist);
    send_available_modes_signal();
  }

  return ret;
}

set_config_result_t set_mode_in_whitelist(const char *mode, int allowed)
{
  set_config_result_t ret = SET_CONFIG_UNCHANGED;

  char *whitelist = make_modes_string(MODE_WHITELIST_KEY, mode, allowed);

  if (whitelist) {
    ret = set_mode_whitelist(whitelist);
  }

  g_free(whitelist);

  return(ret);
}

/*
 * @param config : the key to be set
 * @param setting : The value to be set
 */
set_config_result_t set_network_setting(const char *config, const char *setting)
{
  GKeyFile *settingsfile;
  gboolean test = FALSE;
  gchar *keyfile;

  if(!strcmp(config, NETWORK_IP_KEY) || !strcmp(config, NETWORK_GATEWAY_KEY))
	if(validate_ip(setting) != 0)
		return SET_CONFIG_ERROR;

  settingsfile = g_key_file_new();
  test = g_key_file_load_from_file(settingsfile, FS_MOUNT_CONFIG_FILE, G_KEY_FILE_NONE, NULL);

  if(!strcmp(config, NETWORK_IP_KEY) || !strcmp(config, NETWORK_INTERFACE_KEY) || !strcmp(config, NETWORK_GATEWAY_KEY))
  {
	set_config_result_t ret = SET_CONFIG_ERROR;
	if (test)
	{
		if(!config_value_changed(settingsfile, NETWORK_ENTRY, config, setting))
		{
			g_key_file_free(settingsfile);
			return SET_CONFIG_UNCHANGED;
		}
	}
	else
	{
		log_debug("No conffile. Creating.\n");
		create_conf_file();
	}

	g_key_file_set_string(settingsfile, NETWORK_ENTRY, config, setting);
  	keyfile = g_key_file_to_data (settingsfile, NULL, NULL); 
  	/* free the settingsfile before writing things out to be sure 
     	the contents will be correctly written to file afterwards.
     	Just a precaution. */
  	g_key_file_free(settingsfile);
	if (g_file_set_contents(FS_MOUNT_CONFIG_FILE, keyfile, -1, NULL))
		ret = SET_CONFIG_UPDATED;
	free(keyfile);
	return ret;
  }
  else
  {
	g_key_file_free(settingsfile);
	return SET_CONFIG_ERROR;
  }
}

char * get_network_setting(const char *config)
{
  char * ret = 0;
  struct mode_list_elem *data;

  if(!strcmp(config, NETWORK_IP_KEY))
  {
	ret = get_network_ip();
	if(!ret)
		ret = strdup("192.168.2.15");
  }
  else if(!strcmp(config, NETWORK_INTERFACE_KEY))
  {

	/* check main configuration before using
	the information from the specific mode */
	ret = get_network_interface();

	if(ret)
		goto end;
	/* no interface override specified, let's use the one
	from the mode config */
	data = get_usb_mode_data();
	if(data)
	{
		if(data->network_interface)
		{
			ret = strdup(data->network_interface);
			goto end;
		}
	}
	ret = strdup("usb0");
  }
  else if(!strcmp(config, NETWORK_GATEWAY_KEY))
	return(get_network_gateway());
  else if(!strcmp(config, NETWORK_NETMASK_KEY))
  {
	ret = get_network_netmask();
	if(!ret)
		ret = strdup("255.255.255.0");
  }
  else if(!strcmp(config, NETWORK_NAT_INTERFACE_KEY))
	return(get_network_nat_interface());
  else
	/* no matching keys, return error */
	return(NULL);
end:
   return(ret);
}

/**
 * Merge value from one keyfile to another
 *
 * Existing values will be overridden
 *
 * @param dest keyfile to modify
 * @param srce keyfile to merge from
 * @param grp  value group to merge
 * @param key  value key to merge
 */
static void merge_key(GKeyFile *dest, GKeyFile *srce,
			const char *grp, const char *key)
{
	gchar *val = g_key_file_get_value(srce, grp, key, 0);
	if( val ) {
		log_debug("[%s] %s = %s", grp, key, val);
		g_key_file_set_value(dest, grp, key, val);
		g_free(val);
	}
}

/**
 * Merge group of values from one keyfile to another
 *
 * @param dest keyfile to modify
 * @param srce keyfile to merge from
 * @param grp  value group to merge
 */
static void merge_group(GKeyFile *dest, GKeyFile *srce,
			const char *grp)
{
	gchar **key = g_key_file_get_keys(srce, grp, 0, 0);
	if( key ) {
		for( size_t i = 0; key[i]; ++i )
			merge_key(dest, srce, grp, key[i]);
		g_strfreev(key);
	}
}

/**
 * Merge all groups and values from one keyfile to another
 *
 * @param dest keyfile to modify
 * @param srce keyfile to merge from
 */
static void merge_file(GKeyFile *dest, GKeyFile *srce)
{
	gchar **grp = g_key_file_get_groups(srce, 0);

	if( grp ) {
		for( size_t i = 0; grp[i]; ++i )
			merge_group(dest, srce, grp[i]);
		g_strfreev(grp);
	}
}

/**
 * Callback function for logging errors within glob()
 *
 * @param path path to file/dir where error occurred
 * @param err  errno that occurred
 *
 * @return 0 (= do not stop glob)
 */
static int glob_error_cb(const char *path, int err)
{
	log_debug("%s: glob: %s", path, g_strerror(err));
	return 0;
}

/**
 * Read *.ini files on CONFIG_FILE_DIR in the order of [0-9][A-Z][a-z]
 *
 * @return the in memory value-pair file.
 */
static GKeyFile *read_ini_files(void)
{
	static const char pattern[] = CONFIG_FILE_DIR"/*.ini";

	GKeyFile *ini = g_key_file_new();
	glob_t gb;

	memset(&gb, 0, sizeof gb);

	if( glob(pattern, 0, glob_error_cb, &gb) != 0 ) {
		log_debug("no configuration ini-files found");
		g_key_file_free(ini);
		ini = NULL;
		goto exit;
	}

	for( size_t i = 0; i < gb.gl_pathc; ++i ) {
		const char *path = gb.gl_pathv[i];
		GError *err	= 0;
		GKeyFile *tmp = g_key_file_new();

		if( !g_key_file_load_from_file(tmp, path, 0, &err) ) {
			log_debug("%s: can't load: %s", path, err->message);
		} else {
			log_debug("processing %s ...", path);
			merge_file(ini, tmp);
		}
		g_clear_error(&err);
		g_key_file_free(tmp);
	}
exit:
	globfree(&gb);
	return ini;
}

/**
 * Read the *.ini files and create/overwrite FS_MOUNT_CONFIG_FILE with
 * the merged data.
 *
 * @return 0 on failure
 */
int conf_file_merge(void)
{
  GString *keyfile_string = NULL;
  GKeyFile *settingsfile,*tempfile;
  int ret = 0;

	settingsfile = read_ini_files();
	if (!settingsfile)
	{
		log_debug("No configuration. Creating defaults.");
		create_conf_file();
		/* There was no configuration so no info to be merged */
		return ret;
	}

	tempfile = g_key_file_new();
	if (g_key_file_load_from_file(tempfile, FS_MOUNT_CONFIG_FILE,
				G_KEY_FILE_NONE,NULL)) {
		if (!g_strcmp0(g_key_file_to_data(settingsfile, NULL, NULL),
				g_key_file_to_data(tempfile, NULL, NULL)))
			goto out;
	}

	log_debug("Merging configuration");
	keyfile_string = g_string_new(NULL);
	keyfile_string = g_string_append(keyfile_string,
					g_key_file_to_data(settingsfile,
							NULL, NULL));
	if (keyfile_string) {
		ret = !g_file_set_contents(FS_MOUNT_CONFIG_FILE,
						keyfile_string->str,-1, NULL);
		g_string_free(keyfile_string, TRUE);
	}
out:
	g_key_file_free(tempfile);
	g_key_file_free(settingsfile);
	return ret;
}

char * get_android_manufacturer(void)
{
#ifdef USE_MER_SSU
  /* If SSU can provide manufacturer name, use it. Otherwise fall
   * back to using the name specified in configuration files. */
  char *ssu_name = ssu_get_manufacturer_name();
  if( ssu_name )
  {
    return ssu_name;
  }
#endif

  return get_conf_string(ANDROID_ENTRY, ANDROID_MANUFACTURER_KEY);
}

char * get_android_vendor_id(void)
{
  return(get_conf_string(ANDROID_ENTRY, ANDROID_VENDOR_ID_KEY));
}

char * get_android_product(void)
{
#ifdef USE_MER_SSU
  /* If SSU can provide device model name, use it. Otherwise fall
   * back to using the name specified in configuration files. */
  char *ssu_name = ssu_get_product_name();
  if( ssu_name )
  {
    return ssu_name;
  }
#endif

  return get_conf_string(ANDROID_ENTRY, ANDROID_PRODUCT_KEY);
}

char * get_android_product_id(void)
{
  return(get_conf_string(ANDROID_ENTRY, ANDROID_PRODUCT_ID_KEY));
}

char * get_hidden_modes(void)
{
  return(get_conf_string(MODE_SETTING_ENTRY, MODE_HIDE_KEY));
}
char * get_mode_whitelist(void)
{
  return(get_conf_string(MODE_SETTING_ENTRY, MODE_WHITELIST_KEY));
}

int check_android_section(void)
{
  GKeyFile *settingsfile;
  gboolean test = FALSE;
  gchar **keys;
  int ret = 1;

  settingsfile = g_key_file_new();
  test = g_key_file_load_from_file(settingsfile, FS_MOUNT_CONFIG_FILE, G_KEY_FILE_NONE, NULL);
  if(!test)
  {
	ret = 0;
	goto cleanup;
  }
  keys = g_key_file_get_keys (settingsfile, ANDROID_ENTRY, NULL, NULL);
  if(keys == NULL)
  {  
        ret =  0;
	goto cleanup;
  }

  g_strfreev(keys);
cleanup:
  g_key_file_free(settingsfile);
  return(ret);
}

int is_roaming_not_allowed(void)
{
  return(get_conf_int(NETWORK_ENTRY, NO_ROAMING_KEY));
}
