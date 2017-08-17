/**
  @file usb_moded-log.c

  Copyright (C) 2010 Nokia Corporation. All rights reserved.
  Copyright (C) 2016 Jolla Ltd.

  @author: Philippe De Swert <philippe.de-swert@nokia.com>
  @author: Simo Piiroinen <simo.piiroinen@nokia.com>
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

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <ctype.h>

#include "usb_moded-log.h"

static const char *log_name = "<unset>";
static int log_level = LOG_WARNING;
static int log_type  = LOG_TO_STDERR;
static bool log_lineinfo = false;

static char *strip(char *str)
{
  unsigned char *src = (unsigned char *)str;
  unsigned char *dst = (unsigned char *)str;

  while( *src > 0 && *src <= 32 ) ++src;

  for( ;; )
  {
    while( *src > 32 ) *dst++ = *src++;
    while( *src > 0 && *src <= 32 ) ++src;
    if( *src == 0 ) break;
    *dst++ = ' ';
  }
  *dst = 0;
  return str;
}

static struct timeval log_begtime = { 0, 0 };

static void log_gettime(struct timeval *tv)
{
	gettimeofday(tv, 0);
	timersub(tv, &log_begtime, tv);
}

/**
 * Print the logged messages to the selected output
 *
 * @param lev The wanted log level
 * @param fmt The message to be logged
 * @param va The stdarg variable list
 */
void log_emit_va(const char *file, const char *func, int line, int lev, const char *fmt, va_list va)
{
	int saved = errno;
        if( log_p(lev) )
        {
                switch( log_type )
                {
                case LOG_TO_SYSLOG:

                        vsyslog(lev, fmt, va);
                        break;

                case LOG_TO_STDERR:

                        if( log_get_lineinfo() ) {
                                /* Use gcc error like prefix for logging so
                                 * that logs can be analyzed with jump to
                                 * line parsing  available in editors. */
                                fprintf(stderr, "%s:%d: %s(): ", file, line, func);
                        }
                        else {
                                fprintf(stderr, "%s: ", log_get_name());
                        }

#if LOG_ENABLE_TIMESTAMPS
                        {
                                struct timeval tv;
				log_gettime(&tv);
                                fprintf(stderr, "%3ld.%03ld ",
                                        (long)tv.tv_sec,
                                        (long)tv.tv_usec/1000);
                        }
#endif

#if LOG_ENABLE_LEVELTAGS
                        {
                                static const char *tag = "U:";
                                switch( lev )
                                {
                                case LOG_CRIT:    tag = "C:"; break;
                                case LOG_ERR:     tag = "E:"; break;
                                case LOG_WARNING: tag = "W:"; break;
                                case LOG_NOTICE:  tag = "N:"; break;
                                case LOG_INFO:    tag = "I:"; break;
                                case LOG_DEBUG:   tag = "D:"; break;
                                }
                                fprintf(stderr, "%s ", tag);
                        }
#endif
			{
				// squeeze whitespace like syslog does
				char buf[1024];
				errno = saved;
				vsnprintf(buf, sizeof buf - 1, fmt, va);
			  	fprintf(stderr, "%s\n", strip(buf));
			}
                        break;

                default:
                        // no logging
                        break;
                }
        }
	errno = saved;
}

void log_emit_real(const char *file, const char *func, int line, int lev, const char *fmt, ...)
{
        va_list va;
        va_start(va, fmt);
        log_emit_va(file, func, line, lev, fmt, va);
        va_end(va);
}

void log_debugf(const char *fmt, ...)
{
        /* This goes always to stderr */
        if( log_type == LOG_TO_STDERR && log_p(LOG_DEBUG) )
        {
                va_list va;
                va_start(va, fmt);
                vfprintf(stderr, fmt, va);
                va_end(va);
        }
}

/** Get the currently set logging level
 *
 * @return The current logging level
 */
int log_get_level(void)
{
        return log_level;
}

/** Set the logging level
 *
 * @param lev  The wanted logging level
 */
void log_set_level(int lev)
{
        log_level = lev;
}

/** Test if logging should be done at given level
 *
 * @param lev  The logging level to query
 *
 * @return true if logging in the given level is allowed, false otherwise
 */
bool log_p(int lev)
{
        return lev <= log_level;
}

/** Get the currently set logging type
 *
 * @return The current logging type
 */
int log_get_type(void)
{
        return log_type;
}

/* Set the logging type
 *
 * @param type  The wanted logging type
 */
void log_set_type(int type)
{
        log_type = type;
}

/** Get the currently set logging name
 *
 * @return The current logging name
 */
const char *log_get_name(void)
{
        return log_name;
}

/** Set the logging name
 *
 * @param name  The wanted logging name
 */
void log_set_name(const char *name)
{
        log_name = name;
}

/** Enable/disable the logging line info
 *
 * @param lineinfo  true to enable line info, false to disable
 */
void log_set_lineinfo(bool lineinfo)
{
        log_lineinfo = lineinfo;
}

/** Test if line info should be included in logging
 *
 * @return true when line info should be emitted, false otherwise
 */
bool log_get_lineinfo(void)
{
        return log_lineinfo;
}

/** Initialize logging */
void log_init(void)
{
	/* Get reference time used for verbose logging */
	if( !timerisset(&log_begtime) )
		gettimeofday(&log_begtime, 0);
}
