/**
 * @file usb_moded-network.c
 *
 * (De)activates network depending on the network setting system.
 *
 * Copyright (c) 2011 Nokia Corporation. All rights reserved.
 * Copyright (c) 2012 - 2020 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Philippe De Swert <philippedeswert@gmail.com>
 * @author Philippe De Swert <philippe.deswert@jollamobile.com>
 * @author Marko Saukko <marko.saukko@jollamobile.com>
 * @author Slava Monich <slava.monich@jolla.com>
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

/*============================================================================= */

#include "usb_moded-network.h"

#include "usb_moded-config-private.h"
#include "usb_moded-control.h"
#include "usb_moded-log.h"
#include "usb_moded-modesetting.h"
#include "usb_moded-worker.h"
#include "usb_moded-dbus-private.h"

#include <sys/stat.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* ========================================================================= *
 * Constants
 * ========================================================================= */

#define UDHCP_CONFIG_PATH       "/run/usb-moded/udhcpd.conf"
#define UDHCP_CONFIG_DIR        "/run/usb-moded"
#define UDHCP_CONFIG_LINK       "/etc/udhcpd.conf"

/* ========================================================================= *
 * Types
 * ========================================================================= */

/** IP forwarding configuration block */
typedef struct ipforward_data_t
{
    /** Address of primary DNS */
    char *dns1;
    /** Address of secondary DNS */
    char *dns2;
    /** Interface from which packets should be forwarded */
    char *nat_interface;
} ipforward_data_t;

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * IPFORWARD_DATA
 * ------------------------------------------------------------------------- */

static ipforward_data_t *ipforward_data_create           (void);
static void              ipforward_data_delete           (ipforward_data_t *self);
static void              ipforward_data_clear            (ipforward_data_t *self);
static void              ipforward_data_set_dns1         (ipforward_data_t *self, const char *dns);
static void              ipforward_data_set_dns2         (ipforward_data_t *self, const char *dns);
static void              ipforward_data_set_nat_interface(ipforward_data_t *self, const char *interface);

/* ------------------------------------------------------------------------- *
 * OFONO
 * ------------------------------------------------------------------------- */

#ifdef OFONO
static gchar *ofono_get_default_modem (void);
static gchar *ofono_get_modem_status  (const char *modem);
static bool   ofono_get_roaming_status(void);
#endif

/* ------------------------------------------------------------------------- *
 * CONNMAN
 * ------------------------------------------------------------------------- */

#ifdef CONNMAN
static bool   connman_technology_set_tethering   (DBusConnection *con, const char *technology, bool on, DBusError *err);
static gchar *connman_manager_get_service_path   (DBusConnection *con, const char *type);
static bool   connman_service_get_connection_data(DBusConnection *con, const char *service, ipforward_data_t *ipforward);
static bool   connman_get_connection_data        (ipforward_data_t *ipforward);
bool          connman_set_tethering              (const char *technology, bool on);
#endif

/* ------------------------------------------------------------------------- *
 * LEGACY
 * ------------------------------------------------------------------------- */

#ifndef CONNMAN
static bool legacy_get_connection_data(ipforward_data_t *ipforward);
#endif

/* ------------------------------------------------------------------------- *
 * NETWORK
 * ------------------------------------------------------------------------- */

static bool  network_interface_exists     (char *interface);
static char *network_get_interface        (const modedata_t *data);
static int   network_setup_ip_forwarding  (const modedata_t *data, ipforward_data_t *ipforward);
static void  network_cleanup_ip_forwarding(void);
static int   network_check_udhcpd_symlink (void);
static int   network_write_udhcpd_config  (const modedata_t *data, ipforward_data_t *ipforward);
int          network_update_udhcpd_config (const modedata_t *data);
int          network_up                   (const modedata_t *data);
void         network_down                 (const modedata_t *data);
void         network_update               (void);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static const char default_interface[] = "usb0";

/* ========================================================================= *
 * IPFORWARD_DATA
 * ========================================================================= */

static ipforward_data_t *
ipforward_data_create(void)
{
    LOG_REGISTER_CONTEXT;

    ipforward_data_t *self = g_malloc0(sizeof *self);

    self->dns1 = 0;
    self->dns2 = 0;
    self->nat_interface = 0;

    return self;
}

