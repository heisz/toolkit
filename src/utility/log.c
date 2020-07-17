/*
 * Basic implementation of a logging facility for server platforms.
 *
 * Copyright (C) 2001-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "log.h"
#include "thread.h"
#include "mem.h"
#include <stdarg.h>
#include <syslog.h>

/* Baseline non-allocated logging message size */
#define MESSAGE_SIZE 1024

/* Various static lists aligned to the logging levels */
/*
static const int logLevelSyslog[] = {
    LOG_ERR, LOG_WARNING, LOG_INFO, LOG_DEBUG
};

static const char *logLevelNames[] = {
     "Error", "Warn", "Info", "Debug", "Trace"
};
*/

static const char *shortLogLevelNames[] = {
     "Err", "Wrn", "Inf", "Dbg", "Trc"
};

static FILE *logFileHandles[5];

/**
 * Initialize the logging framework for the core application.
 *
 * @param appName The short name of the application, for syslog support.
 * @param logFileName If non-NULL, direct logging to this file, otherwise
 *                    standard console is used (non-daemon mode).
 */
void WXLog_Init(const char *appName, const char *logFileName) {
    FILE *fp;

#ifndef _WXWIN_BUILD
    /* Initialize syslog for the error/warning instances */
    if (appName != NULL) openlog(appName, LOG_NDELAY | LOG_PID, LOG_USER);
#else
    /* TODO - push to app event viewer */
#endif

    /* If specified, change the logging handles to the target file instance */
    if (logFileName != NULL) {
        fp = fopen(logFileName, "a+");
        if (fp != NULL) {
            logFileHandles[WXLOG_ERROR] = logFileHandles[WXLOG_WARN] =
                logFileHandles[WXLOG_INFO] = logFileHandles[WXLOG_DEBUG] =
                logFileHandles[WXLOG_TRACE] = fp;
            return;
        }
    }

    /* Fallback to console */
    logFileHandles[WXLOG_ERROR] = logFileHandles[WXLOG_WARN] = stderr;
    logFileHandles[WXLOG_INFO] = logFileHandles[WXLOG_DEBUG] =
        logFileHandles[WXLOG_TRACE] = stdout;
}

/**
 * Convenience method to obtain the basename 
 * Unfortunately, GCC's __FILE__ macro includes full path information and there
 * is no other macro for the basename.
 */
static char *logFileBaseName(const char *fileName) {
    char *ptr = (char *) fileName;
#ifndef _WXWIN_BUILD
#define SLASH_CH '/'
#else
#define SLASH_CH '\\'
#endif

    while (*fileName != '\0') {
        if (*(fileName++) == SLASH_CH) ptr = (char *) fileName;
    }

    return ptr;
}

/**
 * Logging utility method to get a consistent timestamp for applications
 * using this logging framework.
 *
 * @param timestamp Character buffer to write formatted timestamp into.
 */
void WXLog_GetFormattedTimestamp(char *timestamp) {
    WXThread_TimeSpec tmspec;
#ifdef _WXWIN_BUILD
    struct tm *tm;
#endif
    struct tm ltm;

    WXThread_GetEpochTime(&tmspec);

    /* Obtain logging timestamp */
#ifndef _WXWIN_BUILD
    localtime_r(&(tmspec.tv_sec), &ltm);
#else
    tm = localtm(&(tmspec.tv_sec));
    if (tm != NULL) {
        (void) memcpy(&ltm, tm, sizeof(struct tm));
    }
#endif
    (void) sprintf(timestamp, "%d-%2.2d-%2.2d %2.2d:%2.2d:%2.2d.%3.3d",
                   (1900 + ltm.tm_year), ltm.tm_mon + 1, ltm.tm_mday,
                   ltm.tm_hour, ltm.tm_min, ltm.tm_sec,
                   (int) (tmspec.tv_nsec / 1000000L));
}

/**
 * Base-level logging method, wrapped by supporting definitions below.
 * Note that the file name is hijacked for the logging context string
 * (negative line number).
 *
 * @param fileName The name of the associated file for the logging origin,
 *                 as provided by the compiler macro.
 * @param lineNum The line number of the associated logging origin, as provided
 *                by the compiler macro.
 * @param level The enumerated level of the associated logging entry.
 * @param format Printf-based formatting for the log message.
 * @param ap The allocated vararg instance.  Note that the state of this
 *           is indeterminant after the call.
 * @param ... Extended argument set based on the provided printf format.
 */
