/*
 * Basic implementation of a logging facility for server platforms.
 *
 * Copyright (C) 2001-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_LOG_H
#define WX_LOG_H 1

/* Grab the standard definitions */
#include "stdconfig.h"

/* The set of logging levels for the generic logging function */
typedef enum {
    WXLOG_ERROR = 0, WXLOG_WARN, WXLOG_INFO, WXLOG_DEBUG, WXLOG_TRACE
} WXLogLevel;

/**
 * Initialize the logging framework for the core application.
 *
 * @param appName The short name of the application, for syslog support.
 * @param logFileName If non-NULL, direct logging to this file, otherwise
 *                    standard console is used (non-daemon mode).
 */
void WXLog_Init(const char *appName, const char *logFileName);

/**
 * Logging utility method to get a consistent timestamp for applications
 * using this logging framework.
 *
 * @param timestamp Character buffer to write formatted timestamp into.
 */
void WXLog_GetFormattedTimestamp(char *timestamp);

/**
 * Base-level logging method, wrapped by supporting definitions below.
 *
 * @param fileName The name of the associated file for the logging origin,
 *                 as provided by the compiler macro.
 * @param lineNum The line number of the associated logging origin, as provided
 *                by the compiler macro.
 * @param level The enumerated level of the associated logging entry.
 * @param format Printf-based formatting for the log message.
 * @param ... Extended argument set based on the provided printf format.
 */
void _WXLog_Print(const char *fileName, const int lineNum, WXLogLevel level,
#ifndef _WXWIN_BUILD
                  const char *format, ...)
          __attribute__((format(__printf__, 4, 5)));
#else
                  const char *format, ...);
#endif

/* All sorts of variations on the above */
#define WXLog_Print(...) \
        _WXLog_Print(__FILE__, __LINE__, ## __VA_ARGS__)
#define WXLog_Error(...) \
        _WXLog_Print(__FILE__, __LINE__, WXLOG_ERROR, ## __VA_ARGS__)
#define WXLog_Warn(...) \
        _WXLog_Print(__FILE__, __LINE__, WXLOG_WARN, ## __VA_ARGS__)
#define WXLog_Info(...) \
        _WXLog_Print(__FILE__, __LINE__, WXLOG_INFO, ## __VA_ARGS__)
#define WXLog_Debug(...) \
        _WXLog_Print(__FILE__, __LINE__, WXLOG_DEBUG, ## __VA_ARGS__)
#define WXLog_Trace(...) \
        _WXLog_Print(__FILE__, __LINE__, WXLOG_TRACE, ## __VA_ARGS__)

/**
 * Convenience/utility method to log a blob of binary data, in useful/formatted
 * form.
 *
 * @param content Pointer to the memory content/blob to be logged.
 * @param offset Zero-offset index of the first byte to log from the content.
 * @param length Number of bytes to be logged.
 */
void _WXLog_Binary(const char *fileName, const int lineNum, WXLogLevel level,
                   uint8_t *content, uint32_t offset, uint32_t length);

#define WXLog_Binary(...) \
        _WXLog_Binary(__FILE__, __LINE__, ## __VA_ARGS__)

/**
 * Callback model interface for registering logging methods with other
 * toolkit elements requiring logging (for consumers of those systems).
 *
 * @param level The enumerated level of the associated logging entry.
 * @param context A string defining the context of the logged message.
 * @param format Printf-based formatting for the log message.
 * @param ... Extended argument set based on the provided printf format.
 */
typedef void (*WXLogger)(WXLogLevel level, const char *context,
#ifndef _WXWIN_BUILD
                         const char *format, ...)
          __attribute__((format(__printf__, 3, 4)));
#else
                         const char *format, ...);
#endif

/**
 * Standard logging implementation of the callback interface above, using the
 * same internal logging mechanisms as the WXLog_Print method.
 *
 * @param level The enumerated level of the associated logging entry.
 * @param context A string defining the context of the logged message.
 * @param format Printf-based formatting for the log message.
 * @param ... Extended argument set based on the provided printf format.
 */
void WXLog_Logger(WXLogLevel level, const char *context,
#ifndef _WXWIN_BUILD
                  const char *format, ...)
          __attribute__((format(__printf__, 3, 4)));
#else
                   const char *format, ...);
#endif

#endif