static void
ipforward_data_delete(ipforward_data_t *self)
{
    LOG_REGISTER_CONTEXT;

    if( self )
    {
        ipforward_data_clear(self);
        g_free(self);
    }
}

static void
ipforward_data_clear(ipforward_data_t *self)
{
    LOG_REGISTER_CONTEXT;

    ipforward_data_set_dns1(self, 0);
    ipforward_data_set_dns2(self, 0);
    ipforward_data_set_nat_interface(self, 0);
}

static void
ipforward_data_set_dns1(ipforward_data_t *self, const char *dns)
{
    LOG_REGISTER_CONTEXT;

    g_free(self->dns1),
        self->dns1 = dns ? g_strdup(dns) : 0;
}

static void
ipforward_data_set_dns2(ipforward_data_t *self, const char *dns)
{
    LOG_REGISTER_CONTEXT;

    g_free(self->dns2),
        self->dns2 = dns ? g_strdup(dns) : 0;
}

static void
ipforward_data_set_nat_interface(ipforward_data_t *self, const char *interface)
{
    LOG_REGISTER_CONTEXT;

    g_free(self->nat_interface),
        self->nat_interface = interface ? g_strdup(interface) : 0;
}

/* ========================================================================= *
 * OFONO
 * ========================================================================= */

#ifdef OFONO

/** Get object path of the 1st modem known to ofono
 *
 * Caller must release the returned string with g_free().
 *
 * @return object path, or NULL
 */

static gchar *
ofono_get_default_modem(void)
{
    gchar          *modem = 0;
    DBusConnection *con   = 0;
    DBusError       err   = DBUS_ERROR_INIT;
    DBusMessage    *rsp   = 0;

    if( !(con = umdbus_get_connection()) )
        goto EXIT;

    rsp = umdbus_blocking_call(con,
                               "org.ofono",
                               "/",
                               "org.ofono.Manager",
                               "GetModems",
                               &err,
                               DBUS_TYPE_INVALID);
    if( !rsp )
        goto EXIT;

    // a(oa{sv}) -> get object path in the first struct in the array
    DBusMessageIter body;
    if( umdbus_parser_init(&body, rsp) ) {
        DBusMessageIter iter_array;
        if( umdbus_parser_get_array(&body, &iter_array) ) {
            DBusMessageIter astruct;
            if( umdbus_parser_get_struct(&iter_array, &astruct) ) {
                const char *object = 0;
                if( umdbus_parser_get_object(&astruct, &object) ) {
                    modem = g_strdup(object);
                }
            }
        }
    }

EXIT:
    if( rsp )
        dbus_message_unref(rsp);

    if( con )
        dbus_connection_unref(con);

    dbus_error_free(&err);

    log_warning("default modem = %s", modem ?: "n/a");

    return modem;
}

/** Query modem status from ofono
 *
 * Caller must release the returned string with g_free().
 *
 * @param modem  D-Bus object path
 *
 * @return modem status, or NULL
 */
static gchar *
ofono_get_modem_status(const char *modem)
{
    gchar          *status = 0;
    DBusConnection *con    = 0;
    DBusError       err    = DBUS_ERROR_INIT;
    DBusMessage    *rsp    = 0;

    if( !(con = umdbus_get_connection()) )
        goto EXIT;

    rsp = umdbus_blocking_call(con,
                               "org.ofono",
                               modem,
                               "org.ofono.NetworkRegistration",
                               "GetProperties",
                               &err,
                               DBUS_TYPE_INVALID);
    if( !rsp )
        goto EXIT;

    DBusMessageIter body;
    if( umdbus_parser_init(&body, rsp) ) {
        DBusMessageIter iter_array;
        if( umdbus_parser_get_array(&body, &iter_array) ) {
            DBusMessageIter entry;
            while( umdbus_parser_get_entry(&iter_array, &entry) ) {
                const char *key = 0;
                if( !umdbus_parser_get_string(&entry, &key) )
                    break;
                if( strcmp(key, "Status") )
                    continue;
                DBusMessageIter var;
                if( !umdbus_parser_get_variant(&entry, &var) )
                    break;
                const char *val = 0;
                if( !umdbus_parser_get_string(&var, &val) )
                    break;
                status = g_strdup(val);
                break;
            }
        }
    }

EXIT:
    if( rsp )
        dbus_message_unref(rsp);

    if( con )
        dbus_connection_unref(con);

    dbus_error_free(&err);

    log_warning("modem status = %s", status ?: "n/a");

    return status;
}

