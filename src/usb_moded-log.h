/*
  Copyright (C) 2010 Nokia Corporation. All rights reserved.
  Copyright (C) 2016 Jolla Ltd.

  Author: Philippe De Swert <philippe.de-swert@nokia.com>
  Author: Simo Piiroinen <simo.piiroinen@jollamobile.com>

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
#include <stdarg.h>
#include <syslog.h>

/* Logging functionality */
extern const char *log_name;
extern int log_level;
extern int log_type;

#define LOG_ENABLE_DEBUG      01
#define LOG_ENABLE_TIMESTAMPS 01
#define LOG_ENABLE_LEVELTAGS  01
    
enum 
{   
  LOG_TO_STDERR, // log to stderr
  LOG_TO_SYSLOG, // log to syslog 
};  
    
             
void log_set_level(int lev); 
int log_get_level(void);

void log_init(void);
void log_emit_va(int lev, const char *fmt, va_list va);
void log_emit(int lev, const char *fmt, ...) __attribute__((format(printf,2,3)));
void log_debugf(const char *fmt, ...) __attribute__((format(printf,1,2)));

#define log_crit(...)      log_emit(2, __VA_ARGS__)
#define log_err(...)       log_emit(3, __VA_ARGS__)
#define log_warning(...)   log_emit(4, __VA_ARGS__)

#if LOG_ENABLE_DEBUG
# define log_notice(...)   log_emit(5, __VA_ARGS__)
# define log_info(...)     log_emit(6, __VA_ARGS__)
# define log_debug(...)    log_emit(7, __VA_ARGS__)
#else
# define log_notice(...)   do{}while(0)
# define log_info(...)     do{}while(0)
# define log_debug(...)    do{}while(0)

# define log_debugf(...)   do{}while(0)
#endif

