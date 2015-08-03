/*
  @file usb-moded_network.c : (De)activates network depending on the network setting system.

  Copyright (C) 2011 Nokia Corporation. All rights reserved.
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

/*============================================================================= */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>

#include "usb_moded.h"
#include "usb_moded-network.h"
#include "usb_moded-config.h"
#include "usb_moded-log.h"
#include "usb_moded-modesetting.h"

#if CONNMAN || OFONO
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#endif

#define UDHCP_CONFIG_PATH	"/run/usb-moded/udhcpd.conf"
#define UDHCP_CONFIG_DIR	"/run/usb-moded"
#define UDHCP_CONFIG_LINK	"/etc/udhcpd.conf"

const char default_interface[] = "usb0";
typedef struct ipforward_data
{
	char *dns1;
	char *dns2;
	char *nat_interface;
}ipforward_data;

#ifdef CONNMAN
static int connman_reset_state(void);
#endif

static void free_ipforward_data (struct ipforward_data *ipforward)
{
  if(ipforward)
  {
	if(ipforward->dns1)
		free(ipforward->dns1);
	if(ipforward->dns2)
		free(ipforward->dns2);
	if(ipforward->nat_interface)
		free(ipforward->nat_interface);
	free(ipforward);
  }
}

/* This function checks if the configured interface exists */
static int check_interface(char *interface)
{
  char path[25];
  int ret = 0;

   snprintf(path, 25, "/sys/class/net/%s", interface );
   ret = access(path, F_OK);

   return(ret);
}

static char* get_interface(struct mode_list_elem *data)
{
  char *interface = NULL;
  int check = 0;

  interface = get_network_setting(NETWORK_INTERFACE_KEY);
  check = check_interface(interface);

  if(check != 0)
  {
	if(interface != NULL)
		free(interface);
	/* no known interface configured and existing, falling back to usb0 */
	interface = malloc(sizeof(default_interface)*sizeof(char));
	strncpy(interface, default_interface, sizeof(default_interface));
  }

  check = check_interface(interface);
  if(check)
  {
	log_warning("Configured interface is incorrect, nor does usb0 exists. Check your config!\n");
	free(interface);
	return(0);
  }

  log_debug("interface = %s\n", interface);
  return interface;
}

/**
 * Turn on ip forwarding on the usb interface
 * @return: 0 on success, 1 on failure
 */
static int set_usb_ip_forward(struct mode_list_elem *data, struct ipforward_data *ipforward)
{
  char *interface, *nat_interface;
  char command[128];

  interface = get_interface(data);
  if(interface == NULL)
	return(1);
  nat_interface = get_network_setting(NETWORK_NAT_INTERFACE_KEY);
  if((nat_interface == NULL) && (ipforward->nat_interface != NULL))
	nat_interface = strdup(ipforward->nat_interface);
  else
  {
	log_debug("No nat interface available!\n");
#ifdef CONNMAN
	/* in case the cellular did not come up we want to make sure wifi gets restored */
	connman_reset_state();
#endif
	free(interface);
	free(nat_interface);
	return(1);
  }
  write_to_file("/proc/sys/net/ipv4/ip_forward", "1");
  snprintf(command, 128, "/sbin/iptables -t nat -A POSTROUTING -o %s -j MASQUERADE", nat_interface);
  system(command);

  snprintf(command, 128, "/sbin/iptables -A FORWARD -i %s -o %s  -m state  --state RELATED,ESTABLISHED -j ACCEPT", nat_interface, interface);
  system(command);

  snprintf(command, 128, "/sbin/iptables -A FORWARD -i %s -o %s -j ACCEPT", interface, nat_interface);
  system(command);

  free(interface);
  free(nat_interface);
  log_debug("ipforwarding success!\n");
  return(0);
}

/** 
 * Remove ip forward
 */
static void clean_usb_ip_forward(void)
{
#ifdef CONNMAN
  connman_reset_state();
#endif
  write_to_file("/proc/sys/net/ipv4/ip_forward", "0");
  system("/sbin/iptables -F FORWARD");
}

#ifdef OFONO
/**
 * Get roaming data from ofono
 * 
 * @return : 1 if roaming, 0 when not (or when ofono is unavailable)
 */