/** Get roaming data from ofono
 *
 * @return true if roaming, false when not (or when ofono is unavailable)
 */
static bool
ofono_get_roaming_status(void)
{
    LOG_REGISTER_CONTEXT;

    bool roaming = false;
    gchar *modem = 0;
    gchar *status = 0;

    if( !(modem = ofono_get_default_modem()) )
        goto EXIT;

    if( !(status = ofono_get_modem_status(modem)) )
        goto EXIT;

    if( !strcmp(status, "roaming") )
        roaming = true;

EXIT:
    g_free(status);
    g_free(modem);

    log_warning("modem roaming = %d", roaming);

    return roaming;
}
#endif /* OFONO */

/* ========================================================================= *
 * CONNMAN
 * ========================================================================= */

#ifdef CONNMAN
# define CONNMAN_SERVICE                "net.connman"
# define CONNMAN_TECH_INTERFACE         "net.connman.Technology"
# define CONNMAN_ERROR_ALREADY_ENABLED  "net.connman.Error.AlreadyEnabled"
# define CONNMAN_ERROR_ALREADY_DISABLED "net.connman.Error.AlreadyDisabled"

/* ------------------------------------------------------------------------- *
 * TECHNOLOGY interface
 * ------------------------------------------------------------------------- */

/** Configures tethering for the specified connman technology.
 *
 * @param con         D-Bus connection
 * @param technology  D-Bus object path
 * @param on          true to enable tethering, false to disable
 * @param err         Where to store D-Bus ipc error
 *
 * @return true on success, false otherwise
 */
static bool
connman_technology_set_tethering(DBusConnection *con, const char *technology, bool on,
                                 DBusError *err)
{
    LOG_REGISTER_CONTEXT;

    bool         res = FALSE;
    DBusMessage *rsp = 0;
    const char  *key = "Tethering";
    dbus_bool_t  val = on;

    rsp = umdbus_blocking_call(con,
                               CONNMAN_SERVICE,
                               technology,
                               CONNMAN_TECH_INTERFACE,
                               "SetProperty",
                               err,
                               DBUS_TYPE_STRING, &key,
                               DBUS_TYPE_VARIANT,
                               DBUS_TYPE_BOOLEAN, &val,
                               DBUS_TYPE_INVALID);

    if( !rsp ) {
        if( on ) {
            if( !g_strcmp0(err->name, CONNMAN_ERROR_ALREADY_ENABLED) )
                goto SUCCESS;
        }
        else {
            if( !g_strcmp0(err->name, CONNMAN_ERROR_ALREADY_DISABLED) )
                goto SUCCESS;
        }
        log_err("%s.%s method call failed: %s: %s",
                CONNMAN_TECH_INTERFACE, "SetProperty",
                err->name, err->message);
        goto FAILURE;
    }

SUCCESS:
    log_debug("%s tethering %s", technology, on ? "on" : "off");
    dbus_error_free(err);
    res = TRUE;

FAILURE:
    if( rsp )
        dbus_message_unref(rsp);

    return res;
}

/* ------------------------------------------------------------------------- *
 * MANAGER interface
 * ------------------------------------------------------------------------- */

/** Get object path for the 1st service matching given type
 *
 * Caller must release the returned string with g_free().
 *
 * @param type  Connman service type string
 *
 * @return D-Bus object path, or NULL
 */
