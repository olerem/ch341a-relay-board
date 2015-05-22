/*
 * http://git.libwebsockets.org/cgi-bin/cgit/libwebsockets/
 * Taken from libwebsockets by Andy Green (GPL)
 * $Id:$
 */

#include "logging.h"

void
lwsl_emit_stderr(int level, const char *line)
{
    char buf[300];
    struct timeval tv;
    int n;

    gettimeofday(&tv, NULL);

    buf[0] = '\0';
    for (n = 0; n < LLL_COUNT; n++)
        if (level == (1 << n)) {
            sprintf(buf, "[%ld:%04d] %s: ", tv.tv_sec,
                    (int) (tv.tv_usec / 100), log_level_names[n]);
            break;
        }

    fprintf(stderr, "%s%s", buf, line);
}

void
lwsl_emit_syslog(int level, const char *line)
{
    int syslog_level = LOG_DEBUG;

    switch (level) {
    case LLL_ERR:
        syslog_level = LOG_ERR;
        break;
    case LLL_WARN:
        syslog_level = LOG_WARNING;
        break;
    case LLL_NOTICE:
        syslog_level = LOG_NOTICE;
        break;
    case LLL_INFO:
        syslog_level = LOG_INFO;
        break;
    }
    syslog(syslog_level, "%s", line);
}

void
_lws_log(int filter, const char *format, ...)
{
    char buf[256];
    va_list ap;

    if (!(log_level & filter))
        return;

    va_start(ap, format);
    vsnprintf(buf, sizeof (buf), format, ap);
    buf[sizeof (buf) - 1] = '\0';
    va_end(ap);

    lwsl_emit(filter, buf);
}

/**
 * lws_set_log_level() - Set the logging bitfield
 * @level:	OR together the LLL_ debug contexts you want output from
 * @log_emit_function:	NULL to leave it as it is, or a user-supplied
 *			function to perform log string emission instead of
 *			the default stderr one.
 *
 *	log level defaults to "err", "warn" and "notice" contexts enabled and
 *	emission on stderr.
 */

void
lws_set_log_level(int level, void (*log_emit_function)(int level,
                  const char *line))
{
    log_level = level;
    if (log_emit_function)
        lwsl_emit = log_emit_function;
}
