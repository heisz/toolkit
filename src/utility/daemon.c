/*
 * Collection of elements related to building a daemon service instance.
 *
 * Copyright (C) 1999-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "log.h"

/* Track the process/pid file details for cleanup */
static const char *pidTrkFileName = NULL;
static int pidTrkFd = -1;

/**
 * Some very old code (thank you Mr. Stevens!) for altering a process into a
 * daemon/service.  This call has been traditionally managed by either a -d
 * flag (on) or a -t flag (test, off).
 *
 * @param rootDir Root directory that the daemon process should run within.
 * @param appName Short name of the application, for syslog support.
 * @param pidFileName Full path to the filename to store the process identifier
 *                    into (for signalling).
 * @param logFileName Target file (appended) for logging in daemon mode.
 * @param signalHandler Reference to a provided method to handle the INT and
 *                      HUP signals for termination and configuration reload,
 *                      respectively.
 */
void daemonStart(const char *rootDir, const char *appName,
                 const char *pidFileName, const char *logFileName,
                 void (*signalHandler)(int)) {
    struct sigaction sigAction;
    pid_t childpid;
    char buff[64];
    int idx;

    /* Begin the long slippery slope to d(a)emonic behaviour */
    if ((childpid = fork()) < 0) {
        (void) fprintf(stderr, "Unable to fork daemon process: %s.",
                       strerror(errno));
        exit(1);
    } else if (childpid > 0) {
        /* Parent process returns */
        exit(0);
    }

    /* Take control of the session and lose controlling tty */
    (void) setsid();

    /* Ignore the hangup from the next fork */
    sigAction.sa_handler = SIG_IGN;
    sigemptyset(&(sigAction.sa_mask));
    sigAction.sa_flags = 0;
    if (sigaction(SIGHUP, &sigAction, NULL) != 0) {
        (void) fprintf(stderr, "Unable to ignore HUP signal: %s.",
                       strerror(errno));
        exit(1);
    }

    /* And fork again to completely lose the process group (no tty) */
    if ((childpid = fork()) < 0) {
        (void) fprintf(stderr, "Unable to fork daemon process (again): %s.",
                       strerror(errno));
        exit(1);
    } else if (childpid > 0) {
        /* Done with the intermediate child */
        exit(0);
    }

    /* Head for home and reset file permissions */
    /* In theory, both of these should check for error, can't void the first */
    if (chdir(rootDir) < 0) {
        (void) fprintf(stderr, "Unable to change to root directory %s: %s.",
                       rootDir, strerror(errno));
    }
    (void) umask(0);

    /* Detach all dangling filehandles */
    for (idx = sysconf(_SC_OPEN_MAX); idx >= 0; idx--) {
        (void) close(idx);
    }

    /* Redirect the standard file handles (replaced with logging) */
    stdin = fopen("/dev/null", "r");
    stdout = fopen("/dev/null", "w+");
    stderr = fopen("/dev/null", "w+");

    /* So, here instead, direct logging to the target file */
    WXLog_Init(appName, logFileName);

    /* Generate and lock the pid file, if requested */
    if (pidFileName != NULL) {
        pidTrkFileName = pidFileName;
        pidTrkFd = open(pidTrkFileName, O_RDWR | O_CREAT, 0640);
        if (pidTrkFd < 0) {
            WXLog_Error("Unable to open pid file: %s.", strerror(errno));
        } else {
            if (lockf(pidTrkFd, F_TLOCK, 0) < 0) {
                WXLog_Error("Unable to lock pid file: %s.", strerror(errno));
            } else {
                (void) sprintf(buff, "%d\n", getpid());
                if (write(pidTrkFd, buff, strlen(buff)) < 0) {
                    WXLog_Error("Unable to write pid: %s.", strerror(errno));
                }
                /* Don't close it, we are holding the lock */
            }
        }
    }

    /* And good little daemons use these signals */
    if (signalHandler != NULL) {
        sigAction.sa_handler = signalHandler;
        sigemptyset(&(sigAction.sa_mask));
        sigAction.sa_flags = 0;

        if (sigaction(SIGHUP, &sigAction, NULL) != 0) {
            WXLog_Error("Unable to capture HUP signal: %s.", strerror(errno));
        }
        if (sigaction(SIGINT, &sigAction, NULL) != 0) {
            WXLog_Error("Unable to capture INT signal: %s.", strerror(errno));
        }
        if (sigaction(SIGTERM, &sigAction, NULL) != 0) {
            WXLog_Error("Unable to capture TERM signal: %s.", strerror(errno));
        }
    }
    /* Note: SIGBUS and SIGSEGV could also be handled, but not generic... */
}

/**
 * Matching method for daemon termination, unlock and release the pid file.
 */
void daemonStop() {
    if (pidTrkFd >= 0) {
        if (lockf(pidTrkFd, F_ULOCK, 0) < 0) {
            WXLog_Error("Unable to unlock pid file: %s.", strerror(errno));
        }
        (void) close(pidTrkFd);
        pidTrkFd = -1;
    }
    if (pidTrkFileName != NULL) {
        (void) unlink(pidTrkFileName);
        pidTrkFileName = NULL;
    }
}
