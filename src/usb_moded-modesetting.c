/**
  @file usb_moded-modesetting.c
 
  Copyright (C) 2010 Nokia Corporation. All rights reserved.

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

#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <limits.h>

#include <glib.h>

#include "usb_moded.h"
#include "usb_moded-modules.h"
#include "usb_moded-modes.h"
#include "usb_moded-log.h"
#include "usb_moded-dbus.h"
#include "usb_moded-dbus-private.h"
#include "usb_moded-appsync.h"
#include "usb_moded-config.h"
#include "usb_moded-modesetting.h"
#include "usb_moded-network.h"
#include "usb_moded-android.h"

static void report_mass_storage_blocker(const char *mountpoint, int try);
guint delayed_network = 0;

int write_to_file(const char *path, const char *text)
{
  int err = -1;
  int fd = -1;
  size_t todo = 0;

  /* if either path or the text to be written are not there
     we return an error */
  if(!text || !path)
	return err;

  todo  = strlen(text);

  /* no O_CREAT -> writes only to already existing files */
  if( (fd = TEMP_FAILURE_RETRY(open(path, O_WRONLY))) == -1 )
  {
    /* gcc -pedantic does not like "%m"
       log_warning("open(%s): %m", path); */
    log_warning("open(%s): %s", path, strerror(errno));
    goto cleanup;
  }

  while( todo > 0 )
  {
    ssize_t n = TEMP_FAILURE_RETRY(write(fd, text, todo));
    if( n < 0 )
    {
      log_warning("write(%s): %s", path, strerror(errno));
      goto cleanup;
    }
    todo -= n;
    text += n;
  }

  err = 0;

cleanup:

  if( fd != -1 ) TEMP_FAILURE_RETRY(close(fd));

  return err;
}

static gboolean network_retry(gpointer data)
{
	usb_network_up(data);
	return(FALSE);
}

static int set_mass_storage_mode(struct mode_list_elem *data)
{
        gchar *command;
        char command2[256], *real_path = NULL, *mountpath;
        const char *mount;
        gchar **mounts;
        int ret = 0, i = 0, mountpoints = 0, fua = 0, try = 0;

        /* send unmount signal so applications can release their grasp on the fs, do this here so they have time to act */
        usb_moded_send_signal(USB_PRE_UNMOUNT);
        fua = find_sync();
        mount = find_mounts();
        if(mount)
        {
        	mounts = g_strsplit(mount, ",", 0);
             	/* check amount of mountpoints */
                for(i=0 ; mounts[i] != NULL; i++)
                {
                	mountpoints++;
                }

		if(strcmp(data->mode_module, MODULE_NONE))
		{
			/* check if the file storage module has been loaded with sufficient luns in the parameter,
			if not, unload and reload or load it. Since  mountpoints start at 0 the amount of them is one more than their id */
			sprintf(command2, "/sys/devices/platform/musb_hdrc/gadget/gadget-lun%d/file", (mountpoints - 1) );
			if(access(command2, R_OK) == -1)
			{
				log_debug("%s does not exist, unloading and reloading mass_storage\n", command2);
				usb_moded_unload_module(MODULE_MASS_STORAGE);
				sprintf(command2, "modprobe %s luns=%d \n", MODULE_MASS_STORAGE, mountpoints);
				log_debug("usb-load command = %s \n", command2);
				ret = system(command2);
				if(ret)
					return(ret);
			}
		}
                /* umount filesystems */
                for(i=0 ; mounts[i] != NULL; i++)
                {
                	/* check if filesystem is mounted or not, if ret = 1 it is already unmounted */
			real_path = realpath(mounts[i], NULL);
			if(real_path)
				mountpath = real_path;
			else
				mountpath = mounts[i];
umount:                 command = g_strconcat("mount | grep ", mountpath, NULL);
                        ret = system(command);
                        g_free(command);
                        if(!ret)
                        {
				/* no check for / needed as that will fail to umount anyway */
				command = g_strconcat("umount ", mountpath, NULL);
                                log_debug("unmount command = %s\n", command);
                                ret = system(command);
                                g_free(command);
                                if(ret != 0)
                                {
					if(try != 3)
					{
						try++;
						sleep(1);
						log_err("Umount failed. Retrying\n");
						report_mass_storage_blocker(mount, 1);
						goto umount;
					}
					else
					{
                                		log_err("Unmounting %s failed\n", mount);
						report_mass_storage_blocker(mount, 2);
                                        	usb_moded_send_error_signal(UMOUNT_ERROR);
     	                                   	return(ret);
					}
                                }
                         }
                         else
                         	/* already unmounted. Set return value to 0 since there is no error */
                                ret = 0;
              	}
		
	        /* activate mounts after sleeping 1s to be sure enumeration happened and autoplay will work in windows*/
		usleep(1800);
                for(i=0 ; mounts[i] != NULL; i++)
                {       
			
			if(strcmp(data->mode_module, MODULE_NONE))
			{
				sprintf(command2, "echo %i  > /sys/devices/platform/musb_hdrc/gadget/gadget-lun%d/nofua", fua, i);
				log_debug("usb lun = %s active\n", command2);
				system(command2);
				sprintf(command2, "/sys/devices/platform/musb_hdrc/gadget/gadget-lun%d/file", i);
				log_debug("usb lun = %s active\n", command2);
				write_to_file(command2, mounts[i]);
			}
			else
			{
				write_to_file("/sys/class/android_usb/android0/enable", "0");
				write_to_file("/sys/class/android_usb/android0/functions", "mass_storage");
				//write_to_file("/sys/class/android_usb/f_mass_storage/lun/nofua", fua);
				write_to_file("/sys/class/android_usb/f_mass_storage/lun/file", mount);
				write_to_file("/sys/class/android_usb/android0/enable", "1");

			}
                }
                g_strfreev(mounts);
		g_free((gpointer *)mount);
		if(real_path)
			free(real_path);
	}

	/* only send data in use signal in case we actually succeed */
        if(!ret)
                usb_moded_send_signal(DATA_IN_USE);
	
	return(ret);

}

