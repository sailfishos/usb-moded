/**
 * @file usb_moded-log.c
 *
 * Copyright (c) 2010 Nokia Corporation. All rights reserved.
 * Copyright (c) 2016 - 2021 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * @author Philippe De Swert <philippe.de-swert@nokia.com>
 * @author Simo Piiroinen <simo.piiroinen@nokia.com>
 * @author Philippe De Swert <phdeswer@lumi.maa>
 * @author Philippe De Swert <philippedeswert@gmail.com>
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

#include "usb_moded-log.h"

#include <sys/time.h>

#include <stdio.h>
#include <errno.h>

#if LOG_ENABLE_CONTEXT
# include <assert.h> // NOTRIM
#endif

/* ========================================================================= *
 * Prototypes
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * LOG
 * ------------------------------------------------------------------------- */

static char *log_strip       (char *str);
static void  log_gettime     (struct timeval *tv);
void         log_emit_va     (const char *file, const char *func, int line, int lev, const char *fmt, va_list va);
void         log_emit_real   (const char *file, const char *func, int line, int lev, const char *fmt, ...);
void         log_debugf      (const char *fmt, ...);
int          log_get_level   (void);
void         log_set_level   (int lev);
bool         log_p           (int lev);
int          log_get_type    (void);
void         log_set_type    (int type);
const char  *log_get_name    (void);
void         log_set_name    (const char *name);
void         log_set_lineinfo(bool lineinfo);
bool         log_get_lineinfo(void);
void         log_init        (void);

/* ========================================================================= *
 * Data
 * ========================================================================= */

static const char *log_name = "<unset>";
static int log_level = LOG_WARNING;
static int log_type  = LOG_TO_STDERR;
static bool log_lineinfo = false;
static struct timeval log_begtime = { 0, 0 };

/* ========================================================================= *
 * CONTEXT STACK
 * ========================================================================= */

#if LOG_ENABLE_CONTEXT
typedef struct context_entry_t
{
    const char *func;
    bool        done;
} context_entry_t;

typedef struct context_stack_t
{
    context_entry_t stk[256];
    int             sp;
    int             id;
} context_stack_t;

static int context_count = 0;
static __thread context_stack_t *context_stack = 0;

static bool log_entry = false;
static bool log_leave = false;

static void
context_write(int tab, const char *msg)
{
    int tag = 0;
    if( context_stack ) {
        tag = context_stack->id;
        if( tab < 0 && context_stack->sp > 0 )
            tab = context_stack->sp;
    }
    tab = (tab <= 0) ? 0 : (tab * 4);
    char *txt = 0;
    int   len = asprintf(&txt, "T%d %*s%s\n",
                         tag,
                         tab, "",
                         msg);
    if( len > 0 ) {
        if( write(STDERR_FILENO, txt, len) == - 1 ) {
            // this is debug logging - do not really care
        }
        free(txt);
    }
}

static void
context_flush(void)
{
    for( int i = 0; i < context_stack->sp; ++i ) {
        char msg[256];
        if( context_stack->stk[i].done )
            continue;
        context_stack->stk[i].done = true;
        if( log_leave )
            snprintf(msg, sizeof msg, "%s() { ...",
                     context_stack->stk[i].func);
        else
            snprintf(msg, sizeof msg, "%s()",
                     context_stack->stk[i].func);
        context_write(i, msg);
    }
}

const char *
context_enter(const char *func)
{
    if( !context_stack ) {
        context_stack = calloc(1, sizeof *context_stack);
        context_stack->id = ++context_count;
    }

    context_stack->stk[context_stack->sp].func = func;
    context_stack->stk[context_stack->sp].done = false;
    context_stack->sp += 1;

    if( log_entry )
        context_flush();

    return func;
}

void
context_leave(void *aptr)
{
    const char *func = *(const char **)aptr;
    assert( context_stack->sp > 0 );
    context_stack->sp -= 1;

    if( log_leave && context_stack->stk[context_stack->sp].done ) {
        char msg[256];
        snprintf(msg, sizeof msg, "} %s()", func);
        context_write(-1, msg);
    }
    assert( context_stack->stk[context_stack->sp].func == func );
}
#endif // LOG_ENABLE_CONTEXT

/* ========================================================================= *
 * Functions
 * ========================================================================= */

static char *log_strip(char *str)
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

static void log_gettime(struct timeval *tv)
{
    gettimeofday(tv, 0);
    timersub(tv, &log_begtime, tv);
}

/** Print the logged messages to the selected output
 *
 * @param file  Source file name
 * @param func  Function name
 * @param line  Line in source file
 * @param lev   The wanted log level
 * @param fmt   The message format string
 * @param va    Arguments for the format string
 */
void log_emit_va(const char *file, const char *func, int line, int lev, const char *fmt, va_list va)
{
    int saved = errno;
    char lineinfo[128] = "";
    char timeinfo[32] = "";
    char levelinfo[8] = "";
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
                snprintf(lineinfo, sizeof lineinfo,
                         "%s:%d: %s(): ", file, line, func);
            }
            else {
                snprintf(lineinfo, sizeof lineinfo,
                         "%s: ", log_get_name());
            }

#if LOG_ENABLE_TIMESTAMPS
            {
                struct timeval tv;
                log_gettime(&tv);
                snprintf(timeinfo, sizeof timeinfo,
                         "%3ld.%03ld ",
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
                snprintf(levelinfo, sizeof levelinfo,
                         "%s ", tag);
            }
#endif
            {
                // squeeze whitespace like syslog does
                char msg[512];
                errno = saved;
                vsnprintf(msg, sizeof msg, fmt, va);
                log_strip(msg);
#if LOG_ENABLE_CONTEXT
                char buf[1024];
                snprintf(buf, sizeof buf, "%s%s%s%s",
                         lineinfo, timeinfo, levelinfo, msg);
                context_flush();
                context_write(-1, buf);
#else
                fprintf(stderr, "%s%s%s%s\n",
                        lineinfo, timeinfo, levelinfo, msg);
#endif
            }
            fflush(stderr);
            break;

        default:
            // no logging
            break;
        }
    }
    errno = saved;
}

/** Print the logged messages to the selected output
 *
 * @param file  Source file name
 * @param func  Function name
 * @param line  Line in source file
 * @param lev   The wanted log level
 * @param fmt   The message format string
 * @param ...   Arguments for the format string
 */
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