static int get_roaming(void)
{
  int ret = 0, type;
  DBusError error;
  DBusMessage *msg = NULL, *reply;
  DBusConnection *dbus_conn_ofono = NULL;
  char *modem = NULL;
  DBusMessageIter iter, subiter;

  dbus_error_init(&error);

  if( (dbus_conn_ofono = dbus_bus_get(DBUS_BUS_SYSTEM, &error)) == 0 )
  {
         log_err("Could not connect to dbus for ofono\n");
  }

  /* find the modem object path so can find out if it is roaming or not (one modem only is assumed) */
  if ((msg = dbus_message_new_method_call("org.ofono", "/", "org.ofono.Manager", "GetModems")) != NULL)
  {
        if ((reply = dbus_connection_send_with_reply_and_block(dbus_conn_ofono, msg, -1, NULL)) != NULL)
        {
	  dbus_message_iter_init(reply, &iter);
	  dbus_message_iter_recurse(&iter, &subiter);
	  iter = subiter;
	  dbus_message_iter_recurse(&iter, &subiter);
	  type = dbus_message_iter_get_arg_type(&subiter);
	  if(type == DBUS_TYPE_OBJECT_PATH)
	  {
		dbus_message_iter_get_basic(&subiter, &modem);
		log_debug("modem = %s\n", modem);
	  }
          dbus_message_unref(reply);
        }
        dbus_message_unref(msg);
  }
  /* if modem found then we check roaming state */
  if(modem != NULL)
  {
	if ((msg = dbus_message_new_method_call("org.ofono", modem, "org.ofono.NetworkRegistration", "GetProperties")) != NULL)
	{
		if ((reply = dbus_connection_send_with_reply_and_block(dbus_conn_ofono, msg, -1, NULL)) != NULL)
		{
		  dbus_message_iter_init(reply, &iter);
		  dbus_message_iter_recurse(&iter, &subiter);
		  iter = subiter;
		  dbus_message_iter_recurse(&iter, &subiter);
		  type = dbus_message_iter_get_arg_type(&subiter);
		    if(type == DBUS_TYPE_STRING)
		    {
			dbus_message_iter_next (&subiter);
			iter = subiter;
			dbus_message_iter_recurse(&iter, &subiter);
			dbus_message_iter_get_basic(&subiter, &modem);
			log_debug("modem status = %s\n", modem);
		    }
		}
		dbus_message_unref(reply);
		}
        dbus_message_unref(msg);
	
	if(!strcmp("roaming", modem))
		ret = 1;
  }

  return(ret);
}
#endif

#ifndef CONNMAN
/**
 * Read dns settings from /etc/resolv.conf
 */
static int resolv_conf_dns(struct ipforward_data *ipforward)
{
  FILE *resolv;
  int i = 0, count = 0;
  char *line = NULL, **tokens;
  size_t len = 0;
  ssize_t read;


  resolv = fopen("/etc/resolv.conf", "r");
  if (resolv == NULL)
	return(1);

  /* we don't expect more than 10 lines in /etc/resolv.conf */
  for (i=0; i < 10; i++)
  {
	read = getline(&line, &len, resolv);
	if(read)
	{
	  if(strstr(line, "nameserver") != NULL)
	  {
		tokens = g_strsplit(line, " ", 2);
		if(count == 0)
			ipforward->dns1 = strdup(tokens[1]);
		else
			ipforward->dns2 = strdup(tokens[1]);
		count++;
		g_strfreev(tokens);
	  }
	}
	if(count == 2)
		goto end;
  }
end:
  free(line);
  fclose(resolv);
  return(0);
}
#endif

static int checklink(void)
{
  int ret = -1;
  char dest[sizeof(UDHCP_CONFIG_PATH)+1];
  size_t len = readlink(UDHCP_CONFIG_LINK, dest, sizeof(dest)-1);

  if (len > 0)
  {
	dest[len] = 0;
	ret = strcmp(dest, UDHCP_CONFIG_PATH);
  }
  return(ret);
}

/** 
  * Write udhcpd.conf
  * @ipforward : NULL if we want a simple config, otherwise include dns info etc...
  */
