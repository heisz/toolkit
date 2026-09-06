/**
 * Self-verifying testsuite for the M:N fiber scheduler.
 *
 * Copyright (C) 2026 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "stdconfig.h"
#include "scheduler.h"
#include "channel.h"
#include "socket.h"
#include "thread.h"
#include "mem.h"
#include <stdatomic.h>
#include <sys/socket.h>
#include <fcntl.h>

/* Credit where credit is due, AI has greatly expanded the edge cases */

/********** Core test elements **********/

/* Configurable parameters for the various test conditions */
static int stressCount = 2000;
static int connCount = 200;
static int burstCount = 64;
static int procCount = 4;

/* Seconds without activity before watchdog fails and heartbeat to signal it */
static int watchdogLimit = 30;
static _Atomic(uint64_t) heartbeat = 0;
static void thump() {
    (void) atomic_fetch_add(&heartbeat, 1);
}

/* Result tracking and notification chaneel to signal completion */
static int testCount = 0;
static int failCount = 0;
static char *currentTest = "startup";
static GMPS_Channel *doneChannel = NULL;

/* Methods to record/test outcomes */
static void startTest(char *name) {
    currentTest = name;
    testCount++;
    thump();
    (void) fprintf(stderr, "-- %s\n", name);
}

static void testFail(char *detail) {
    failCount++;
    (void) fprintf(stderr, "Error: %s: %s\n", currentTest, detail);
}

static void expectEq(long val, long exp, char *part) {
    if (val == exp) return;

    failCount++;
    (void) fprintf(stderr, "Error: %s: %s (val %ld, exp %ld)\n",
                   currentTest, part, val, exp);
}

/* Worker fibers report completion here, the driver parks until they do */
static void signalDone() {
    (void) GMPS_ChannelSend(doneChannel, (void *) (intptr_t) 1);
}

/* Sleep the current fiber for the provided millitime, under syscall */
static void sleepMillis(int millis) {
    GMPS_EnterSyscall();
    (void) usleep(millis * 1000);
    GMPS_ExitSyscall();
    thump();
}

/* Wait for a number of completion notifications from the done channel */
static void awaitCompletions(int count) {
    void *val;
    int idx;

    for (idx = 0; idx < count; idx++) {
        if (!GMPS_ChannelRecv(doneChannel, &val)) {
            testFail("completion channel closed early");
            return;
        }
        thump();
    }
}

/* Primary watchdog to capture scheduler hang conditions (fatal failure) */
static void *watchdogThread(void *arg) {
    uint64_t last = 0, curr;
    int stalled = 0;

    while (TRUE) {
        /* Limit is in seconds */
        (void) sleep(1);

        curr = atomic_load(&heartbeat);
        if (curr != last) {
            last = curr;
            stalled = 0;
            continue;
        }

        if ((++stalled) < watchdogLimit) continue;

        (void) fprintf(stderr, "\nError: no progress for %d seconds during "
                               "'%s'\n", stalled, currentTest);
        (void) fprintf(stderr, "Lost wakeup signature: runnable work with "
                               "every scheduler thread parked.\n");
        (void) fprintf(stderr, "Compare 'ss -tnap' against "
                               "/proc/%d/fdinfo/<epollfd> - an established "
                               "socket carrying a pending Recv-Q that is "
                               "absent from the epoll set lost its "
                               "registration.\n", (int) getpid());
        _exit(1);
    }

    return NULL;
}

/* Thread runner to handle the standard scheduler netpoll call */
static void *netPollThread(void *arg) {
    while (TRUE) {
        (void) GMPS_NetPoll(500);
    }

    return NULL;
}

/* Wait for the flag to be set with delays (parked fiber) */
static void awaitParked(_Atomic(int) *flag) {
    int idx;

    for (idx = 0; (idx < 100) && (atomic_load(flag) == 0); idx++) {
        sleepMillis(10);
    }
    sleepMillis(50);
}