static int unset_mass_storage_mode(struct mode_list_elem *data)
{
        gchar *command;
        char command2[256], *real_path = NULL, *mountpath;
        const char *mount;
        gchar **mounts;
        int ret = 1, i = 0;

        mount = find_mounts();
        if(mount)
        {
        	mounts = g_strsplit(mount, ",", 0);
                for(i=0 ; mounts[i] != NULL; i++)
                {
                	/* check if it is still or already mounted, if so (ret==0) skip mounting */
			real_path = realpath(mounts[i], NULL);
			if(real_path)
				mountpath = real_path;
			else
				mountpath = mounts[i];
                	command = g_strconcat("mount | grep ", mountpath, NULL);
                        ret = system(command);
                        g_free(command);
                        if(ret)
                        {
                        	command = g_strconcat("mount ", mountpath, NULL);
				log_debug("mount command = %s\n",command);
                                ret = system(command);
                                g_free(command);
				/* mount returns 0 if success */
                                if(ret != 0 )
                                {
                                	log_err("Mounting %s failed\n", mount);
					if(ret)
					{
						mount = find_alt_mount();
						if(mount)
						{
                                               		command = g_strconcat("mount -t tmpfs tmpfs -o ro --size=512K ", mount, NULL);
							log_debug("Total failure, mount ro tmpfs as fallback\n");
                                                        ret = system(command);
                                                        g_free(command);
                                                }
						usb_moded_send_error_signal(RE_MOUNT_FAILED);
                                        }

                                  }
                        }
			if(data != NULL)
			{
				if(!strcmp(data->mode_module, MODULE_NONE))
				{
					log_debug("Disable android mass storage\n");
					write_to_file("/sys/class/android_usb/f_mass_storage/lun/file", "0");
					write_to_file("/sys/class/android_usb/android0/enable", "0");
				}
			}
			else
			{
				sprintf(command2, "echo \"\"  > /sys/devices/platform/musb_hdrc/gadget/gadget-lun%d/file", i);
				log_debug("usb lun = %s inactive\n", command2);
				system(command2);
			}
                 }
                 g_strfreev(mounts);
		 g_free((gpointer *)mount);
		 if(real_path)
			free(real_path);
        }

	return(ret);

}

static void report_mass_storage_blocker(const char *mountpoint, int try)
{
  FILE *stream = 0;
  gchar *lsof_command = 0;
  int count = 0;

  lsof_command = g_strconcat("lsof ", mountpoint, NULL);

  if( (stream = popen(lsof_command, "r")) )
  {
    char *text = 0;
    size_t size = 0;

    while( getline(&text, &size, stream) >= 0 )
    {
        /* skip the first line as it does not contain process info */
        if(count != 0)
        {
          gchar **split = 0;
          split = g_strsplit((const gchar*)text, " ", 2);
          log_err("Mass storage blocked by process %s\n", split[0]);
          usb_moded_send_error_signal(split[0]);
          g_strfreev(split);
        }
        count++;
    }
    pclose(stream);
  }
  g_free(lsof_command);
  if(try == 2)
	log_err("Setting Mass storage blocked. Giving up.\n");

}