static int write_udhcpd_conf(struct ipforward_data *ipforward, struct mode_list_elem *data)
{
  FILE *conffile;
  char *ip, *interface, *netmask;
  char *ipstart, *ipend;
  int dot = 0, i = 0, test;
  struct stat st;

  /* /tmp and /run is often tmpfs, so we avoid writing to flash */
  mkdir(UDHCP_CONFIG_DIR, 0664);
  conffile = fopen(UDHCP_CONFIG_PATH, "w");
  if(conffile == NULL)
  {
	log_debug("Error creating "UDHCP_CONFIG_PATH"!\n");
	return(1);
  }

  interface = get_interface(data);
  if(interface == NULL)
  {
	fclose(conffile);
	return(1);
  }
  /* generate start and end ip based on the setting */
  ip = get_network_setting(NETWORK_IP_KEY);
  ipstart = malloc(sizeof(char)*15);
  ipend = malloc(sizeof(char)*15);
  while(i < 15)
  {
        if(dot < 3)
        {
                if(ip[i] == '.')
                        dot ++;
                ipstart[i] = ip[i];
                ipend[i] = ip[i];
        }
        else
        {
                ipstart[i] = '\0';
                ipend[i] = '\0';
                break;
        }
        i++;
  }
  strcat(ipstart,"1");
  strcat(ipend, "15");

  netmask = get_network_setting(NETWORK_NETMASK_KEY);

  /* print all data in the file */
  fprintf(conffile, "start\t%s\n", ipstart);
  fprintf(conffile, "end\t%s\n", ipend);
  fprintf(conffile, "interface\t%s\n", interface);
  fprintf(conffile, "option\tsubnet\t%s\n", netmask);
  fprintf(conffile, "option\tlease\t3600\n");

  if(ipforward != NULL)
  {
	if(!ipforward->dns1 || !ipforward->dns2)
	{
		log_debug("No dns info!");
	}
	else
		fprintf(conffile, "opt\tdns\t%s %s\n", ipforward->dns1, ipforward->dns2);
	fprintf(conffile, "opt\trouter\t%s\n", ip);
  }

  free(ipstart);
  free(ipend);
  free(ip);
  free(interface);
  free(netmask);
  fclose(conffile);

  /* check if it is a symlink, if not remove and link, create the link if missing */
  test = lstat(UDHCP_CONFIG_LINK, &st);
  /* if stat fails there is no file or link */
  if(test == -1)
	goto link;
  /* if it is not a link, or points to the wrong place we remove it */
  if(((st.st_mode & S_IFMT) != S_IFLNK) || checklink())
  {
	unlink(UDHCP_CONFIG_LINK);
  }
  else
	goto end;

link:
  if (symlink(UDHCP_CONFIG_PATH, UDHCP_CONFIG_LINK) == -1)
  {
	log_debug("Error creating link "UDHCP_CONFIG_LINK" -> "UDHCP_CONFIG_PATH": %s\n", strerror(errno));
	unlink(UDHCP_CONFIG_PATH);
	return(1);
  }

end:
  log_debug(UDHCP_CONFIG_LINK" created\n");
  return(0);
}

#ifdef CONNMAN

#define CONNMAN_SERVICE                "net.connman"
#define CONNMAN_TECH_INTERFACE         "net.connman.Technology"
#define CONNMAN_ERROR_INTERFACE        CONNMAN_SERVICE ".Error"
#define CONNMAN_ERROR_ALREADY_ENABLED  CONNMAN_ERROR_INTERFACE ".AlreadyEnabled"
#define CONNMAN_ERROR_ALREADY_DISABLED CONNMAN_ERROR_INTERFACE ".AlreadyDisabled"

/*
 * Configures tethering for the specified connman technology.
 */
static gboolean connman_try_set_tethering(DBusConnection *connection, const char *path, gboolean on)
{
  gboolean ok = FALSE;
  DBusMessage *message = dbus_message_new_method_call(CONNMAN_SERVICE, path, CONNMAN_TECH_INTERFACE, "SetProperty");
  if (message)
  {
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter iter2;
	DBusError error;

	const char* key = "Tethering";
	dbus_bool_t value = (on != FALSE);

	dbus_message_iter_init_append(message, &iter);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &key);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, DBUS_TYPE_BOOLEAN_AS_STRING, &iter2);
	dbus_message_iter_append_basic(&iter2, DBUS_TYPE_BOOLEAN, &value);
	dbus_message_iter_close_container(&iter, &iter2);

	dbus_error_init(&error);
	reply = dbus_connection_send_with_reply_and_block(connection, message,  DBUS_TIMEOUT_USE_DEFAULT, &error);
	dbus_message_unref(message);
	if (reply)
	{
		log_debug("%s tethering %s", path, on ? "on" : "off");
		dbus_message_unref(reply);
		ok = TRUE;
	}
	else
	{
		if ((on && !g_strcmp0(error.name, CONNMAN_ERROR_ALREADY_ENABLED)) ||
		    (!on && (!g_strcmp0(error.name, CONNMAN_ERROR_ALREADY_DISABLED) ||
			     !g_strcmp0(error.name, DBUS_ERROR_UNKNOWN_OBJECT))))
		{
			ok = TRUE;
		}
		else
		{
			log_err("%s\n", error.message);
		}
		dbus_error_free(&error);
	}
  }
  return ok;
}

gboolean connman_set_tethering(const char *path, gboolean on)
{
  gboolean ok = FALSE;
  DBusError error;
  DBusConnection *connection;

  dbus_error_init(&error);
  connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
  if (connection)
  {
	int i;
	for (i=0; i<10; i++)
	{
		if (i>0)
		{
			struct timespec tv;
			tv.tv_sec = 0;
			tv.tv_nsec = 200000000;
			nanosleep(&tv, NULL);
		}
		if (connman_try_set_tethering(connection, path, on))
		{
			ok = TRUE;
			break;
		}
	}
	dbus_connection_unref(connection);
  }
  else
  {
	log_err("%s\n", error.message);
	dbus_error_free (&error);
  }

  return ok;
}

