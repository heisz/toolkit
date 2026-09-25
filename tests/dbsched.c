/*
 * Lightweight test to validate the database wrapper executing against fibers.
 *
 * Copyright (C) 2025-2026 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include <stdatomic.h>
#include <unistd.h>
#include "dbxf.h"
#include "scheduler.h"
#include "channel.h"
#include "thread.h"
#include "mem.h"

/* Number of fast queries the second fiber attempts while the slow query runs */
#define FAST_QUERY_COUNT 20

/* Duration of the deliberately slow query, in seconds */
#define SLOW_QUERY_SECONDS "2"

static WXDBConnectionPool pool;

/* Signals the second fiber that the slow query is about to run */
static struct GMPS_Channel *startChannel;

static _Atomic(int) slowStarted = 0;
static _Atomic(int) slowFinished = 0;
static _Atomic(int) fastDuringSleep = 0;
static _Atomic(int) fastCompleted = 0;
static _Atomic(int) fibersFinished = 0;
static _Atomic(int) failureCount = 0;

static void testFailed(const char *msg, const char *detail) {
    (void) fprintf(stderr, "Error: %s%s%s\n", msg,
                   ((detail == NULL) ? "" : " - "),
                   ((detail == NULL) ? "" : detail));
    atomic_fetch_add(&failureCount, 1);
}

/* Need a periodic poller call to support the database network connections */
static void *netpollThread(void *arg) {
    while (TRUE) {
        (void) GMPS_NetPoll(50);
    }

    return NULL;
}

/* Generate the results summary, the last fiber issues it */
static void fiberFinished() {
    int during;

    if (atomic_fetch_add(&fibersFinished, 1) != 1) return;

    during = atomic_load(&fastDuringSleep);
    (void) fprintf(stdout, "Fast queries during slow query: %d of %d\n",
                   during, atomic_load(&fastCompleted));

    if (during < FAST_QUERY_COUNT / 2) {
        testFailed("slow query did not yield to fast execution", NULL);
    }

    WXDBConnectionPool_Destroy(&pool);

    if (atomic_load(&failureCount) != 0) {
        (void) fprintf(stderr, "\n%d test failure(s)\n",
                       atomic_load(&failureCount));
    } else {
        (void) fprintf(stdout, "All scheduled database tests passed.\n");
    }

    (void) fflush(stdout);
    (void) fflush(stderr);
    exit((atomic_load(&failureCount) != 0) ? 1 : 0);
}

static void slowFiber(void *arg) {
    WXDBConnection *conn;
    WXDBResultSet *rs;

    conn = WXDBConnectionPool_Obtain(&pool);
    if (conn == NULL) {
        testFailed("slow fiber could not obtain a connection",
                   WXDB_GetLastErrorMessage(&pool));

        /* Still needs to signal so fast fiber exits */
        atomic_store(&slowFinished, 1);
        (void) GMPS_ChannelSend(startChannel, NULL);
        fiberFinished();
        return;
    }

    /* Issue a snoozing query after marking in progress */
    atomic_store(&slowStarted, 1);
    (void) GMPS_ChannelSend(startChannel, NULL);
    rs = WXDBConnection_ExecuteQuery(conn,
                                     "SELECT pg_sleep(" SLOW_QUERY_SECONDS ")");
    if (rs == NULL) {
        testFailed("slow sleep query", WXDB_GetLastErrorMessage(conn));
    } else {
        WXDBResultSet_Close(rs);
    }

    atomic_store(&slowFinished, 1);
    WXDBConnectionPool_Return(conn);
    fiberFinished();
}

