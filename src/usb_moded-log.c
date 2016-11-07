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

const char *log_name = "<unset>";
int log_level = LOG_WARNING;
int log_type  = LOG_TO_STDERR;

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
void log_emit_va(int lev, const char *fmt, va_list va)
{
	int saved = errno;
        if( log_level >= lev )
        {
                switch( log_type )
                {
                case LOG_TO_SYSLOG:

                        vsyslog(lev, fmt, va);
                        break;

                case LOG_TO_STDERR:

                        fprintf(stderr, "%s: ", log_name);

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
                                case 2: tag = "C:"; break;
                                case 3: tag = "E:"; break;
                                case 4: tag = "W:"; break;
                                case 5: tag = "N:"; break;
                                case 6: tag = "I:"; break;
                                case 7: tag = "D:"; break;
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

void log_emit(int lev, const char *fmt, ...)
{
        va_list va;
        va_start(va, fmt);
        log_emit_va(lev, fmt, va);
        va_end(va);
}

void log_debugf(const char *fmt, ...)
{
        /* This goes always to stderr */
        if( log_type == LOG_TO_STDERR && log_level >= LOG_DEBUG )
        {
                va_list va;
                va_start(va, fmt);
                vfprintf(stderr, fmt, va);
                va_end(va);
        }
}
/**
 * returns the currently set log level
 *
 * @return The current log level
 */
inline int log_get_level(void)
{
        return log_level;
}

/* Set the log level
 *
 * @param The wanted log level
 */
inline void log_set_level(int lev)
{
        log_level = lev;
}

/** Initialize logging */
void log_init(void)
{
	/* Get reference time used for verbose logging */
	if( !timerisset(&log_begtime) )
		gettimeofday(&log_begtime, 0);
}