/**
 * Connman message handling
 */
static char * connman_parse_manager_reply(DBusMessage *reply, const char *req_service)
{
  DBusMessageIter iter, subiter, origiter;
  int type;
  char *service;
  
  dbus_message_iter_init(reply, &iter);
  type = dbus_message_iter_get_arg_type(&iter);
  dbus_message_iter_recurse(&iter, &subiter);
  type = dbus_message_iter_get_arg_type(&subiter);
  origiter = subiter;
  iter = subiter;
  while(type != DBUS_TYPE_INVALID)
  {

	  if(type == DBUS_TYPE_STRUCT)
	  {
		origiter = iter;
		dbus_message_iter_recurse(&iter, &subiter);
		type = dbus_message_iter_get_arg_type(&subiter);
		if(type == DBUS_TYPE_OBJECT_PATH)
		{
			dbus_message_iter_get_basic(&subiter, &service);
			log_debug("service = %s\n", service);
			if(strstr(service, req_service))
			{
				log_debug("%s service found!\n", req_service);
				return(strdup(service));
			}
		}
	   }
	dbus_message_iter_next(&origiter);
	type = dbus_message_iter_get_arg_type(&origiter);
	iter = origiter;
  }
  log_debug("end of list\n");
  return(0);
}

static int connman_fill_connection_data(DBusMessage *reply, struct ipforward_data *ipforward)
{
  DBusMessageIter array_iter, dict_iter, inside_dict_iter, variant_iter;
  DBusMessageIter sub_array_iter, string_iter;
  int type, next;
  char *string;
  
  log_debug("Filling in dns data\n");
  dbus_message_iter_init(reply, &array_iter);
  type = dbus_message_iter_get_arg_type(&array_iter);

  dbus_message_iter_recurse(&array_iter, &dict_iter);
  type = dbus_message_iter_get_arg_type(&dict_iter);
	  
  while(type != DBUS_TYPE_INVALID)
  {
		dbus_message_iter_recurse(&dict_iter, &inside_dict_iter);
		type = dbus_message_iter_get_arg_type(&inside_dict_iter);
		if(type == DBUS_TYPE_STRING)
		{
			dbus_message_iter_get_basic(&inside_dict_iter, &string);
			//log_debug("string = %s\n", string);
			if(!strcmp(string, "Nameservers"))
			{
				//log_debug("Trying to get Nameservers");
				dbus_message_iter_next (&inside_dict_iter);
				type = dbus_message_iter_get_arg_type(&inside_dict_iter);
				dbus_message_iter_recurse(&inside_dict_iter, &variant_iter);
				type = dbus_message_iter_get_arg_type(&variant_iter);
				if(type == DBUS_TYPE_ARRAY)
				{
					dbus_message_iter_recurse(&variant_iter, &string_iter);
					type = dbus_message_iter_get_arg_type(&string_iter);
					if(type != DBUS_TYPE_STRING)
					{
						/* not online */
						return(1);
					}
					dbus_message_iter_get_basic(&string_iter, &string);
					log_debug("dns = %s\n", string);
					ipforward->dns1 = strdup(string);
					next = dbus_message_iter_next (&string_iter);
					if(!next)
					{
						log_debug("No secundary dns\n");
						/* FIXME: set the same dns for dns2 to avoid breakage */
						ipforward->dns2 = strdup(string);
						return(0);
					}
					dbus_message_iter_get_basic(&string_iter, &string);
					log_debug("dns2 = %s\n", string);
					ipforward->dns2 = strdup(string);
					return(0);
				}
			}
			else if(!strcmp(string, "State"))
			{
				//log_debug("Trying to get online state");
				dbus_message_iter_next (&inside_dict_iter);
				type = dbus_message_iter_get_arg_type(&inside_dict_iter);
				dbus_message_iter_recurse(&inside_dict_iter, &variant_iter);
				type = dbus_message_iter_get_arg_type(&variant_iter);
                                if(type == DBUS_TYPE_STRING)
                                {
                                        dbus_message_iter_get_basic(&variant_iter, &string);
					log_debug("Connection state = %s\n", string);
					/* if cellular not online, connect it */
					if(strcmp(string, "online"))
					{
						log_debug("Not online. Turning on cellular data connection.\n");
						return(1);
					}
					
				}

			}
			else if(!strcmp(string, "Ethernet"))
			{
				dbus_message_iter_next (&inside_dict_iter);
				type = dbus_message_iter_get_arg_type(&inside_dict_iter);
				dbus_message_iter_recurse(&inside_dict_iter, &variant_iter);
				type = dbus_message_iter_get_arg_type(&variant_iter);
				if(type == DBUS_TYPE_ARRAY)
				{
					dbus_message_iter_recurse(&variant_iter, &sub_array_iter);
					/* we want the second dict */
					dbus_message_iter_next(&sub_array_iter);
					/* we go into the dict and get the string */
					dbus_message_iter_recurse(&sub_array_iter, &variant_iter);
					type = dbus_message_iter_get_arg_type(&variant_iter);
					if(type == DBUS_TYPE_STRING)
					{
						dbus_message_iter_get_basic(&variant_iter, &string);
						if(!strcmp(string, "Interface"))
						{
							/* get variant and iter down in it */
							dbus_message_iter_next(&variant_iter);
							dbus_message_iter_recurse(&variant_iter, &string_iter);
							dbus_message_iter_get_basic(&string_iter, &string);
							log_debug("cellular interface = %s\n", string);
							ipforward->nat_interface = strdup(string);
						}
					}
				}

			}
		}
	dbus_message_iter_next (&dict_iter);
	type = dbus_message_iter_get_arg_type(&dict_iter);
  }
  return(0);
}