int set_dynamic_mode(void)
{

  struct mode_list_elem *data; 
  int ret = 1;
  int network = 1;

  data = get_usb_mode_data();

  if(!data)
	return(ret);

  if(data->mass_storage)
  {
	return set_mass_storage_mode(data);
  }

#ifdef APP_SYNC
  if(data->appsync)
	if(activate_sync(data->mode_name)) /* returns 1 on error */
	{
		log_debug("Appsync failure");
		return(ret);
	}
#endif
  /* make sure things are disabled before changing functionality */
  if(data->softconnect_disconnect)
  {
	write_to_file(data->softconnect_path, data->softconnect_disconnect);
  }
  /* set functionality first, then enable */
  if(data->android_extra_sysfs_value && data->android_extra_sysfs_path)
  {
	ret = write_to_file(data->android_extra_sysfs_path, data->android_extra_sysfs_value);
  }
  if(data->android_extra_sysfs_value2 && data->android_extra_sysfs_path2)
  {
	write_to_file(data->android_extra_sysfs_path2, data->android_extra_sysfs_value2);
  }
  if(data->sysfs_path)
  {
	write_to_file(data->sysfs_path, data->sysfs_value);
	log_debug("writing to file %s, value %s\n", data->sysfs_path, data->sysfs_value);
  }
  if(data->idProduct)
  {
	/* only works for android since the idProduct is a module parameter */
	set_android_productid(data->idProduct);
  }
  /* enable the device */
  if(data->softconnect)
  {
	ret = write_to_file(data->softconnect_path, data->softconnect);
  }

  /* functionality should be enabled, so we can enable the network now */
  if(data->network)
  {
#ifdef DEBIAN
  	char command[256];

	g_snprintf(command, 256, "ifdown %s ; ifup %s", data->network_interface, data->network_interface);
        system(command);
#else
	usb_network_down(data);
	network = usb_network_up(data);
#endif /* DEBIAN */
  }

  /* try a second time to bring up the network if it failed the first time,
     this can happen with functionfs based gadgets (which is why we sleep for a bit */
  if(network != 0)
  {
	log_debug("Retry setting up the network later\n");
	delayed_network = g_timeout_add_seconds(3, network_retry, data);
  }

  /* Needs to be called before application post synching so
     that the dhcp server has the right config */
  if(data->nat || data->dhcp_server)
	usb_network_set_up_dhcpd(data);

  /* no need to execute the post sync if there was an error setting the mode */
  if(data->appsync && !ret)
  {
	/* let's sleep for a bit (350ms) to allow interfaces to settle before running postsync */
	usleep(350000);
	activate_sync_post(data->mode_name);
  }

#ifdef CONNMAN
  if(data->connman_tethering)
	connman_set_tethering(data->connman_tethering, TRUE);
#endif

  if(ret)
	usb_moded_send_error_signal(MODE_SETTING_FAILED);
  return(ret);
}

void unset_dynamic_mode(void)
{ 

  struct mode_list_elem *data; 

  data = get_usb_mode_data();

  if(delayed_network)
  {
	g_source_remove(delayed_network);
	delayed_network = 0;
  }

  /* the modelist could be empty */
  if(!data)
	return;

  if(!strcmp(data->mode_name, MODE_MASS_STORAGE))
  {
	unset_mass_storage_mode(data);
	return;
  }

#ifdef CONNMAN
  if(data->connman_tethering)
	connman_set_tethering(data->connman_tethering, FALSE);
#endif

  if(data->network)
  {
	usb_network_down(data);
  }

  /* disconnect before changing functionality */
  if(data->softconnect_disconnect)
  {
	write_to_file(data->softconnect_path, data->softconnect_disconnect);
  }
  if(data->sysfs_path)
  {
	write_to_file(data->sysfs_path, data->sysfs_reset_value);
	log_debug("writing to file %s, value %s\n", data->sysfs_path, data->sysfs_reset_value);
  }
}

/** clean up mode changes or extra actions to perform after a mode change 
 * @param module Name of module currently in use
 * @return 0 on success, non-zero on failure
 *
 */
int usb_moded_mode_cleanup(const char *module)
{

	log_debug("Cleaning up mode\n");

	if(!module)
	{
		log_warning("No module found to unload. Skipping cleanup\n");
		return 0;
	}

#ifdef APP_SYNC
	appsync_stop();
#endif /* APP_SYNC */

        if(!strcmp(module, MODULE_MASS_STORAGE)|| !strcmp(module, MODULE_FILE_STORAGE))
        {
		/* no clean-up needs to be done when we come from charging mode. We need
		   to check since we use fake mass-storage for charging */
		if(!strcmp(MODE_CHARGING, get_usb_mode()) || !strcmp(MODE_CHARGING_FALLBACK, get_usb_mode()))
		  return 0;	
		unset_mass_storage_mode(NULL);
        }

	else if(get_usb_mode_data())
		unset_dynamic_mode();

        return(0);
}