static gchar *
connman_manager_get_service_path(DBusConnection *con, const char *type)
{
    LOG_REGISTER_CONTEXT;

    gchar          *service = 0;
    DBusError       err   = DBUS_ERROR_INIT;
    DBusMessage    *rsp   = 0;

    rsp = umdbus_blocking_call(con,
                               "net.connman",
                               "/",
                               "net.connman.Manager",
                               "GetServices",
                               &err,
                               DBUS_TYPE_INVALID);
    if( !rsp )
        goto EXIT;

    // a(oa{sv}) -> get object path in the first struct matching given type
    DBusMessageIter body;
    if( umdbus_parser_init(&body, rsp) ) {
        // @ body
        DBusMessageIter array_of_structs;
        if( umdbus_parser_get_array(&body, &array_of_structs) ) {
            // @ array of structs
            DBusMessageIter astruct;
            while( umdbus_parser_get_struct(&array_of_structs, &astruct) ) {
                // @ struct
                const char *object = 0;
                if( !umdbus_parser_get_object(&astruct, &object) )
                    break;
                DBusMessageIter array_of_entries;
                if( !umdbus_parser_get_array(&astruct, &array_of_entries) )
                    break;
                // @ array of dict entries
                DBusMessageIter entry;
                while( umdbus_parser_get_entry(&array_of_entries, &entry) ) {
                    // @ dict entry
                    const char *key = 0;
                    if( !umdbus_parser_get_string(&entry, &key) )
                        break;
                    if( strcmp(key, "Type") )
                        continue;
                    DBusMessageIter var;
                    if( !umdbus_parser_get_variant(&entry, &var) )
                        break;
                    const char *value = 0;
                    if( !umdbus_parser_get_string(&var, &value) )
                        break;
                    if( strcmp(value, type) )
                        continue;
                    service = g_strdup(object);
                    goto EXIT;
                }
            }
        }
    }

EXIT:
    if( rsp )
        dbus_message_unref(rsp);

    dbus_error_free(&err);

    log_warning("%s service = %s", type, service ?: "n/a");

    return service;
}

/** Query ipforwarding parameters from connman
 *
 * @param con         D-Bus connection
 * @param service     D-Bus object path
 * @param ipforward   ipforward object to fill in
 *
 * @return true on success, false otherwise
 */
static bool
connman_service_get_connection_data(DBusConnection *con,
                                    const char *service,
                                    ipforward_data_t *ipforward)
{
    LOG_REGISTER_CONTEXT;

    bool            ack   = false;
    DBusMessage    *rsp   = NULL;
    DBusError       err   = DBUS_ERROR_INIT;

    log_debug("Filling in dns data");

    rsp = umdbus_blocking_call(con,
                               "net.connman",
                               service,
                               "net.connman.Service",
                               "GetProperties",
                               &err,
                               DBUS_TYPE_INVALID);
    if( !rsp )
        goto EXIT;

    const char *dns1 = 0;
    const char *dns2 = 0;
    const char *state = 0;
    const char *interface = 0;

    DBusMessageIter body;
    if( umdbus_parser_init(&body, rsp) ) {
        // @ body
        DBusMessageIter array_of_entries;
        if( umdbus_parser_get_array(&body, &array_of_entries) ) {
            // @ array of entries
            DBusMessageIter entry;
            while( umdbus_parser_get_entry(&array_of_entries, &entry) ) {
                // @ dict entry
                const char *key = 0;
                if( !umdbus_parser_get_string(&entry, &key) )
                    break;
                DBusMessageIter var;
                if( !umdbus_parser_get_variant(&entry, &var) )
                    break;

                if( !strcmp(key, "Nameservers"))
                {
                    DBusMessageIter array_of_strings;
                    if( umdbus_parser_get_array(&var, &array_of_strings) ) {
                        // expect 0, 1, or 2 entries
                        if( !umdbus_parser_at_end(&array_of_strings) )
                            umdbus_parser_get_string(&array_of_strings, &dns1);
                        if( !umdbus_parser_at_end(&array_of_strings) )
                            umdbus_parser_get_string(&array_of_strings, &dns2);
                    }
                }
                else if( !strcmp(key, "State"))
                {
                    umdbus_parser_get_string(&var, &state);
                }
                else if( !strcmp(key, "Ethernet"))
                {
                    DBusMessageIter array_of_en_entries;
                    if( umdbus_parser_get_array(&var, &array_of_en_entries) ) {
                        DBusMessageIter en_entry;
                        while( umdbus_parser_get_entry(&array_of_en_entries, &en_entry) ) {
                            const char *en_key = 0;
                            if( !umdbus_parser_get_string(&en_entry, &en_key) )
                                break;
                            if( strcmp(en_key, "Interface") )
                                continue;
                            DBusMessageIter en_var;
                            if( umdbus_parser_get_variant(&en_entry, &en_var) )
                                umdbus_parser_get_string(&en_var, &interface);
                        }
                    }
                }
            }
        }
    }

    bool connected = (!g_strcmp0(state, "ready") ||
                      !g_strcmp0(state, "online"));

    log_debug("state = %s", state ?: "n/a");
    log_debug("connected = %s", connected ? "true" : "false");
    log_debug("interface = %s", interface ?: "n/a");
    log_debug("dns1 = %s", dns1 ?: "n/a");
    log_debug("dns2 = %s", dns2 ?: "n/a");

    if( !dns1 || !interface || !connected )
        goto EXIT;

    ipforward_data_set_dns1(ipforward, dns1);
    ipforward_data_set_dns2(ipforward, dns2 ?: dns1);
    ipforward_data_set_nat_interface(ipforward, interface);

    ack = true;

EXIT:
    if( rsp )
        dbus_message_unref(rsp);

    dbus_error_free(&err);

    return ack;
}