/**
 * Turn on cellular connection if it is not on 
 */
static int connman_set_cellular_online(DBusConnection *dbus_conn_connman, const char *service, int retry)
{
  DBusMessage *msg = NULL, *reply;
  DBusError error;
  int ret = 0;
  char *wifi = NULL;

  dbus_error_init(&error);

  if(!retry)
  {
	if ((msg = dbus_message_new_method_call("net.connman", "/", "net.connman.Manager", "GetServices")) != NULL)
	{
		if ((reply = dbus_connection_send_with_reply_and_block(dbus_conn_connman, msg, -1, NULL)) != NULL)
		{
		  wifi = connman_parse_manager_reply(reply, "wifi");
		  dbus_message_unref(reply);
		}
		dbus_message_unref(msg);
	}

	if(wifi != NULL)
	{
		/* we must make sure that wifi is disconnected as sometimes cellular will not come up otherwise */
		if ((msg = dbus_message_new_method_call("net.connman", wifi, "net.connman.Service", "Disconnect")) != NULL)
		{
		  /* we don't care for the reply, which is empty anyway if all goes well */
		  ret = !dbus_connection_send(dbus_conn_connman, msg, NULL);
		  dbus_connection_flush(dbus_conn_connman);
		  dbus_message_unref(msg);
		}
	}
  }
  if ((msg = dbus_message_new_method_call("net.connman", service, "net.connman.Service", "Connect")) != NULL)
  {
	/* we don't care for the reply, which is empty anyway if all goes well */
        ret = !dbus_connection_send(dbus_conn_connman, msg, NULL);
	/* sleep for the connection to come up */
	sleep(5);
	/* make sure the message is sent before cleaning up and closing the connection */
	dbus_connection_flush(dbus_conn_connman);
        dbus_message_unref(msg);
  }

  if(wifi)
	free(wifi);

  return(ret);
}

/*
 * Turn on or off the wifi service
 * wifistatus static variable tracks the state so we do not turn on the wifi if it was off
 *
*/
static int connman_wifi_power_control(DBusConnection *dbus_conn_connman, int on)
{
  static int wifistatus = 0;
  int type = 0;
  char *string;
  DBusMessage *msg = NULL, *reply;
  //DBusError error;
  DBusMessageIter array_iter, dict_iter, inside_dict_iter, variant_iter;

  if(!on)
  {
	/* check wifi status only before turning off */
	if ((msg = dbus_message_new_method_call("net.connman", "/net/connman/technology/wifi", "net.connman.Technology", "GetProperties")) != NULL)
	{
		if ((reply = dbus_connection_send_with_reply_and_block(dbus_conn_connman, msg, -1, NULL)) != NULL)
		{
		  dbus_message_iter_init(reply, &array_iter);

		  dbus_message_iter_recurse(&array_iter, &dict_iter);
		  type = dbus_message_iter_get_arg_type(&dict_iter);

		  while(type != DBUS_TYPE_INVALID)
		  {
		    dbus_message_iter_recurse(&dict_iter, &inside_dict_iter);
		    type = dbus_message_iter_get_arg_type(&inside_dict_iter);
		    if(type == DBUS_TYPE_STRING)
		    {
			dbus_message_iter_get_basic(&inside_dict_iter, &string);
                        log_debug("string = %s\n", string);
                        if(!strcmp(string, "Powered"))
                        {
				dbus_message_iter_next (&inside_dict_iter);
				type = dbus_message_iter_get_arg_type(&inside_dict_iter);
				dbus_message_iter_recurse(&inside_dict_iter, &variant_iter);
				type = dbus_message_iter_get_arg_type(&variant_iter);
                                if(type == DBUS_TYPE_BOOLEAN)
                                {
                                        dbus_message_iter_get_basic(&variant_iter, &wifistatus);
					log_debug("Powered state = %d\n", wifistatus);
				}
				break;
			}
		     }
		     dbus_message_iter_next (&dict_iter);
		     type = dbus_message_iter_get_arg_type(&dict_iter);
		  }
		  dbus_message_unref(reply);
		}
		dbus_message_unref(msg);
	}
  }

  /*  /net/connman/technology/wifi net.connman.Technology.SetProperty string:Powered variant:boolean:false */
  if(wifistatus && !on)
	system("/bin/dbus-send --print-reply --type=method_call --system --dest=net.connman /net/connman/technology/wifi net.connman.Technology.SetProperty string:Powered variant:boolean:false");
  if(wifistatus && on)
       /* turn on wifi after tethering is over and wifi was on before */
	system("/bin/dbus-send --print-reply --type=method_call --system --dest=net.connman /net/connman/technology/wifi net.connman.Technology.SetProperty string:Powered variant:boolean:true");

  return(0);
}

