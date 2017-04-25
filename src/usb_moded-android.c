/**
  @file usb_moded-android.c

  Copyright (C) 2013 Jolla. All rights reserved.

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

#include <stdio.h>
#include <glib.h>

#include "usb_moded-android.h"
#include "usb_moded-log.h"
#include "usb_moded-modesetting.h"
#include "usb_moded-config.h"
#include "usb_moded-mac.h"

/** check if android settings are set
 *
 * @return 1 if settings are available, 0 if not
 *
 */
int android_settings(void)
{
  int ret = 0;
  
  ret = check_android_section();

  return ret;
}

/** Read android serial number from kernel command line
 */
static gchar *get_android_serial(void)
{
    static const char path[] = "/proc/cmdline";
    static const char find[] = "androidboot.serialno=";
    static const char pbrk[] = " \t\r\n,";

    char   *res  = 0;
    FILE   *file = 0;
    size_t  size = 0;
    char   *data = 0;

    if( !(file = fopen(path, "r")) ) {
        log_warning("%s: %s: %m", path, "can't open");
        goto EXIT;
    }

    if( getline(&data, &size, file) < 0 ) {
        log_warning("%s: %s: %m", path, "can't read");
        goto EXIT;
    }

    char *beg = strstr(data, find);
    if( !beg ) {
        log_warning("%s: no serial found", path);
        goto EXIT;
    }

    beg += sizeof find - 1;
    size_t len = strcspn(beg, pbrk);
    if( len < 1 ) {
        log_warning("%s: empty serial found", path);
        goto EXIT;
    }

    res = g_strndup(beg, len);

EXIT:

    free(data);

    if( file )
        fclose(file);

    return res;
}

/** initialize the basic android values
 */
void android_init_values(void)
{
  gchar *text;

  if( (text = get_android_serial()) )
  {
	write_to_file("/sys/class/android_usb/android0/iSerial", text);
	g_free(text);
  }

  text = get_android_manufacturer();
  if(text)
  {
	write_to_file("/sys/class/android_usb/android0/iManufacturer", text);
	g_free(text);
  }
  text = get_android_vendor_id();
  if(text)
  {
	write_to_file("/sys/class/android_usb/android0/idVendor", text);
	g_free(text);
  }
  text = get_android_product();
  if(text)
  {
	write_to_file("/sys/class/android_usb/android0/iProduct", text);
	g_free(text);
  }
  text = get_android_product_id();
  if(text)
  {
	write_to_file("/sys/class/android_usb/android0/idProduct", text);
	g_free(text);
  }
  text = read_mac();
  if(text)
  {
	write_to_file("/sys/class/android_usb/f_rndis/ethaddr", text);
	g_free(text);
  }
  /* For rndis to be discovered correctly in M$ Windows (vista and later) */
  write_to_file("/sys/class/android_usb/f_rndis/wceis", "1");

  /* Make sure android_usb does not stay disabled in case usb moded
   * has crashed / been killed in inconvenient time. */
  write_to_file("/sys/class/android_usb/android0/enable", "1");
}

/* Set a charging mode for the android gadget
 *
 * @return 0 if successful, 1 on failure
 */
int set_android_charging_mode(void)
{
   int ret = 0;

   /* disable, set functions to "mass_storage", re-enable */
   write_to_file("/sys/class/android_usb/android0/enable", "0");
   write_to_file("/sys/class/android_usb/android0/idProduct", "0AFE"); /* TODO: make configurable */
   write_to_file("/sys/class/android_usb/android0/functions", "mass_storage");
   ret = write_to_file("/sys/class/android_usb/android0/enable", "1");
   if(ret < 0)
	return(1);
   else
	return(ret);
}

/* Set a product id for the android gadget
 *
 * @return 0 if successful, 1 on failure
 */
int set_android_productid(char *id)
{
   int ret = 0;

   /* disable, set functions to "mass_storage", re-enable */
   ret = write_to_file("/sys/class/android_usb/android0/idProduct", id);
   if(ret < 0)
	return(1);
   else
	return(ret);
}

/* Set a vendor id for the android gadget
 *
 * @return 0 if successful, 1 on failure
 */
int set_android_vendorid(char *id)
{
   int ret = 0;

   /* disable, set functions to "mass_storage", re-enable */
   ret = write_to_file("/sys/class/android_usb/android0/idVendor", id);
   if(ret < 0)
	return(1);
   else
	return(ret);
}
