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

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <syslog.h>

/* Logging functionality */

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
int log_get_type(void);
void log_set_type(int type);
const char *log_get_name(void);
void log_set_name(const char *name);
void log_set_lineinfo(bool lineinfo);
bool log_get_lineinfo(void);

void log_init(void);
void log_emit_va(const char *file, const char *func, int line, int lev, const char *fmt, va_list va);
void log_emit_real(const char *file, const char *func, int line, int lev, const char *fmt, ...) __attribute__((format(printf,5,6)));
void log_debugf(const char *fmt, ...) __attribute__((format(printf,1,2)));
bool log_p(int lev);

#define log_emit(LEV, FMT, ARGS...) do {\
        if( log_p(LEV) ) {\
                log_emit_real(__FILE__,__FUNCTION__,__LINE__, LEV, FMT, ##ARGS);\
        }\
} while(0)

#define log_crit(    FMT, ARGS...)   log_emit(LOG_CRIT,    FMT, ##ARGS)
#define log_err(     FMT, ARGS...)   log_emit(LOG_ERR,     FMT, ##ARGS)
#define log_warning( FMT, ARGS...)   log_emit(LOG_WARNING, FMT, ##ARGS)

#if LOG_ENABLE_DEBUG
# define log_notice( FMT, ARGS...)   log_emit(LOG_NOTICE,  FMT, ##ARGS)
# define log_info(   FMT, ARGS...)   log_emit(LOG_INFO,    FMT, ##ARGS)
# define log_debug(  FMT, ARGS...)   log_emit(LOG_DEBUG,   FMT, ##ARGS)
#else
# define log_notice( FMT, ARGS...)   do{}while(0)
# define log_info(   FMT, ARGS...)   do{}while(0)
# define log_debug(  FMT, ARGS...)   do{}while(0)

# define log_debugf( FMT, ARGS...)   do{}while(0)
#endif