static int connman_get_connection_data(struct ipforward_data *ipforward)
{
  DBusConnection *dbus_conn_connman = NULL;
  DBusMessage *msg = NULL, *reply = NULL;
  DBusError error;
  char *service = NULL;
  int online = 0, ret = 0;

  dbus_error_init(&error);

  if( (dbus_conn_connman = dbus_bus_get(DBUS_BUS_SYSTEM, &error)) == 0 )
  {
         log_err("Could not connect to dbus for connman\n");
  }

  /* turn off wifi in preparation for cellular connection if needed */
  connman_wifi_power_control(dbus_conn_connman, 0);

  /* get list of services so we can find out which one is the cellular */
  if ((msg = dbus_message_new_method_call("net.connman", "/", "net.connman.Manager", "GetServices")) != NULL)
  {
        if ((reply = dbus_connection_send_with_reply_and_block(dbus_conn_connman, msg, -1, NULL)) != NULL)
        {
	    service = connman_parse_manager_reply(reply, "cellular");
            dbus_message_unref(reply);
        }
        dbus_message_unref(msg);
  }
  
  log_debug("service = %s\n", service);
  if(service)
  {
try_again:
	if ((msg = dbus_message_new_method_call("net.connman", service, "net.connman.Service", "GetProperties")) != NULL)
	{
		if ((reply = dbus_connection_send_with_reply_and_block(dbus_conn_connman, msg, -1, NULL)) != NULL)
		{
			if(connman_fill_connection_data(reply, ipforward))
			{
				if(!connman_set_cellular_online(dbus_conn_connman, service, online) && !online)
				{
					online = 1;
					goto try_again;
				}
				else
				{
					log_debug("Cannot connect to cellular data\n");
					ret = 1;
				}
			}
			dbus_message_unref(reply);
		}
		dbus_message_unref(msg);
	}
  }
  dbus_connection_unref(dbus_conn_connman);
  dbus_error_free(&error);
  free(service);
  return(ret);
}

static int connman_reset_state(void)
{
  DBusConnection *dbus_conn_connman = NULL;
  DBusError error;

  dbus_error_init(&error);

  if( (dbus_conn_connman = dbus_bus_get(DBUS_BUS_SYSTEM, &error)) == 0 )
  {
         log_err("Could not connect to dbus for connman\n");
  }

  /* make sure connman turns wifi back on when we disconnect */
  log_debug("Turning wifi back on\n");
  connman_wifi_power_control(dbus_conn_connman, 1);
  dbus_connection_unref(dbus_conn_connman);
  dbus_error_free(&error);

  return(0);
}
#endif /* CONNMAN */

/** 
 * Write out /etc/udhcpd.conf conf so the config is available when it gets started
 */
int usb_network_set_up_dhcpd(struct mode_list_elem *data)
{
  struct ipforward_data *ipforward = NULL;
  int ret = 1;

  /* Set up nat info only if it is required */
  if(data->nat)
  {
#ifdef OFONO
	/* check if we are roaming or not */
	if(get_roaming())
	{
		/* get permission to use roaming */
		if(is_roaming_not_allowed())
			goto end;
	}
#endif /* OFONO */
	ipforward = malloc(sizeof(struct ipforward_data));
	memset(ipforward, 0, sizeof(struct ipforward_data));
#ifdef CONNMAN
	if(connman_get_connection_data(ipforward))
	{
		log_debug("data connection not available!\n");
		/* TODO: send a message to the UI */
		goto end;
	}
#else
	if(resolv_conf_dns(ipforward))
		goto end;
#endif /*CONNMAN */
  }
  /* ipforward can be NULL here, which is expected and handled in this function */
  ret = write_udhcpd_conf(ipforward, data);

  if(data->nat)
	ret = set_usb_ip_forward(data, ipforward);

end:
  /* the function checks if ipforward is NULL or not */
  free_ipforward_data(ipforward);
  return(ret);
}