/* ------------------------------------------------------------------------- *
 * USB-MODED interface
 * ------------------------------------------------------------------------- */

/** Query ipforwarding parameters from connman
 *
 * @param ipforward   ipforward object to fill in
 *
 * @return true on success, false otherwise
 */
static bool
connman_get_connection_data(ipforward_data_t *ipforward)
{
    LOG_REGISTER_CONTEXT;

    bool            ack      = false;
    DBusConnection *con      = 0;
    gchar          *cellular = 0;
    gchar          *wifi     = 0;

    if( !(con = umdbus_get_connection()) )
        goto FAILURE;

    /* Try to get connection data from cellular service */
    if( !(cellular = connman_manager_get_service_path(con, "cellular")) )
        log_warning("no sellular service");
    else if( connman_service_get_connection_data(con, cellular, ipforward) )
        goto SUCCESS;

    /* Try to get connection data from wifi service */
    if( !(wifi = connman_manager_get_service_path(con, "wifi")) )
        log_warning("no wifi service");
    else if( connman_service_get_connection_data(con, wifi, ipforward) )
        goto SUCCESS;

    /* Abandon hope */
    goto FAILURE;

SUCCESS:
    ack = true;

FAILURE:
    if( !ack )
        log_warning("no connection data");
    else
        log_debug("got connection data");

    g_free(wifi);
    g_free(cellular);

    if( con )
        dbus_connection_unref(con);

    return ack;
}

/** Configures tethering for the specified connman technology.
 *
 * @param technology  D-Bus object path
 * @param on          true to enable tethering, false to disable
 *
 * @return true on success, false otherwise
 */
bool
connman_set_tethering(const char *technology, bool on)
{
    LOG_REGISTER_CONTEXT;

    bool            res = false;
    DBusError       err = DBUS_ERROR_INIT;
    DBusConnection *con = 0;

    if( !(con = umdbus_get_connection()) )
        goto EXIT;

    res = connman_technology_set_tethering(con, technology, on, &err);

EXIT:
    dbus_error_free(&err);

    if( con )
        dbus_connection_unref(con);

    log_debug("set tethering(%s) = %s -> %s", technology,
              on ? "on" : "off", res ? "ack" : "nak");

    return res;
}
#endif /* CONNMAN */

/* ========================================================================= *
 * LEGACY
 * ========================================================================= */

#ifndef CONNMAN
/** Read dns settings from /etc/resolv.conf
 *
 * @return true on success, false on failure
 */
static bool
legacy_get_connection_data(ipforward_data_t *ipforward)
{
    LOG_REGISTER_CONTEXT;

    static const char path[] = "/etc/resolv.conf";

    bool    ack  = false;
    FILE   *file = 0;
    char   *buff = 0;
    size_t  size = 0;

    if( !(file = fopen(path, "r")) ) {
        log_warning("%s: can't open for reading: %m", path);
        goto EXIT;
    }

    int count = 0;
    while( count < 2 && getline(&buff, &size, file) >= 0 ) {
        /* skip comment and empty lines */
        if( strchr("#\n", *buff) )
            continue;

        gchar **tokens = g_strsplit(buff, " ", 3);
        if( !tokens || !tokens[0] || !tokens[1] ) {
            // ignore
        }
        else if( !g_strcmp0(tokens[0], "nameserver") ) {
            // TODO: can have '\n' at eol?
            g_strstrip(tokens[1]);
            if( ++count == 1 )
                ipforward_data_set_dns1(ipforward, tokens[1]);
            else
                ipforward_data_set_dns2(ipforward, tokens[1]);
        }
        g_strfreev(tokens);
    }

    if( count < 1 ) {
        log_warning("%s: no nameserver lines found", path);
        goto EXIT;
    }

    /* FIXME: connman_service_get_connection_data() duplicates
     *        dns1 if dns2 is not set - is this necessary?
     */
    if( count == 1 )
        ipforward_data_set_dns2(ipforward, ipforward->dns1);

    ack = true;

EXIT:
    free(buff);

    if( file )
        fclose(file);

    return ack;
}
#endif