/* Lots of tests need socket chatter, make a pair of interconnected sockets */
static int makePair(WXSocket *pair) {
    int fds[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return FALSE;
    pair[0] = (WXSocket) fds[0];
    pair[1] = (WXSocket) fds[1];
    (void) WXSocket_SetNonBlockingState(pair[0], TRUE);
    (void) WXSocket_SetNonBlockingState(pair[1], TRUE);

    return TRUE;
}

/* Fill a write socket buffer (flood) so that it is no longer writable */
static int fillSendBuffer(WXSocket sock) {
    char block[4096];
    ssize_t rc;
    int idx;

    /* Should fill before but avoid infinite loop */
    (void) memset(block, 'x', sizeof(block));
    for (idx = 0; idx < 8192; idx++) {
        rc = WXSocket_Send(sock, block, sizeof(block), 0);
        if (rc == 0) return TRUE;
        if (rc < 0) return FALSE;
    }

    return FALSE;
}

/* Many of the tests can share a common pair, with cleanup */
static WXSocket testPair[2];

static int openTestPair() {
    if (!makePair(testPair)) {
        testFail("socketpair failed");
        return FALSE;
    }

    return TRUE;
}

/* For specific tests, close the peer socket to trigger actions on the first */
static void closeTestPeer() {
    WXSocket_Close(testPair[1]);
    testPair[1] = INVALID_SOCKET_FD;
}

static void closeTestPair() {
    (void) GMPS_SocketUnregister(testPair[0]);
    if (testPair[0] != INVALID_SOCKET_FD) WXSocket_Close(testPair[0]);
    if (testPair[1] != INVALID_SOCKET_FD) WXSocket_Close(testPair[1]);
    testPair[0] = testPair[1] = INVALID_SOCKET_FD;
}

/********** Basic fiber run/exit **********/

static _Atomic(int) basicSum = 0;

static void basicFiber(void *arg) {
    (void) atomic_fetch_add(&basicSum, (int) (intptr_t) arg);
    signalDone();
}

static void testFiberBasics() {
    int idx;

    startTest("basic fiber start/exit");

    atomic_store(&basicSum, 0);
    for (idx = 1; idx <= burstCount; idx++) {
        if (GMPS_Start(basicFiber, (void *) (intptr_t) idx) == NULL) {
            testFail("GMPS_Start returned NULL");
            return;
        }
    }
    awaitCompletions(burstCount);

    expectEq(atomic_load(&basicSum), (burstCount * (burstCount + 1)) / 2,
             "basic fiber argument total");

    if (!GMPS_OnFiber()) testFail("GMPS_OnFiber false inside a fiber");
}

/********** Basic fiber yielding **********/

#define YIELD_ROUNDS 500

static _Atomic(int) yieldTotal = 0;

static void yieldFiber(void *arg) {
    int idx;

    for (idx = 0; idx < YIELD_ROUNDS; idx++) {
        (void) atomic_fetch_add(&yieldTotal, 1);

        /* Keep the watchdog happy as we progress */
        if ((idx % 64) == 0) thump();

        GMPS_Yield();
    }
    signalDone();
}

static void testYieldProgress() {
    int idx, fibers = 16;

    startTest("basic fiber yield");

    atomic_store(&yieldTotal, 0);
    for (idx = 0; idx < fibers; idx++) {
        (void) GMPS_Start(yieldFiber, NULL);
    }
    awaitCompletions(fibers);

    expectEq(atomic_load(&yieldTotal), fibers * YIELD_ROUNDS,
             "basic fiber yield total");
}

/********** Fiber-local storage **********/

#define FLS_FIBERS 32

static GMPS_FlsKey flsKeyA, flsKeyB;
static _Atomic(int) flsDestroyed = 0;
static _Atomic(int) flsErrors = 0;

static void flsDestructor(void *value) {
    (void) atomic_fetch_add(&flsDestroyed, 1);
}

static void flsFiber(void *arg) {
    int id = (int) (intptr_t) arg;

    /* Mark FLS values for this fiber based on numerical id */
    if ((!GMPS_FlsSet(flsKeyA, (void *) (intptr_t) (id * 100))) ||
            (!GMPS_FlsSet(flsKeyB, (void *) (intptr_t) (id * 200)))) {
        (void) atomic_fetch_add(&flsErrors, 1);
        signalDone();
        return;
    }

    /* Verify correctness across fiber transfer to different threads */
    GMPS_Yield();

    if ((int) (intptr_t) GMPS_FlsGet(flsKeyA) != (id * 100)) {
        (void) atomic_fetch_add(&flsErrors, 1);
    }
    if ((int) (intptr_t) GMPS_FlsGet(flsKeyB) != (id * 200)) {
        (void) atomic_fetch_add(&flsErrors, 1);
    }

    signalDone();
}

static void testFiberLocalStorage() {
    int idx;

    startTest("basic fiber-local storage");

    atomic_store(&flsErrors, 0);
    atomic_store(&flsDestroyed, 0);

    for (idx = 1; idx <= FLS_FIBERS; idx++) {
        (void) GMPS_Start(flsFiber, (void *) (intptr_t) idx);
    }
    awaitCompletions(FLS_FIBERS);

    expectEq(atomic_load(&flsErrors), 0, "store/retrieve FLS mismatches");

    /* Yield with delays to allow yields to exit and destructors to run */
    for (idx = 0; idx < 200; idx++) {
        GMPS_Yield();
        if ((idx % 20) == 0) sleepMillis(10);
        if (atomic_load(&flsDestroyed) == FLS_FIBERS) break;
    }
    expectEq(atomic_load(&flsDestroyed), FLS_FIBERS,
             "fls value destructor calls");
}

/********** Channel communication **********/

#define CHAN_ITEMS 200

static GMPS_Channel *testChannel = NULL;
static _Atomic(int) chanSum = 0;
static _Atomic(int) chanRecvd = 0;

/* Just stream a set of integers */
static void chanProducer(void *arg) {
    int idx, base = (int) (intptr_t) arg;

    for (idx = 1; idx <= CHAN_ITEMS; idx++) {
        if (!GMPS_ChannelSend(testChannel, (void *) (intptr_t) (base + idx))) {
            break;
        }
    }
    signalDone();
}

/* And add them together */
static void chanConsumer(void *arg) {
    void *val;

    while (GMPS_ChannelRecv(testChannel, &val)) {
        (void) atomic_fetch_add(&chanSum, (int) (intptr_t) val);
        if ((atomic_fetch_add(&chanRecvd, 1) % 64) == 0) thump();
    }
    signalDone();
}

/* Support both buffered and unbuffered channel instances */
static void testChannels(uint32_t capacity, char *label) {
    int producers = 4, consumers = 3, idx;

    startTest(label);
    testChannel = GMPS_ChannelCreate(capacity);
    if (testChannel == NULL) {
        testFail("channel creation failed");
        return;
    }
    atomic_store(&chanSum, 0);
    atomic_store(&chanRecvd, 0);

    /* Launch a set of consumers followed by producers for ordering */
    for (idx = 0; idx < consumers; idx++) {
        (void) GMPS_Start(chanConsumer, NULL);
    }
    for (idx = 0; idx < producers; idx++) {
        (void) GMPS_Start(chanProducer, (void *) (intptr_t) 0);
    }

    /* Wait/close/wait in the opposite order */
    awaitCompletions(producers);
    GMPS_ChannelClose(testChannel);
    awaitCompletions(consumers);

    /* Check and tidy up */
    expectEq(atomic_load(&chanRecvd), producers * CHAN_ITEMS, "items received");
    expectEq(atomic_load(&chanSum),
             (long) producers * ((CHAN_ITEMS * (CHAN_ITEMS + 1)) / 2),
             "sum of received values");

    GMPS_ChannelDestroy(testChannel);
    testChannel = NULL;
}

/********** Basic reader/writer on common desc **********/

static _Atomic(uint32_t) duoReadMask = 0;
static _Atomic(uint32_t) duoWriteMask = 0;
static _Atomic(int) duoErrors = 0;

static void duoReader(void *arg) {
    uint8_t byte = 0;

    atomic_store(&duoReadMask, GMPS_YieldSocket(testPair[0], GMPS_EVT_IN));

    /* Read the single byte message */
    if (WXSocket_Recv(testPair[0], &byte, 1, 0) != 1) {
        (void) atomic_fetch_add(&duoErrors, 1);
    } else if (byte != 'w') {
        (void) atomic_fetch_add(&duoErrors, 1);
    }

    signalDone();
}

static void duoWriter(void *arg) {
    /* Nothing to do except capture the write condition */
    atomic_store(&duoWriteMask, GMPS_YieldSocket(testPair[0], GMPS_EVT_OUT));
    signalDone();
}

/* A reader and a writer hold independent slots on the same descriptor */
static void testReaderAndWriter() {
    int idx;

    startTest("basic read/write on single descriptor");

    if (!openTestPair()) return;
    atomic_store(&duoReadMask, 0);
    atomic_store(&duoWriteMask, 0);

    /* Reader will park waiting for read */
    (void) GMPS_Start(duoReader, NULL);
    for (idx = 0; idx < 200; idx++) GMPS_Yield();

    /* Writer will immediately have writability */
    (void) GMPS_Start(duoWriter, NULL);
    for (idx = 0; idx < 200; idx++) GMPS_Yield();

    /* Awake the reader */
    if (WXSocket_Send(testPair[1], "w", 1, 0) != 1) {
        testFail("wakeup write failed");
    }
    awaitCompletions(2);

    if ((atomic_load(&duoWriteMask) & GMPS_EVT_OUT) == 0) {
        testFail("write event mask missing OUT event");
    }
    if ((atomic_load(&duoReadMask) & GMPS_EVT_IN) == 0) {
        testFail("read event mask missing IN event");
    }
    expectEq(atomic_load(&duoErrors), 0, "read/write errors");

    /* Detach and close */
    closeTestPair();
}

/********** Idle - wake - delivery **********/

static _Atomic(int) pokeConsumed = 0;
static _Atomic(int) pokeErrors = 0;

/* Primary/code pathway - idle for inbound message, wake up and read it */
static void pokeFiber(void *arg) {
    uint32_t evt;
    uint8_t byte;
    ssize_t len;
    int idx;

    for (idx = 0; idx < stressCount; idx++) {
        evt = GMPS_YieldSocket(testPair[0], GMPS_EVT_IN);
        if (evt == 0) {
            (void) atomic_fetch_add(&pokeErrors, 1);
            break;
        }

        len = WXSocket_Recv(testPair[0], &byte, 1, 0);
        if (len != 1) {
            (void) atomic_fetch_add(&pokeErrors, 1);
            break;
        }

        (void) atomic_fetch_add(&pokeConsumed, 1);
        thump();
    }

    signalDone();
}

/* Vice versa, send message, sleep until consumer reads and let system idle */
static void *pokeThread(void *arg) {
    int idx, waited, target;
    uint8_t byte = 'x';

    for (idx = 0; idx < stressCount; idx++) {
        target = idx + 1;

        if (WXSocket_Send(testPair[1], &byte, 1, 0) != 1) {
            (void) fprintf(stderr, "Error: %s: poke write failed\n",
                           currentTest);
            _exit(1);
        }
        thump();

        /* The byte only gets consumed if the wakeup survived full idle */
        waited = 0;
        while (atomic_load(&pokeConsumed) < target) {
            (void) usleep(1000);
            if ((++waited) > 5000) {
                (void) fprintf(stderr, "\nError: %s: wakeup lost after %d "
                                       "successful cycles\n",
                               currentTest, idx);
                (void) fprintf(stderr, "The fiber is stuck on a socket that "
                                       "has data waiting.\n");
                _exit(1);
            }
        }

        /* Let all threads idle down before next message */
        (void) usleep(200 + ((idx % 5) * 100));
    }

    return NULL;
}

static void testIdleWakeDelivery() {
    WXThread poker;

    startTest("delivery with wake from fully idle scheduler");

    if (!openTestPair()) return;
    atomic_store(&pokeConsumed, 0);
    atomic_store(&pokeErrors, 0);

    /* Start the reader as a fiber */
    (void) GMPS_Start(pokeFiber, NULL);

    /* Writer runs on thread to allow more scheduler idling */
    GMPS_EnterSyscall();
    if (WXThread_Create(&poker, pokeThread, NULL) != WXTRC_OK) {
        GMPS_ExitSyscall();
        testFail("could not create thread");
        closeTestPair();
        return;
    }
    GMPS_ExitSyscall();

    awaitCompletions(1);

    expectEq(atomic_load(&pokeErrors), 0, "socket wait/read errors");
    expectEq(atomic_load(&pokeConsumed), stressCount, "bytes consumed");

    closeTestPair();
}

/********** Accept/request handling **********/

static WXSocket svcSocket = 0;
static uint32_t svcPort = 0;
static _Atomic(int) svcServed = 0;
static _Atomic(int) svcErrors = 0;
static int svcTarget = 0;

/* Fiber to process request content from accepted connection */
static void connHandler(void *arg) {
    WXSocket sock = (WXSocket) (uintptr_t) arg;
    uint8_t buff[16];
    uint32_t evt;
    ssize_t len;

    /* Wait for the incoming message */
    evt = GMPS_YieldSocket(sock, GMPS_EVT_IN);
    if (evt == 0) {
        (void) atomic_fetch_add(&svcErrors, 1);
        (void) GMPS_SocketUnregister(sock);
        WXSocket_Close(sock);
        return;
    }

    /* Read it (TODO - validate? we don't mess with sockets, so nah) */
    len = WXSocket_Recv(sock, buff, sizeof(buff), 0);
    if (len <= 0) {
        (void) atomic_fetch_add(&svcErrors, 1);
        (void) GMPS_SocketUnregister(sock);
        WXSocket_Close(sock);
        return;
    }

    /* Send an appropriate response and close the connection */
    if (WXSocket_Send(sock, "pong", 4, 0) != 4) {
        (void) atomic_fetch_add(&svcErrors, 1);
    }
    (void) GMPS_SocketUnregister(sock);
    WXSocket_Close(sock);

    /* Last one signals completion of the connection instances */
    if ((atomic_fetch_add(&svcServed, 1) + 1) == svcTarget) {
        signalDone();
    }
}

/* Primary fiber to handle incoming connections and spawn request handler */
static void connAcceptFiber(void *arg) {
    WXSocket client;
    char origin[64];
    uint32_t evt;
    int rc;

    while (TRUE) {
        evt = GMPS_YieldSocket(svcSocket, GMPS_EVT_IN);
        if (evt == 0) {
            (void) atomic_fetch_add(&svcErrors, 1);
            return;
        }

        while (TRUE) {
            rc = WXSocket_Accept(svcSocket, &client, origin, sizeof(origin));
            if (rc == WXNRC_TIMEOUT) break;
            if (rc != WXNRC_OK) {
                (void) atomic_fetch_add(&svcErrors, 1);
                break;
            }

            /* Connection good, spawn fiber to process */
            (void) WXSocket_SetNonBlockingState(client, TRUE);
            if (GMPS_Start(connHandler, (void *) (uintptr_t) client) == NULL) {
                (void) atomic_fetch_add(&svcErrors, 1);
                WXSocket_Close(client);
            }
        }
    }
}

/* Thread to make a set of connections and have a chat */
static void *clientThread(void *arg) {
    int count = (int) (intptr_t) arg;
    char portStr[16], buf[16];
    WXSocket sock;
    int idx;

    (void) sprintf(portStr, "%u", (unsigned int) svcPort);

    for (idx = 0; idx < count; idx++) {
        if (WXSocket_OpenTCPClient("127.0.0.1", portStr, &sock,
                                   NULL) != WXNRC_OK) {
            (void) atomic_fetch_add(&svcErrors, 1);
            return NULL;
        }

        if (WXSocket_Send(sock, "ping", 4, 0) != 4) {
            (void) atomic_fetch_add(&svcErrors, 1);
            WXSocket_Close(sock);
            return NULL;
        }

        if (WXSocket_Recv(sock, buf, sizeof(buf), 0) != 4) {
            (void) atomic_fetch_add(&svcErrors, 1);
            WXSocket_Close(sock);
            return NULL;
        }

        WXSocket_Close(sock);
        thump();
        (void) usleep(300);
    }

    return NULL;
}

static void runConnectionLoad(char *label, int threads, int perThread) {
    WXThread clients[8];
    int idx;

    startTest(label);

    atomic_store(&svcServed, 0);
    atomic_store(&svcErrors, 0);
    svcTarget = threads * perThread;

    GMPS_EnterSyscall();
    for (idx = 0; idx < threads; idx++) {
        if (WXThread_Create(&(clients[idx]), clientThread,
                            (void *) (intptr_t) perThread) != WXTRC_OK) {
            GMPS_ExitSyscall();
            testFail("could not create a client thread");
            return;
        }
    }
    GMPS_ExitSyscall();

    awaitCompletions(1);

    expectEq(atomic_load(&svcErrors), 0, "connection errors");
    expectEq(atomic_load(&svcServed), svcTarget, "connections served");
}

/********** Syscall parking **********/

static _Atomic(int) tickCount = 0;
static _Atomic(int) tickRunning = 1;

/* Fiber that just keeps ticking with a very large stream of yields */
static void tickFiber(void *arg) {
    int guard = 0;

    while ((atomic_load(&tickRunning)) && ((++guard) < 100000000)) {
        (void) atomic_fetch_add(&tickCount, 1);
        if ((guard % 256) == 0) thump();
        GMPS_Yield();
    }
    signalDone();
}

/* Fiber that does a single syscall/sleep to test scheduler handoff */
static void blockingFiber(void *arg) {
    int before;

    before = atomic_load(&tickCount);

    /* Syscall will detach fibers/thread to allow tick to keep running */
    /* Yah I could use sleepMillis() here but be very clear */
    GMPS_EnterSyscall();
    (void) usleep(200000);
    GMPS_ExitSyscall();

    if (atomic_load(&tickCount) == before) {
        testFail("ticks blocked during blocking syscall");
    }

    atomic_store(&tickRunning, 0);
    signalDone();
}

static void testSyscallHandoff() {
    int idx, tickers = 3;

    startTest("blocking syscall handoff");

    atomic_store(&tickCount, 0);
    atomic_store(&tickRunning, 1);

    /* Spawn a bunch of busy ticker fibers */
    for (idx = 0; idx < tickers; idx++) {
        (void) GMPS_Start(tickFiber, NULL);
    }

    /* And then one to block in a syscall */
    (void) GMPS_Start(blockingFiber, NULL);

    awaitCompletions(tickers + 1);
}

/********** Read/write combined **********/

static _Atomic(uint32_t) combMask = 0;

static void combFiber(void *arg) {
    atomic_store(&combMask, GMPS_YieldSocket(testPair[0],
                                             GMPS_EVT_IN | GMPS_EVT_OUT));
    signalDone();
}

static void testCombinedWait() {
    int idx;

    startTest("combined read/write wait return");

    if (!openTestPair()) return;
    atomic_store(&combMask, 0);

    /* Initially writable but not readable */
    (void) GMPS_Start(combFiber, NULL);
    awaitCompletions(1);

    if ((atomic_load(&combMask) & GMPS_EVT_OUT) == 0) {
        testFail("combined wait not writable (missing OUT)");
    }
    if ((atomic_load(&combMask) & GMPS_EVT_IN) != 0) {
        testFail("combined wait has IN with no data");
    }

    /* Fill the buffer to ensure read and deny writability for combined */
    if (!fillSendBuffer(testPair[0])) {
        testFail("could not fill the send buffer");
        closeTestPair();
        return;
    }

    /* Use the standard reader to create a conflicting reader-only-waiter */
    atomic_store(&duoReadMask, 0);
    (void) GMPS_Start(duoReader, NULL);
    for (idx = 0; idx < 200; idx++) GMPS_Yield();

    /* Fire a second combined, should be rejected due to existing read-waiter */
    atomic_store(&combMask, 0);
    (void) GMPS_Start(combFiber, NULL);
    awaitCompletions(1);
    expectEq((long) atomic_load(&combMask), GMPS_EVT_BUSY,
             "combined wait refused with existing reader");

    /* Send a message to awaken the read-waiter */
    if (WXSocket_Send(testPair[1], "r", 1, 0) != 1) {
        testFail("reader release write failed");
    }
    awaitCompletions(1);

    closeTestPair();
}

static _Atomic(uint32_t) staleFirst = 0;
static _Atomic(uint32_t) staleSecond = 0;
static _Atomic(int) staleState = 0;
static _Atomic(int) staleErrors = 0;

static void staleFiber(void *arg) {
    uint8_t byte = 0;

    /* Pre-register the socket without a waiter */
    if (!GMPS_SocketRegister(testPair[0])) {
        (void) atomic_fetch_add(&staleErrors, 1);
        atomic_store(&staleState, 2);
        signalDone();
        return;
    }

    /* Wait for read/write edges, recorded in r/w and c event trackers */
    sleepMillis(300);

    /* Handle the first wait (both) and consume the readability */
    atomic_store(&staleFirst, GMPS_YieldSocket(testPair[0],
                                               GMPS_EVT_IN | GMPS_EVT_OUT));
    if (WXSocket_Recv(testPair[0], &byte, 1, 0) != 1) {
        (void) atomic_fetch_add(&staleErrors, 1);
    }

    /* Fill the socket buffer to take away the writability as well */
    if (!fillSendBuffer(testPair[0])) {
        (void) atomic_fetch_add(&staleErrors, 1);
    }

    /* At this point, the second combined wait will stall */
    atomic_store(&staleState, 1);
    atomic_store(&staleSecond, GMPS_YieldSocket(testPair[0],
                                                GMPS_EVT_IN | GMPS_EVT_OUT));
    atomic_store(&staleState, 2);
    signalDone();
}

/* Verify that the resolution of a combined event cleans up the r/w events */
static void testNoStaleCombinedEvent() {
    int idx;

    startTest("combined wait resets read marker");

    if (!openTestPair()) return;
    atomic_store(&staleFirst, 0);
    atomic_store(&staleSecond, 0);
    atomic_store(&staleState, 0);
    atomic_store(&staleErrors, 0);

    /* Readable from this, writable already, so both markers get set */
    if (WXSocket_Send(testPair[1], "s", 1, 0) != 1) {
        testFail("priming write failed");
        closeTestPair();
        return;
    }

    /* Start the waiting fiber */
    (void) GMPS_Start(staleFiber, NULL);

    /* Wait for the fiber to reach the second wait state */
    for (idx = 0; (idx < 40) && (atomic_load(&staleState) < 1); idx++) {
        sleepMillis(50);
    }
    if (atomic_load(&staleState) < 1) {
        testFail("fiber never reached the second wait");
        closeTestPair();
        return;
    }

    /* Verify first wait got both, could be delays (not a regression) */
    if ((atomic_load(&staleFirst) & (GMPS_EVT_IN | GMPS_EVT_OUT)) !=
                                               (GMPS_EVT_IN | GMPS_EVT_OUT)) {
        (void) fprintf(stderr, "   uh-oh - didn't get both, first=%#x)\n",
                       (unsigned int) atomic_load(&staleFirst));
    } else {
        /* Wait a bit longer, make sure the second wait is still waiting */
        sleepMillis(300);
        expectEq((long) atomic_load(&staleState), 1,
                 "second combined wait woke with no new event");
    }

    /* Release the waiter with a real edge and reap it */
    if (WXSocket_Send(testPair[1], "t", 1, 0) != 1) {
        testFail("release write failed");
    }
    awaitCompletions(1);

    /* Second wait should have shown readability */
    expectEq(atomic_load(&staleErrors), 0, "fiber execution errors");
    if ((atomic_load(&staleSecond) & GMPS_EVT_IN) == 0) {
        testFail("second combined wait missing IN event");
    }

    closeTestPair();
}

static _Atomic(uint32_t) crossRead = 0;
static _Atomic(uint32_t) crossBoth = 0;
static _Atomic(int) crossState = 0;
static _Atomic(int) crossErrors = 0;

/* Handles the initial readability event, leaving in read/write wait ready */
static void crossReader(void *arg) {
    uint8_t byte = 0;

    /* Wait for the read and consume it to block readability */
    atomic_store(&crossRead, GMPS_YieldSocket(testPair[0], GMPS_EVT_IN));
    if (WXSocket_Recv(testPair[0], &byte, 1, 0) != 1) {
        (void) atomic_fetch_add(&crossErrors, 1);
    }

    /* Fill the socket buffer to cancel writability */
    if (!fillSendBuffer(testPair[0])) {
        (void) atomic_fetch_add(&crossErrors, 1);
    }

    signalDone();
}

/* Second fiber to exercise the combined yield after initial read completed */
static void crossBothFiber(void *arg) {
    atomic_store(&crossState, 1);
    atomic_store(&crossBoth, GMPS_YieldSocket(testPair[0],
                                              GMPS_EVT_IN | GMPS_EVT_OUT));
    atomic_store(&crossState, 2);
    signalDone();
}

/* Reverse of previous test, read-only event followed by combined */
static void testCrossModeStaleWake() {
    int idx;

    startTest("consumed read does not trigger later combined wait");

    if (!openTestPair()) return;
    atomic_store(&crossRead, 0);
    atomic_store(&crossBoth, 0);
    atomic_store(&crossState, 0);
    atomic_store(&crossErrors, 0);

    (void) GMPS_Start(crossReader, NULL);

    /* Give the reader its event */
    if (WXSocket_Send(testPair[1], "x", 1, 0) != 1) {
        testFail("priming write failed");
        closeTestPair();
        return;
    }
    awaitCompletions(1);

    if ((atomic_load(&crossRead) & GMPS_EVT_IN) == 0) {
        testFail("read wait missing IN event");
    }
    expectEq(atomic_load(&crossErrors), 0, "fiber execution errors");

    /* Now the combined waiter, with nothing ready in either direction */
    (void) GMPS_Start(crossBothFiber, NULL);
    for (idx = 0; (idx < 40) && (atomic_load(&crossState) < 1); idx++) {
        sleepMillis(50);
    }

    /* Wait a bit longer, make sure the combined wait is still waiting */
    sleepMillis(300);
    expectEq((long) atomic_load(&crossState), 1,
             "combined wait woke on consumed read");

    /* Release the waiter with a real edge and reap it */
    if (WXSocket_Send(testPair[1], "y", 1, 0) != 1) {
        testFail("release write failed");
    }
    awaitCompletions(1);

    if ((atomic_load(&crossBoth) & GMPS_EVT_IN) == 0) {
        testFail("combined wait missing IN event");
    }

    closeTestPair();
}

/********** Error -> readable wake **********/

static _Atomic(uint32_t) errMask = 0;
static _Atomic(int) errEntered = 0;
static _Atomic(int) errState = 0;
static _Atomic(int) errErrors = 0;

/* Fiber waits on a write that will error on close and return readable */
static void errFiber(void *arg) {
    /* Flood the buffer so we wait on write */
    if (!fillSendBuffer(testPair[0])) {
        (void) atomic_fetch_add(&errErrors, 1);
    }

    /* Signal test method and perform the errored wait */
    atomic_store(&errEntered, 1);
    atomic_store(&errMask, GMPS_YieldSocket(testPair[0], GMPS_EVT_OUT));
    atomic_store(&errState, 1);
    signalDone();
}

/* Verify that a failed descriptor returns readable for handling */
static void testErrorWakesAsReadable() {
    int idx;

    startTest("error wakes a write waiter as readable");

    if (!openTestPair()) return;
    atomic_store(&errMask, 0);
    atomic_store(&errEntered, 0);
    atomic_store(&errState, 0);
    atomic_store(&errErrors, 0);

    /* Wait for the fiber to enter the wait state */
    (void) GMPS_Start(errFiber, NULL);
    awaitParked(&errEntered);

    /* Close will generate failure on the descriptor and write waiter */
    closeTestPeer();

    /* Wait/verify that the fiber wait was awoken */
    for (idx = 0; (idx < 40) && (atomic_load(&errState) < 1); idx++) {
        sleepMillis(50);
    }
    if (atomic_load(&errState) < 1) {
        testFail("write waiter didn't wake on error");
        (void) GMPS_SocketUnregister(testPair[0]);
    }
    awaitCompletions(1);

    /* With a readability condition */
    expectEq(atomic_load(&errErrors), 0, "error setup failed");
    if ((atomic_load(&errMask) & GMPS_EVT_IN) == 0) {
        testFail("error did not return IN event for write waiter");
    }

    closeTestPair();
}

/* Similar to above, but with two waiters */
static _Atomic(uint32_t) dualReadMask = 0;
static _Atomic(uint32_t) dualWriteMask = 0;
static _Atomic(int) dualReadIn = 0;
static _Atomic(int) dualWriteIn = 0;

static void dualReadFiber(void *arg) {
    atomic_store(&dualReadIn, 1);
    atomic_store(&dualReadMask, GMPS_YieldSocket(testPair[0], GMPS_EVT_IN));
    signalDone();
}

static void dualWriteFiber(void *arg) {
    atomic_store(&dualWriteIn, 1);
    atomic_store(&dualWriteMask, GMPS_YieldSocket(testPair[0], GMPS_EVT_OUT));
    signalDone();
}

static void testDualWaiterErrorFold() {
    int idx;

    startTest("error wakes separate read and write waiters");

    if (!openTestPair()) return;
    atomic_store(&dualReadMask, 0);
    atomic_store(&dualWriteMask, 0);
    atomic_store(&dualReadIn, 0);
    atomic_store(&dualWriteIn, 0);

    /* Fill to block the write yield */
    if (!fillSendBuffer(testPair[0])) {
        testFail("could not fill the send buffer");
        closeTestPair();
        return;
    }

    /* Establish the two fibers in read and write wait states */
    (void) GMPS_Start(dualReadFiber, NULL);
    awaitParked(&dualReadIn);
    (void) GMPS_Start(dualWriteFiber, NULL);
    awaitParked(&dualWriteIn);

    /* Kaboom */
    closeTestPeer();

    /* Wait for the waiting fibers to exit */
    for (idx = 0; (idx < 40) && ((atomic_load(&dualReadMask) == 0) ||
                                 (atomic_load(&dualWriteMask) == 0)); idx++) {
        sleepMillis(50);
    }
    if ((atomic_load(&dualReadMask) == 0) ||
            (atomic_load(&dualWriteMask) == 0)) {
        testFail("one/two waiter(s) didn't wake on error");
        (void) GMPS_SocketUnregister(testPair[0]);
    }
    awaitCompletions(2);

    if ((atomic_load(&dualReadMask) & GMPS_EVT_IN) == 0) {
        testFail("error did not return IN event for read waiter");
    }
    if ((atomic_load(&dualWriteMask) & GMPS_EVT_IN) == 0) {
        testFail("error did not return IN event for write waiter");
    }

    closeTestPair();
}

static _Atomic(uint32_t) detachMask = 1;
static _Atomic(int) detachEntered = 0;

/* Simple read wait condition to go kablooie */
static void detachWaiter(void *arg) {
    atomic_store(&detachEntered, 1);
    atomic_store(&detachMask, GMPS_YieldSocket(testPair[0], GMPS_EVT_IN));
    signalDone();
}

static void testUnregisterWithWaiter() {
    startTest("unregister wakes a waiter on the descriptor");

    if (!openTestPair()) return;
    atomic_store(&detachMask, 1);
    atomic_store(&detachEntered, 0);

    /* Park in the read wait state */
    (void) GMPS_Start(detachWaiter, NULL);
    awaitParked(&detachEntered);

    /* Detach the descriptor which will trigger the wait wakeup */
    if (!GMPS_SocketUnregister(testPair[0])) {
        testFail("failed to unregister an active descriptor");
    }
    awaitCompletions(1);

    expectEq((long) atomic_load(&detachMask), 0,
             "unregister did not return 0 event for waiter wake");

    closeTestPair();
}

/********** Socket readiness (edge->level changes) **********/

static _Atomic(uint32_t) readyMask = 0;
static _Atomic(int) readyErrors = 0;

static void readyFiber(void *arg) {
    uint32_t evt;
    uint8_t byte = 0;

    /* Wait for inbound event for socket, which should already be there */
    evt = GMPS_YieldSocket(testPair[0], GMPS_EVT_IN);
    atomic_store(&readyMask, evt);
    if (evt == 0) {
        (void) atomic_fetch_add(&readyErrors, 1);
        signalDone();
        return;
    }

    /* Read the single byte message */
    if (WXSocket_Recv(testPair[0], &byte, 1, 0) != 1) {
        (void) atomic_fetch_add(&readyErrors, 1);
    } else if (byte != 'q') {
        (void) atomic_fetch_add(&readyErrors, 1);
    }

    signalDone();
}

/* This failed for edge case, ready sees present data at registration! */
static void testSocketReadyOnRegister() {
    startTest("socket ready before wait");

    if (!openTestPair()) return;
    atomic_store(&readyErrors, 0);
    atomic_store(&readyMask, 0);

    /* Write the data before starting the reader (pre-ready state) */
    if (WXSocket_Send(testPair[1], "q", 1, 0) != 1) {
        testFail("priming write failed");
        closeTestPair();
        return;
    }

    /* Exec the reader, should be immediate */
    (void) GMPS_Start(readyFiber, NULL);
    awaitCompletions(1);

    expectEq(atomic_load(&readyErrors), 0, "ready errors");
    if ((atomic_load(&readyMask) & GMPS_EVT_IN) == 0) {
        testFail("wake event mask missing IN event");
    }

    /* Detach and close */
    closeTestPair();
}

static _Atomic(int) latchPhase = 0;
static _Atomic(uint32_t) latchMask = 0;
static _Atomic(int) latchErrors = 0;

/* Loop so readability occurs while busy elsewhere, event must be stored */
static void latchFiber(void *arg) {
    uint8_t byte = 0;
    uint32_t evt;
    int idx;

    /* Set up the event tracking without actually waiting */
    if (!GMPS_SocketRegister(testPair[0])) {
        (void) atomic_fetch_add(&latchErrors, 1);
        signalDone();
        return;
    }

    atomic_store(&latchPhase, 1);

    /* Spin until writer indicates 'ready' (event should be captured) */
    for (idx = 0; idx < 10000000; idx++) {
        if (atomic_load(&latchPhase) == 2) break;
        if ((idx % 64) == 0) thump();
        GMPS_Yield();
    }

    /* Spin a little while longer */
    for (idx = 0; (idx < 2000) && (atomic_load(&latchPhase) == 2); idx++) {
        if ((idx % 64) == 0) thump();
        GMPS_Yield();
    }

    /* Failure check in case writer never happened */
    if (atomic_load(&latchPhase) != 2) {
        (void) fprintf(stderr, "   (write never happened: phase = %d)\n",
                       (int) atomic_load(&latchPhase));
        (void) atomic_fetch_add(&latchErrors, 1);
        signalDone();
        return;
    }

    /* Should have read event waiting, get it and read data */
    evt = GMPS_YieldSocket(testPair[0], GMPS_EVT_IN);
    atomic_store(&latchMask, evt);
    if (evt == 0) {
        (void) fprintf(stderr, "   (latched wait returned 0)\n");
        (void) atomic_fetch_add(&latchErrors, 1);
    } else if (WXSocket_Recv(testPair[0], &byte, 1, 0) != 1) {
        (void) atomic_fetch_add(&latchErrors, 1);
    }

    signalDone();
}

/* Fiber to do the write needed for above */
static void latchWriter(void *arg) {
    int idx;

    for (idx = 0; idx < 100000; idx++) {
        if (atomic_load(&latchPhase) == 1) break;
        GMPS_Yield();
    }

    if (WXSocket_Send(testPair[1], "z", 1, 0) != 1) {
        (void) atomic_fetch_add(&latchErrors, 1);
    }
    atomic_store(&latchPhase, 2);

    signalDone();
}

static void testReadinessEvent() {
    startTest("readiness saved while the fiber is busy");

    if (!openTestPair()) return;
    atomic_store(&latchPhase, 0);
    atomic_store(&latchMask, 0);
    atomic_store(&latchErrors, 0);

    (void) GMPS_Start(latchFiber, NULL);
    (void) GMPS_Start(latchWriter, NULL);
    awaitCompletions(2);

    expectEq(atomic_load(&latchErrors), 0, "latched readiness errors");
    if ((atomic_load(&latchMask) & GMPS_EVT_IN) == 0) {
        testFail("IN not recorded while reading fiber busy");
    }

    closeTestPair();
}

static _Atomic(uint32_t) existFirst = 0;
static _Atomic(uint32_t) existSecond = 0;
static _Atomic(int) existState = 0;

static void existFiber(void *arg) {
    /* Resolves on the write side, consuming the writable edge */
    atomic_store(&existFirst, GMPS_YieldSocket(testPair[0], GMPS_EVT_OUT));

    /* Do it again, should have the same outcome (no change but level) */
    atomic_store(&existState, 1);
    atomic_store(&existSecond, GMPS_YieldSocket(testPair[0], GMPS_EVT_OUT));
    atomic_store(&existState, 2);
    signalDone();
}

/* Test the retention of a write condition on second call, level not edge */
/* This is what libpq broke with edge-triggered - connect does two write reqs */
static void testExistingLevelRewait() {
    int idx;

    startTest("second wait on a existing level still triggers");

    if (!openTestPair()) return;
    atomic_store(&existFirst, 0);
    atomic_store(&existSecond, 0);
    atomic_store(&existState, 0);

    /* Fiber does two yields on the same (unchanged) event/condition */
    (void) GMPS_Start(existFiber, NULL);

    /* Tick-tock, it comes back or hangs forever */
    for (idx = 0; (idx < 40) && (atomic_load(&existState) < 2); idx++) {
        sleepMillis(50);
    }

    if ((atomic_load(&existFirst) & GMPS_EVT_OUT) == 0) {
        testFail("first wait did not return OUT event");
    }

    if (atomic_load(&existState) < 2) {
        testFail("second wait hung on still-writable socket");

        /* Unregister to force the exit */
        (void) GMPS_SocketUnregister(testPair[0]);
    } else if ((atomic_load(&existSecond) & GMPS_EVT_OUT) == 0) {
        testFail("second wait did not return OUT event");
    }
    awaitCompletions(1);

    closeTestPair();
}

/********** Duplicate waiter (busy) **********/

static _Atomic(uint32_t) dupFirst = 0;
static _Atomic(uint32_t) dupSecond = 1;
static _Atomic(int) dupEntered = 0;

static void dupFirstFiber(void *arg) {
    uint8_t byte = 0;

    atomic_store(&dupEntered, 1);
    atomic_store(&dupFirst, GMPS_YieldSocket(testPair[0], GMPS_EVT_IN));
    (void) WXSocket_Recv(testPair[0], &byte, 1, 0);
    signalDone();
}

static void dupSecondFiber(void *arg) {
    atomic_store(&dupSecond, GMPS_YieldSocket(testPair[0], GMPS_EVT_IN));
    signalDone();
}

static void testDuplicateWait() {
    startTest("second waiter for same event returns busy");

    if (!openTestPair()) return;
    atomic_store(&dupFirst, 0);
    atomic_store(&dupSecond, 1);
    atomic_store(&dupEntered, 0);

    (void) GMPS_Start(dupFirstFiber, NULL);
    awaitParked(&dupEntered);

    (void) GMPS_Start(dupSecondFiber, NULL);
    awaitCompletions(1);

    expectEq((long) atomic_load(&dupSecond), GMPS_EVT_BUSY,
             "second read waiter busy error");

    /* Still need to wake the first waiter */
    if (WXSocket_Send(testPair[1], "d", 1, 0) != 1) {
        testFail("wakeup write failed");
    }
    awaitCompletions(1);

    if ((atomic_load(&dupFirst) & GMPS_EVT_IN) == 0) {
        testFail("original waiter missing IN event");
    }

    closeTestPair();
}

/********** Invalid descriptor **********/

static _Atomic(uint32_t) badResult = 1;
static int badFd = -1;

static void badRegFiber(void *arg) {
    atomic_store(&badResult, GMPS_YieldSocket((WXSocket) badFd, GMPS_EVT_IN));
    signalDone();
}

static void testRegistrationFailure() {
    startTest("unpollable descriptor refused cleanly");

    /* This does not look like a socket to me... */
    badFd = open("/etc/hostname", O_RDONLY);
    if (badFd < 0) {
        testFail("could not open a regular file");
        return;
    }
    atomic_store(&badResult, 1);
    (void) GMPS_Start(badRegFiber, NULL);
    awaitCompletions(1);

    expectEq((long) atomic_load(&badResult), 0, "wait on regular file");

    (void) close(badFd);
    badFd = -1;
}

/********** Rapid churn/descriptor reuse **********/

static _Atomic(int) churnErrors = 0;

/* Cycle a lot of parallel fibers to churn through descriptors (reuse) */
static void churnFiber(void *arg) {
    WXSocket pair[2];
    uint8_t byte = 0;
    uint32_t evt;

    if (!makePair(pair)) {
        (void) atomic_fetch_add(&churnErrors, 1);
        signalDone();
        return;
    }

    if (WXSocket_Send(pair[1], "c", 1, 0) != 1) {
        (void) atomic_fetch_add(&churnErrors, 1);
    } else {
        evt = GMPS_YieldSocket(pair[0], GMPS_EVT_IN);
        if (evt == 0) {
            (void) atomic_fetch_add(&churnErrors, 1);
        } else if (WXSocket_Recv(pair[0], &byte, 1, 0) != 1) {
            (void) atomic_fetch_add(&churnErrors, 1);
        }
    }

    (void) GMPS_SocketUnregister(pair[0]);
    WXSocket_Close(pair[0]);
    WXSocket_Close(pair[1]);

    signalDone();
}

static void testDescriptorChurn() {
    int idx, rounds = 2000;

    startTest("descriptor close and reuse churn");

    atomic_store(&churnErrors, 0);

    for (idx = 0; idx < rounds; idx++) {
        (void) GMPS_Start(churnFiber, NULL);
        awaitCompletions(1);
    }

    expectEq(atomic_load(&churnErrors), 0, "churn wait/read errors");
}

/* Put the test sequence into a parallel fiber to exercise scheduler more */
static void driverFiber(void *arg) {
    doneChannel = GMPS_ChannelCreate(512);
    if (doneChannel == NULL) {
        (void) fprintf(stderr, "Error: could not create completion channel\n");
        exit(1);
    }

    /* The accept loop lives for the rest of the run */
    (void) GMPS_Start(connAcceptFiber, NULL);

    testFiberBasics();
    testYieldProgress();
    testFiberLocalStorage();
    testChannels(0, "unbuffered channel");
    testChannels(8, "buffered channel");
    testReaderAndWriter();
    testIdleWakeDelivery();
    runConnectionLoad("accept/request sequential connections", 1, connCount);
    runConnectionLoad("accept/request concurrent connections",
                      8, connCount / 8);
    testSyscallHandoff();
    testCombinedWait();
    testNoStaleCombinedEvent();
    testCrossModeStaleWake();
    testErrorWakesAsReadable();
    testDualWaiterErrorFold();
    testUnregisterWithWaiter();
    testSocketReadyOnRegister();
    testReadinessEvent();
    testExistingLevelRewait();
    testDuplicateWait();
    testRegistrationFailure();
    testDescriptorChurn();

    (void) fprintf(stderr, "\n%d scenarios, %d failure(s)\n",
                   testCount, failCount);
    exit((failCount != 0) ? 1 : 0);
}

/**
 * Where all of the fun begins!
 */
int main(int argc, char **argv) {
    WXThread npThread, wdThread;
    int idx;

    for (idx = 1; idx < argc; idx++) {
        if (strcmp(argv[idx], "-n") == 0) {
            if (idx >= (argc - 1)) {
                (void) fprintf(stderr, "Error: missing -n <count> argument\n");
                exit(1);
            }
            stressCount = atoi(argv[++idx]);
        } else if (strcmp(argv[idx], "-c") == 0) {
            if (idx >= (argc - 1)) {
                (void) fprintf(stderr, "Error: missing -c <count> argument\n");
                exit(1);
            }
            connCount = atoi(argv[++idx]);
        } else if (strcmp(argv[idx], "-w") == 0) {
            if (idx >= (argc - 1)) {
                (void) fprintf(stderr, "Error: missing -w <secs> argument\n");
                exit(1);
            }
            watchdogLimit = atoi(argv[++idx]);
        } else if (strcmp(argv[idx], "-p") == 0) {
            if (idx >= (argc - 1)) {
                (void) fprintf(stderr, "Error: missing -p <count> argument\n");
                exit(1);
            }
            procCount = atoi(argv[++idx]);
        } else {
            (void) fprintf(stderr, "Error: Invalid argument: %s\n", argv[idx]);
            (void) fprintf(stderr, "Usage: schedsuite [-n <pokes>] "
                                   "[-c <connections>] [-w <stall secs>] "
                                   "[-p <processors>]\n");
            exit(1);
        }
    }

    (void) fprintf(stderr, "Scheduler suite: %d poke cycles, %d connections, "
                           "%d processors, %ds stall limit\n",
                   stressCount, connCount, procCount, watchdogLimit);

    /* Watchdog first, so even a botched startup gets reported */
    if (WXThread_Create(&wdThread, watchdogThread, NULL) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to create watchdog thread\n");
        exit(1);
    }

    if (GMPS_OnFiber()) {
        (void) fprintf(stderr, "Error: GMPS_OnFiber true on the main thread\n");
        exit(1);
    }

    /* Listener has to exist before the accept fiber starts */
    if (WXSocket_OpenEphemeralServer("127.0.0.1", &svcPort,
                                     &svcSocket) != WXNRC_OK) {
        (void) fprintf(stderr, "Failed to open the test listener: %s\n",
                       WXSocket_GetErrorStr(WXSocket_GetLastErrNo()));
        exit(1);
    }
    if (WXSocket_SetNonBlockingState(svcSocket, TRUE) != WXNRC_OK) {
        (void) fprintf(stderr, "Failed to unblock the test listener\n");
        exit(1);
    }

    if (!GMPS_SchedulerInit(procCount)) {
        (void) fprintf(stderr, "Scheduler initialization failed\n");
        exit(1);
    }

    flsKeyA = GMPS_FlsKeyCreate(flsDestructor);
    flsKeyB = GMPS_FlsKeyCreate(NULL);
    if ((flsKeyA == GMPS_FLS_INVALID_KEY) ||
            (flsKeyB == GMPS_FLS_INVALID_KEY)) {
        (void) fprintf(stderr, "FLS key creation failed\n");
        exit(1);
    }

    if (GMPS_Start(driverFiber, NULL) == NULL) {
        (void) fprintf(stderr, "Failed to start the driver fiber\n");
        exit(1);
    }

    /* Without this nothing delivers events once all threads are parked */
    if (WXThread_Create(&npThread, netPollThread, NULL) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to create netpoll thread\n");
        exit(1);
    }

    /* Does not return, the driver fiber exits the process */
    GMPS_SchedulerStart();

    return 1;
}