#if CONNMAN_WORKS_BETTER
static int append_variant(DBusMessageIter *iter, const char *property,
		int type, const char *value)
{
	DBusMessageIter variant;
	const char *type_str;

	switch(type) {
	case DBUS_TYPE_BOOLEAN:
                type_str = DBUS_TYPE_BOOLEAN_AS_STRING;
                break;
        case DBUS_TYPE_BYTE:
                type_str = DBUS_TYPE_BYTE_AS_STRING;
                break;
        case DBUS_TYPE_STRING:
                type_str = DBUS_TYPE_STRING_AS_STRING;
                break;
	case DBUS_TYPE_INT32:
		type_str = DBUS_TYPE_INT32_AS_STRING;
		break;
	default:
		return -EOPNOTSUPP;
	}

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &property);
	dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, type_str,
			&variant);
	dbus_message_iter_append_basic(&variant, type, value);
	dbus_message_iter_close_container(iter, &variant);

	return 0;
}
#endif /* CONNMAN */

/**
 * Activate the network interface
 *
 */
int usb_network_up(struct mode_list_elem *data)
{
  char *ip = NULL, *gateway = NULL;
  int ret = -1;

#if CONNMAN_WORKS_BETTER
  DBusConnection *dbus_conn_connman = NULL;
  DBusMessage *msg = NULL, *reply = NULL;
  DBusMessageIter iter, variant, dict;
  DBusMessageIter msg_iter;
  DBusMessageIter dict_entry;
  DBusError error;
  const char *service = NULL;

  /* make sure connman will recognize the gadget interface NEEDED? */
  //system("/bin/dbus-send --print-reply --type=method_call --system --dest=net.connman /net/connman/technology/gadget net.connman.Technology.SetProperty string:Powered variant:boolean:true");
  //system("/sbin/ifconfig rndis0 up");

  log_debug("waiting for connman to pick up interface\n");
  sleep(1);
  dbus_error_init(&error);

  if( (dbus_conn_connman = dbus_bus_get(DBUS_BUS_SYSTEM, &error)) == 0 )
  {
         log_err("Could not connect to dbus for connman\n");
  }

  /* get list of services so we can find out which one is the usb gadget */
  if ((msg = dbus_message_new_method_call("net.connman", "/", "net.connman.Manager", "GetServices")) != NULL)
  {
        if ((reply = dbus_connection_send_with_reply_and_block(dbus_conn_connman, msg, -1, NULL)) != NULL)
        {
            service = connman_parse_manager_reply(reply, "gadget");
            dbus_message_unref(reply);
        }
        dbus_message_unref(msg);
  }

  if(service == NULL)
	return(1);
  log_debug("gadget = %s\n", service);

  /* now we need to configure the connection */
  if ((msg = dbus_message_new_method_call("net.connman", service, "net.connman.Service", "SetProperty")) != NULL)
  {
	log_debug("iter init\n");
	dbus_message_iter_init_append(msg, &msg_iter);
	log_debug("iter append\n");
	// TODO: crashes here, need to rework this whole bit, connman dbus is hell
	dbus_message_iter_append_basic(&msg_iter, DBUS_TYPE_STRING, "IPv4.Configuration");
	log_debug("iter open container\n");
	dbus_message_iter_open_container(&msg_iter, DBUS_TYPE_VARIANT,
			DBUS_TYPE_ARRAY_AS_STRING
				DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
					DBUS_TYPE_STRING_AS_STRING
					DBUS_TYPE_VARIANT_AS_STRING
				DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
			&variant);

	log_debug("iter open container 2\n");
	dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY,
			DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
				DBUS_TYPE_STRING_AS_STRING
				DBUS_TYPE_VARIANT_AS_STRING
			DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
			&dict);

	log_debug("Set Method\n");
	dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
	append_variant(&dict_entry, "Method", DBUS_TYPE_STRING, "manual");
	dbus_message_iter_close_container(&dict, &dict_entry);

	log_debug("Set ip\n");
	ip = get_network_settings(NETWORK_IP_KEY);
	dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
	append_variant(&dict_entry, "Address", DBUS_TYPE_STRING, ip);
	dbus_message_iter_close_container(&dict, &dict_entry);

	log_debug("Set netmask\n");
	dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
	append_variant(&dict_entry, "Netmask", DBUS_TYPE_STRING, "255.255.255.0");
	dbus_message_iter_close_container(&dict, &dict_entry);

	log_debug("set gateway\n");
	gateway = get_network_setting(NETWORK_GATEWAY_KEY);
	if(gateway)
	{
		dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
		append_variant(&dict_entry, "Gateway", DBUS_TYPE_STRING, gateway);
		dbus_message_iter_close_container(&dict, &dict_entry);
	}
	dbus_message_iter_close_container(&variant, &dict);
	dbus_message_iter_close_container(&msg_iter, &variant);
  }

  log_debug("Connect gadget\n");
  /* Finally we can bring it up */
  if ((msg = dbus_message_new_method_call("net.connman", service, "net.connman.Service", "Connect")) != NULL)
  {
        /* we don't care for the reply, which is empty anyway if all goes well */
        ret = !dbus_connection_send(dbus_conn_connman, msg, NULL);
        /* make sure the message is sent before cleaning up and closing the connection */
        dbus_connection_flush(dbus_conn_connman);
        dbus_message_unref(msg);
  }
  dbus_connection_unref(dbus_conn_connman);
  dbus_error_free(&error);
  free(service);
  if(ip)
	free(ip);
  if(gateway)
	free(gateway);
  return(ret);