static void fastFiber(void *arg) {
    WXDBConnection *conn;
    WXDBResultSet *rs;
    const char *emsg;
    int idx;

    conn = WXDBConnectionPool_Obtain(&pool);
    if (conn == NULL) {
        testFailed("fast fiber could not obtain a connection",
                   WXDB_GetLastErrorMessage(&pool));
        fiberFinished();
        return;
    }

    /* Wait for the slow query to start */
    (void) GMPS_ChannelRecv(startChannel, NULL);
    if (atomic_load(&slowStarted) == 0) {
        testFailed("the slow query never started", NULL);
    }

    /* Run a series of very fast queries */
    for (idx = 0; idx < FAST_QUERY_COUNT; idx++) {
        rs = WXDBConnection_ExecuteQuery(conn, "SELECT 1 AS one");
        if (rs == NULL) {
            testFailed("quick query", WXDB_GetLastErrorMessage(conn));
            break;
        }
        if (!WXDBResultSet_NextRow(rs)) {
            testFailed("quick query returned no rows", NULL);
        }
        WXDBResultSet_Close(rs);

        atomic_fetch_add(&fastCompleted, 1);
        if (atomic_load(&slowFinished) == 0) {
            atomic_fetch_add(&fastDuringSleep, 1);
        }
    }

    /* Also check the error flow when fiber is involved */
    if (WXDBConnection_Execute(conn, "SELECT * FROM _xyzzy_") == WXDRC_OK) {
        testFailed("bad query unexpectedly succeeded", NULL);
    } else {
        emsg = WXDB_GetLastErrorMessage(conn);
        if (strlen(emsg) == 0) {
            testFailed("bad query reported no error message", NULL);
        }
    }

    /* Verify the connection is still operational */
    rs = WXDBConnection_ExecuteQuery(conn, "SELECT 1 AS one");
    if (rs == NULL) {
        testFailed("query after a failure", WXDB_GetLastErrorMessage(conn));
    } else {
        WXDBResultSet_Close(rs);
    }

    WXDBConnectionPool_Return(conn);
    fiberFinished();
}

/**
 * Where all of the fun begins!
 */
int main(int argc, char **argv) {
    char *user = NULL, *password = NULL, *dsn = NULL;
    WXThread npThread;
    int idx;

    for (idx = 1; idx < argc; idx++) {
        if (strcmp(argv[idx], "-u") == 0) {
            if (idx >= (argc - 1)) {
                (void) fprintf(stderr, "Error: missing -u <user> argument\n");
                exit(1);
            }
            user = argv[++idx];
        } else if (strcmp(argv[idx], "-p") == 0) {
            if (idx >= (argc - 1)) {
                (void) fprintf(stderr, "Error: missing -p <pwd> argument\n");
                exit(1);
            }
            password = argv[++idx];
        } else {
            dsn = argv[idx];
        }
    }
    if (dsn == NULL) {
        (void) fprintf(stderr, "Missing DSN argument\n");
        exit(1);
    }

    /* Ultimate failsafe, must be longer than slow query! */
    (void) alarm(30);

    /* Enforce only one processor for force fiber yield conflict */
    if (!GMPS_SchedulerInit(1)) {
        (void) fprintf(stderr, "Failed to initialize the scheduler\n");
        exit(1);
    }

    WXDB_SetSocketHandlers(GMPS_SocketWait, GMPS_SocketRelease);

    if (WXDBConnectionPool_Init(&pool, dsn, user, password, 1) < 0) {
        (void) fprintf(stderr, "Failed to initialize the test pool: %s\n",
                       WXDB_GetLastErrorMessage(&pool));
        exit(1);
    }

    /* Create a buffered channel so the slow fiber can signal without wait */
    startChannel = GMPS_ChannelCreate(1);
    if (startChannel == NULL) {
        (void) fprintf(stderr, "Failed to create the start channel\n");
        exit(1);
    }

    /* Start the two test fibers */
    if (GMPS_Start(slowFiber, NULL) == NULL) {
        (void) fprintf(stderr, "Failed to start the slow fiber\n");
        exit(1);
    }
    if (GMPS_Start(fastFiber, NULL) == NULL) {
        (void) fprintf(stderr, "Failed to start the fast fiber\n");
        exit(1);
    }

    /* And the polling instance to handle socket events */
    if (WXThread_Create(&npThread, netpollThread, NULL) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to create the netpoll thread\n");
        exit(1);
    }

    GMPS_SchedulerStart();

    return 1;
}
