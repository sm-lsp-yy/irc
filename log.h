/*
 *  Logging functions
 *
 *  Use these functions to print log messages. Each message has an
 *  associated log level:
 *
 *  CRITICAL: A critical unrecoverable error
 *  ERROR: A recoverable error
 *  WARNING: A warning
 *  INFO: High-level information about the progress of the application
 *  DEBUG: Lower-level information
 *  TRACE: Very low-level information.
 *
 */

#ifndef CHIRC_LOG_H_
#define CHIRC_LOG_H_

/* Log levels */
typedef enum {
    QUIET    = 00,
    CRITICAL = 10,
    ERROR    = 20,
    WARNING  = 30,
    INFO     = 40,
    DEBUG    = 50,
    TRACE    = 60
} loglevel_t;

/*
 * chitcp_setloglevel - Sets the logging level
 *
 * When a log level is set, all messages at that level or "worse" are
 * printed. e.g., if you set the log level to WARNING, then all
 * WARNING, ERROR, and CRITICAL messages will be printed.
 *
 * level: Logging level
 *
 * Returns: Nothing.
 */
void chirc_setloglevel(loglevel_t level);


/*
 * chilog - Print a log message
 *
 * level: Logging level of the message
 *
 * fmt: printf-style formatting string
 *
 * ...: Extra parameters if needed by fmt
 *
 * Returns: nothing.
 */
void chilog(loglevel_t level, char *fmt, ...);


#endif /* CHIRC_LOG_H_ */




/*
 *  chirc
 *
 *  Logging functions
 *
 *  see log.h for descriptions of functions, parameters, and return values.
 *
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

/* Logging level. Set by default to print just informational messages */
static int loglevel = INFO;


void chirc_setloglevel(loglevel_t level)
{
    loglevel = level;
}

/* This function does the actual logging and is called by chilog().
 * It has a va_list parameter instead of being a variadic function */
void __chilog(loglevel_t level, char *fmt, va_list argptr)
{
    time_t t;
    char buf[80], *levelstr;

    if(level > loglevel)
        return;

    t = time(NULL);
    strftime(buf,80,"%Y-%m-%d %H:%M:%S",localtime(&t));

    switch(level)
    {
    case CRITICAL:
        levelstr = "CRITIC";
        break;
    case ERROR:
        levelstr = "ERROR";
        break;
    case WARNING:
        levelstr = "WARN";
        break;
    case INFO:
        levelstr = "INFO";
        break;
    case DEBUG:
        levelstr = "DEBUG";
        break;
    case TRACE:
        levelstr = "TRACE";
        break;
    default:
        levelstr = "UNKNOWN";
        break;
    }

    flockfile(stdout);
    printf("[%s] %6s ", buf, levelstr);

    vprintf(fmt, argptr);
    printf("\n");
    funlockfile(stdout);
    fflush(stdout);
}

void chilog(loglevel_t level, char *fmt, ...)
{
    va_list argptr;

    va_start(argptr, fmt);
    __chilog(level, fmt, argptr);
    va_end(argptr);
}