/* ========================================================================= *
 * NETWORK
 * ========================================================================= */

/** This function checks if the configured interface exists
 *
 * @return true on success, false on failure
 */
static bool
network_interface_exists(char *interface)
{
    LOG_REGISTER_CONTEXT;

    bool ack = false;

    if(interface)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "/sys/class/net/%s", interface);
        ack = (access(path, F_OK) == 0);
    }

    return ack;
}

/** Get network interface to use
 *
 * @param data  Dynamic mode data (not used)
 *
 * @return interface name, or NULL
 */
static char *
network_get_interface(const modedata_t *data)
{
    LOG_REGISTER_CONTEXT;

    (void)data; // FIXME: why is this passed in the 1st place?

    char *interface = 0;
    char *setting   = config_get_network_setting(NETWORK_INTERFACE_KEY);

    if( network_interface_exists(setting) )
    {
        /* Use the configured value */
        interface = setting, setting = 0;
    }
    else
    {
        /* Fall back to default value */
        interface = strdup(default_interface);
        if( !network_interface_exists(interface) )
        {
            log_warning("Neither configured %s nor fallback %s interface exists."
                        " Check your config!",
                        setting   ?: "NULL",
                        interface ?: "NULL");
            free(interface), interface = 0;
        }
    }

    log_debug("interface = %s", interface ?: "NULL");
    free(setting);
    return interface;
}

/** Turn on ip forwarding on the usb interface
 *
 * To cleanup: #network_cleanup_ip_forwarding()
 *
 * @param data  Dynamic mode data (not used)
 *
 * @return 0 on success, 1 on failure
 */
static int
network_setup_ip_forwarding(const modedata_t *data, ipforward_data_t *ipforward)
{
    LOG_REGISTER_CONTEXT;

    int   failed        = 1;
    char *interface     = 0;
    char *nat_interface = 0;

    char command[256];

    if( !(interface = network_get_interface(data)) )
        goto EXIT;

    nat_interface = config_get_network_setting(NETWORK_NAT_INTERFACE_KEY);
    if( !nat_interface ) {
        if( !ipforward->nat_interface ) {
            log_debug("No nat interface available!");
            goto EXIT;
        }
        nat_interface = strdup(ipforward->nat_interface);
    }

    write_to_file("/proc/sys/net/ipv4/ip_forward", "1");

    snprintf(command, sizeof command, "/sbin/iptables -t nat -A POSTROUTING -o %s -j MASQUERADE", nat_interface);
    common_system(command);

    snprintf(command, sizeof command, "/sbin/iptables -A FORWARD -i %s -o %s  -m state  --state RELATED,ESTABLISHED -j ACCEPT", nat_interface, interface);
    common_system(command);

    snprintf(command, sizeof command, "/sbin/iptables -A FORWARD -i %s -o %s -j ACCEPT", interface, nat_interface);
    common_system(command);

    log_debug("ipforwarding success!");
    failed = 0;

EXIT:
    free(interface);
    free(nat_interface);

    return failed;
}

/** Turn off ip forwarding on the usb interface
 */
static void
network_cleanup_ip_forwarding(void)
{
    LOG_REGISTER_CONTEXT;

    write_to_file("/proc/sys/net/ipv4/ip_forward", "0");

    common_system("/sbin/iptables -F FORWARD");
}

/** Validate udhcpd.conf symlink
 *
 * @return zero if symlink is valid, non-zero otherwise
 */
