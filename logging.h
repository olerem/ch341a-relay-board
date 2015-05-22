/* 
 * Taken from http://git.libwebsockets.org/cgi-bin/cgit/libwebsockets/
 * written by Andy Green (GPL)
 */

#ifndef LOGGING_H
#define	LOGGING_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <syslog.h>
#include <sys/time.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>

#ifndef LWS_EXTERN
#define LWS_EXTERN extern
#endif

enum lws_log_levels {
    LLL_ERR = 1 << 0,
    LLL_WARN = 1 << 1,
    LLL_NOTICE = 1 << 2,
    LLL_INFO = 1 << 3,
    LLL_DEBUG = 1 << 4,

    LLL_COUNT = 5 /* set to count of valid flags */
};

extern void _lws_log(int filter, const char *format, ...);

/* notice, warn and log are always compiled in */
#define lwsl_notice(...) _lws_log(LLL_NOTICE, __VA_ARGS__)
#define lwsl_warn(...) _lws_log(LLL_WARN, __VA_ARGS__)
#define lwsl_err(...) _lws_log(LLL_ERR, __VA_ARGS__)
#define lwsl_info(...) _lws_log(LLL_INFO, __VA_ARGS__)
#define lwsl_debug(...) _lws_log(LLL_DEBUG, __VA_ARGS__)


static int log_level = LLL_ERR | LLL_WARN | LLL_NOTICE;
void lwsl_emit_stderr(int level, const char *line);
void lwsl_emit_syslog(int level, const char *line);
void (*lwsl_emit)(int level, const char *line); // = lwsl_emit_stderr;

void lws_set_log_level(int level, void (*log_emit_function)(int level,
        const char *line));

static const char * const log_level_names[] = {
    "ERR",
    "WARN",
    "NOTICE",
    "INFO",
    "DEBUG",
};

#ifdef	__cplusplus
}
#endif

#endif	/* LOGGING_H */