#else
  char command[128];
  char *interface;
  char *netmask;

  interface = get_interface(data); 

  if(interface == NULL)
	return(1);

  /* interface found, so we can get all the rest */
  ip = get_network_setting(NETWORK_IP_KEY);
  gateway = get_network_setting(NETWORK_GATEWAY_KEY);
  netmask = get_network_setting(NETWORK_NETMASK_KEY);

  if(!strcmp(ip, "dhcp"))
  {
	sprintf(command, "dhclient -d %s\n", interface);
	ret = system(command);
	if(ret != 0)
	{	
		sprintf(command, "udhcpc -i %s\n", interface);
		system(command);
	}

  }
  else
  {
	sprintf(command, "ifconfig %s %s %s\n", interface, ip, netmask);
	system(command);
  }

  /* TODO: Check first if there is a gateway set */
  if(gateway)
  {
	sprintf(command, "route add default gw %s\n", gateway);
        system(command);
  }

  free(interface);
  free(gateway);
  free(ip);
  free(netmask);

  return(0);
#endif /* CONNMAN */
}

/**
 * Deactivate the network interface
 *
 */
int usb_network_down(struct mode_list_elem *data)
{
#if CONNMAN_WORKS_BETTER
  DBusConnection *dbus_conn_connman = NULL;
  DBusMessage *msg = NULL, *reply = NULL;
  DBusError error;
  char *service = NULL;
  int ret = -1;

  dbus_error_init(&error);

  if( (dbus_conn_connman = dbus_bus_get(DBUS_BUS_SYSTEM, &error)) == 0 )
  {
         log_err("Could not connect to dbus for connman\n");
  }

  /* get list of services so we can find out which one is the usb gadget */
  if ((msg = dbus_message_new_method_call("net.connman", "/", "net.connman.Manager", "GetServices")) != NULL)
  {
        if ((reply = dbus_connection_send_with_reply_and_block(dbus_conn_connman, msg, -1, NULL)) != NULL)
        {
            service = connman_parse_manager_reply(reply, "gadget");
            dbus_message_unref(reply);
        }
        dbus_message_unref(msg);
  }

  if(service == NULL)
	return(1);

  /* Finally we can shut it down */
  if ((msg = dbus_message_new_method_call("net.connman", service, "net.connman.Service", "Disconnect")) != NULL)
  {
        /* we don't care for the reply, which is empty anyway if all goes well */
        ret = !dbus_connection_send(dbus_conn_connman, msg, NULL);
        /* make sure the message is sent before cleaning up and closing the connection */
        dbus_connection_flush(dbus_conn_connman);
        dbus_message_unref(msg);
  }
  free(service);

  /* dhcp server shutdown happens on disconnect automatically */
  if(data->nat)
	clean_usb_ip_forward();
  dbus_error_free(&error);

  return(ret);
#else
  char *interface;
  char command[128];

  interface = get_interface(data);
  if(interface == NULL)
	return(0);

  sprintf(command, "ifconfig %s down\n", interface);
  system(command);

  /* dhcp client shutdown happens on disconnect automatically */
  if(data->nat)
	clean_usb_ip_forward();

  free(interface);
  
  return(0);
#endif /* CONNMAN_IS_EVER_FIXED_FOR_USB */
}

/**
 * Update the network interface with the new setting if connected.
 * 
*/
int usb_network_update(void)
{
  struct mode_list_elem * data;

  if(!get_usb_connection_state())
	return(0);

  data = get_usb_mode_data();
  if(data == NULL)
	return(0);
  if(data->network)
  {
	usb_network_down(data);	
	usb_network_up(data);	
	return(0);
  }
  else
	return(0);

}