static int
network_check_udhcpd_symlink(void)
{
    LOG_REGISTER_CONTEXT;

    int ret = -1;
    char dest[sizeof UDHCP_CONFIG_PATH + 1];
    ssize_t rc = readlink(UDHCP_CONFIG_LINK, dest, sizeof dest - 1);

    if( rc < 0 ) {
        if( errno != ENOENT )
            log_err("%s: can't read symlink: %m", UDHCP_CONFIG_LINK);
    }
    else if( (size_t)rc < sizeof dest ) {
        dest[rc] = 0;
        if( strcmp(dest, UDHCP_CONFIG_PATH) )
            log_warning("%s: symlink is invalid", UDHCP_CONFIG_LINK);
        else
            ret = 0;
    }
    return ret;
}

/** Write udhcpd.conf
 *
 * @param ipforward  NULL if we want a simple config, otherwise include dns info etc...
 * @param data  Dynamic mode data (not used)
 *
 * @return zero on success, non-zero otherwise
 */
static int
network_write_udhcpd_config(const modedata_t *data, ipforward_data_t *ipforward)
{
    LOG_REGISTER_CONTEXT;

    // assume failure
    int err = -1;

    FILE  *conffile = 0;
    char  *interface = 0;
    char  *ip = 0;
    char  *netmask = 0;

    if( !(interface = network_get_interface(data)) ) {
        log_err("no network interface");
        goto EXIT;
    }

    /* generate start and end ip based on the setting */
    if( !(ip = config_get_network_setting(NETWORK_IP_KEY)) ) {
        log_err("no network address");
        goto EXIT;
    }

    int len = 0;
    if( sscanf(ip, "%*d.%*d.%*d%n.%*d", &len) == EOF || ip[len] != '.') {
        log_err("malformed network address: %s", ip);
        goto EXIT;
    }

    if( !(netmask = config_get_network_setting(NETWORK_NETMASK_KEY)) ) {
        log_err("no network address mask");
        goto EXIT;
    }

    /* /tmp and /run is often tmpfs, so we avoid writing to flash */
    if( mkdir(UDHCP_CONFIG_DIR, 0775) == -1 && errno != EEXIST ) {
        log_warning("%s: can't create directory: %m", UDHCP_CONFIG_DIR);
    }

    /* print all data in the file */
    if( !(conffile = fopen(UDHCP_CONFIG_PATH, "w")) ) {
        log_err("%s: can't open for writing: %m", UDHCP_CONFIG_PATH);
        goto EXIT;
    }

    fprintf(conffile, "start\t%.*s.1\n", len, ip);
    fprintf(conffile, "end\t%.*s.15\n", len, ip);
    fprintf(conffile, "interface\t%s\n", interface);
    fprintf(conffile, "option\tsubnet\t%s\n", netmask);
    fprintf(conffile, "option\tlease\t3600\n");
    fprintf(conffile, "max_leases\t15\n");

    if(ipforward != NULL)
    {
        if( !ipforward->dns1 || !ipforward->dns2 )
            log_debug("No dns info!");
        else
            fprintf(conffile, "opt\tdns\t%s %s\n", ipforward->dns1, ipforward->dns2);
        fprintf(conffile, "opt\trouter\t%s\n", ip);
    }

    fclose(conffile), conffile = 0;

    /* check that we have a valid symlink */
    if( network_check_udhcpd_symlink() != 0 ) {
        if( unlink(UDHCP_CONFIG_LINK) == -1 && errno != ENOENT )
            log_warning("%s: can't remove invalid config: %m", UDHCP_CONFIG_LINK);

        if( symlink(UDHCP_CONFIG_PATH, UDHCP_CONFIG_LINK) == -1 ) {
            log_err("%s: can't create symlink to %s: %m",
                    UDHCP_CONFIG_LINK, UDHCP_CONFIG_PATH);
            goto EXIT;
        }
        log_debug("%s: symlink to %s created",
                  UDHCP_CONFIG_LINK, UDHCP_CONFIG_PATH);
    }

    // success
    err = 0;

EXIT:
    free(netmask);
    free(ip);
    free(interface);
    if( conffile )
        fclose(conffile);

    return err;
}

/** Update udhcpd.conf
 *
 * Must be succesfully called before starting udhcpd to ensure
 * /etc/udhcpd.conf points to valid data.
 *
 * No cleanup required (the config file can be left behind).
 *
 * @param data  Dynamic mode data
 *
 * @return zero on success, non-zero on failure
 */