void _WXLog_VPrint(const char *fileName, const int lineNum, WXLogLevel level,
                   const char *format, va_list src) {
    char *msgPtr, message[MESSAGE_SIZE], timestamp[128];
    size_t allocLen = 0;
    FILE *logFp;
    va_list ap;
    int len;

    /* Capture pre-initialization conditions */
    if ((logFp = logFileHandles[level]) == NULL) {
        logFp = (level < WXLOG_INFO) ? stderr : stdout;
    }

    /* Grab the logging timestamp right up front */
    WXLog_GetFormattedTimestamp(timestamp);

    /* Format the message, handling automatic allocation as needed */
    msgPtr = message;
    allocLen = MESSAGE_SIZE;
    while (TRUE) {
        /* Attempt the formatting, done if it fits */
        va_copy(ap, src);
        len = vsnprintf(msgPtr, allocLen, format, ap);
        va_end(ap);

        if (len < 0) {
            allocLen *= 2;
        } else if (len > (int) allocLen) {
            allocLen = len + 2;
        } else {
            break;
        }

        /* Unable to format into buffer, reallocate */
        if (msgPtr != message) WXFree(msgPtr);
        msgPtr = WXMalloc(allocLen);
        if (msgPtr == NULL) {
            (void) strcpy(msgPtr = message,
                          "Unable to allocate message formatting buffer");
            break;
        }
    }

    /* Out it goes! */
    if (lineNum >= 0) {
        (void) fprintf(logFp, "%s %c [%s:%d] %s\n",
                       timestamp, *(shortLogLevelNames[level]),
                       logFileBaseName(fileName), lineNum, msgPtr);
    } else {
        (void) fprintf(logFp, "%s %c [%s] %s\n",
                       timestamp, *(shortLogLevelNames[level]),
                       fileName, msgPtr);
    }
    (void) fflush(logFp);

    /* Clean up if buffer was allocated */
    if (msgPtr != message) WXFree(msgPtr);
}

void _WXLog_Print(const char *fileName, const int lineNum, WXLogLevel level,
                  const char *format, ...) {
    va_list ap;

    /* Just initiate the vararg set and jump to the ap list method */
    va_start(ap, format);
    _WXLog_VPrint(fileName, lineNum, level, format, ap);
    va_end(ap);
}

/**
 * Standard logging implementation of the logger interface, using the
 * same internal logging mechanisms as the WXLog_Print method.
 *
 * @param level The enumerated level of the associated logging entry.
 * @param context A string defining the context of the logged message.
 * @param format Printf-based formatting for the log message.
 * @param ... Extended argument set based on the provided printf format.
 */
void WXLog_Logger(WXLogLevel level, const char *context,
                  const char *format, ...) {
    va_list ap;

    /* Just initiate the vararg set and jump to the ap list method */
    va_start(ap, format);
    _WXLog_VPrint(context, -1, level, format, ap);
    va_end(ap);
}

/**
 * Convenience/utility method to log a blob of binary data, in useful/formatted
 * form.
 *
 * @param content Pointer to the memory content/blob to be logged.
 * @param offset Zero-offset index of the first byte to log from the content.
 * @param length Number of bytes to be logged.
 */
void _WXLog_Binary(const char *fileName, const int lineNum, WXLogLevel level,
                   uint8_t *content, uint32_t offset, uint32_t length) {
    char charBuffer[20], hexStr[4], *message, *str;
    int len, column = 0, hexVal;

    /* Assemble the binary dump */
    message = (char *) WXMalloc(50 + 71 * (length / 16 + 1));
    if (message == NULL) {
        _WXLog_Print(fileName, lineNum, level, 
                     "Failed to allocate memory for binary buffer log.");
        return;
    }

    sprintf(message, "Binary content: %d bytes", length);
    str = message + strlen(message);

    memset(charBuffer, 0, sizeof(charBuffer));
    for (content = content + offset; length > 0; length--, content++) {
        /* If this is the start of a new 16-byte set */
        if (column == 0) {
            *(str++) = ' ';
            len = strlen(charBuffer);
            (void) strncpy(str, charBuffer, len);
            str += len;
            (void) strcpy(str, "\n    ");
            str += 5;
        }

        /* Dump the current byte */
        hexVal = ((int) *content) & 0xFF;
        (void) sprintf(hexStr, "%02x ", hexVal);
        len = strlen(hexStr);
        (void) strncpy(str, hexStr, len);
        str += len;

        if ((hexVal > 31) && (hexVal < 128)) {
            charBuffer[column] = (char) hexVal;
        } else {
            charBuffer[column] = '.';
        }

        /* Check if this is the end of the 16-bytes set */
        if (column < 15) {
            column++;
        } else {
            column = 0;
        }
    }

    /* Assemble the last line - buffer as needed */
    if (column == 0) column = 16;
    charBuffer[column] = '\0';
    while (column++ < 16) {
        *(str++) = ' '; *(str++) = ' '; *(str++) = ' ';
    }
    *(str++) = ' ';
    len = strlen(charBuffer);
    (void) strncpy(str, charBuffer, len);
    str += len;
    *str = '\0';

    _WXLog_Print(fileName, lineNum, level, "%s", message);
    WXFree(message);
}