int
network_update_udhcpd_config(const modedata_t *data)
{
    LOG_REGISTER_CONTEXT;

    ipforward_data_t *ipforward = NULL;
    int ret = 1;

    /* Set up nat info only if it is required */
    if( data->nat )
    {
#ifdef OFONO
        /* check if we are roaming or not */
        if( ofono_get_roaming_status() ) {
            /* get permission to use roaming */
            if(config_is_roaming_not_allowed())
                goto EXIT;
        }
#endif

        ipforward = ipforward_data_create();

#ifdef CONNMAN
        if( !connman_get_connection_data(ipforward) )
        {
            log_debug("data connection not available from connman!");
            /* TODO: send a message to the UI */
            goto EXIT;
        }
#else
        if( !legacy_get_connection_data(ipforward) ) {
            log_debug("data connection not available in resolv.conf!");
            goto EXIT;
        }
#endif
    }

    /* ipforward can be NULL here, which is expected and handled in this function */
    ret = network_write_udhcpd_config(data, ipforward);

    if( ret == 0 && data->nat )
        ret = network_setup_ip_forwarding(data, ipforward);

EXIT:
    ipforward_data_delete(ipforward);

    return ret;
}

/** Activate the network interface
 *
 * @param data  Dynamic mode data (not used)
 *
 * @return zero on success, non-zero on failure
 */
int
network_up(const modedata_t *data)
{
    LOG_REGISTER_CONTEXT;

    // assume failure
    int ret = 1;

    gchar *interface = 0;
    gchar *address   = 0;
    gchar *netmask   = 0;
    gchar *gateway   = 0;

    char command[256];

    if( !(interface = network_get_interface(data)) ) {
        log_err("no network interface");
        goto EXIT;
    }

    if( !(address = config_get_network_setting(NETWORK_IP_KEY)) ) {
        log_err("no network address");
        goto EXIT;
    }

    if( !(netmask = config_get_network_setting(NETWORK_NETMASK_KEY)) ) {
        log_err("no network address mask");
        goto EXIT;
    }

    if( !(gateway = config_get_network_setting(NETWORK_GATEWAY_KEY)) ) {
        /* gateway is optional */
        log_warning("no network gateway");
    }

    if( !strcmp(address, "dhcp") )
    {
        snprintf(command, sizeof command,"dhclient -d %s", interface);
        if( common_system(command) != 0 ) {
            snprintf(command, sizeof command,"udhcpc -i %s", interface);
            if( common_system(command) != 0 )
                goto EXIT;
        }
    }
    else
    {
        snprintf(command, sizeof command,"ifconfig %s %s netmask %s", interface, address, netmask);
        if( common_system(command) != 0 )
            goto EXIT;
    }

    /* TODO: Check first if there is a gateway set */
    if( gateway )
    {
        snprintf(command, sizeof command,"route add default gw %s", gateway);
        if( common_system(command) != 0 )
            goto EXIT;
    }

    ret = 0;

EXIT:
    log_debug("iface=%s addr=%s mask=%s gw=%s -> %s",
              interface ?: "n/a",
              address   ?: "n/a",
              netmask   ?: "n/a",
              gateway   ?: "n/a",
              ret ? "failure" : "success");

    g_free(gateway);
    g_free(netmask);
    g_free(address);
    g_free(interface);
    return ret;
}

/** Deactivate the network interface
 *
 * @param data  Dynamic mode data (not used)
 */
void
network_down(const modedata_t *data)
{
    LOG_REGISTER_CONTEXT;

    gchar *interface = network_get_interface(data);

    char command[256];

    log_debug("iface=%s nat=%d", interface ?: "n/a", data->nat);

    if( interface ) {
        snprintf(command, sizeof command,"ifconfig %s down", interface);
        common_system(command);
    }

    /* dhcp client shutdown happens on disconnect automatically */
    if(data->nat)
        network_cleanup_ip_forwarding();

    g_free(interface);
}

/** Update the network interface with the new setting if connected.
 *
 * Should be called when relevant settings have changed.
 */
void
network_update(void)
{
    LOG_REGISTER_CONTEXT;

    if( control_get_cable_state() == CABLE_STATE_PC_CONNECTED ) {
        modedata_t *data = worker_dup_usb_mode_data();
        if( data && data->network ) {
            network_down(data);
            network_up(data);
        }
        modedata_free(data);
    }
}
