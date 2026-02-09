/**
 * Test/debug wrapper for the scheduling toolkit.
 * 
 * Copyright (C) 2025-2026 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "stdconfig.h"
#include "scheduler.h"
#include "schedulerint.h"
#include "channel.h"
#include "socket.h"
#include "thread.h"
#include "mem.h"

static void basicWork(void *arg) {
    int id = (int)(intptr_t)arg;

    while(1) {
        fprintf(stderr, "Tick %d\n", id);
        usleep(1000000);
        GMPS_Yield();
    }
}

static void syscallWork(void *arg) {
    int id = (int)(intptr_t)arg;

    while(1) {
        fprintf(stderr, "[Syscall %d] About to sleep (blocking)\n", id);

        /* Release processor before blocking syscall */
        GMPS_EnterSyscall();
        sleep(2);
        GMPS_ExitSyscall();

        fprintf(stderr, "[Syscall %d] Woke from sleep\n", id);
        GMPS_Yield();
    }
}

static void fcgi_accept_loop(void *arg) {
    WXSocket serverSocket = (WXSocket)(uintptr_t) arg;
    WXSocket clientSocket;
    char origin[64];
    int rc;
    uint32_t ready_events;

    printf("[Network] Accept loop started\n");

    while (1) {
        /* Wait for incoming connection */
        ready_events = GMPS_YieldSocket(serverSocket, GMPS_EVT_IN);
        if (ready_events == 0) {
            printf("[Network] Error waiting for connections\n");
            break;
        }

        /* Accept all pending connections */
        while (1) {
            rc = WXSocket_Accept(serverSocket, &clientSocket,
                                 origin, sizeof(origin));
            if (rc == WXNRC_TIMEOUT) {
                /* No more pending connections */
                break;
            }
            if (rc != WXNRC_OK) {
                printf("[Network] Accept error: %d\n", rc);
                break;
            }

            printf("[Network] Accepted connection from %s (socket %u)\n",
                   origin, clientSocket);

            /* Set client socket to non-blocking */
            WXSocket_SetNonBlockingState(clientSocket, 1);

            /* Start a fiber to process this connection */
            /* BLAH BLAH BLAH */
        }
    }

    printf("[Network] Accept loop finished\n");
}

static void *netpollThread(void *arg) {
    int cnt;

    (void) arg;
    fprintf(stderr, "[NetPoll] External poll thread started\n");

    while (1) {
        cnt = GMPS_NetPoll(500);
        if (cnt > 0) {
            fprintf(stderr, "[NetPoll] Woke %d fiber(s)\n", cnt);
        }
    }

    return NULL;
}

#define CHAN_TEST_COUNT 10

static void chanProducer(void *arg) {
    struct GMPS_Channel *ch = (struct GMPS_Channel *) arg;
    int idx;

    for (idx = 0; idx < CHAN_TEST_COUNT; idx++) {
        void *val = (void *)(intptr_t)(idx + 1);
        int rc = GMPS_ChannelSend(ch, val);
        (void) fprintf(stderr,
            "[Chan] Sent %d, rc=%d\n", idx + 1, rc);
    }
    (void) fprintf(stderr, "[Chan] Producer done\n");
}

static void chanConsumer(void *arg) {
    struct GMPS_Channel *ch = (struct GMPS_Channel *) arg;
    void *val;
    int idx;

    for (idx = 0; idx < CHAN_TEST_COUNT; idx++) {
        int rc = GMPS_ChannelRecv(ch, &val);
        (void) fprintf(stderr,
            "[Chan] Recv %d, rc=%d\n",
            (int)(intptr_t) val, rc);
    }
    (void) fprintf(stderr, "[Chan] Consumer done\n");
}

static void chanBufProducer(void *arg) {
    struct GMPS_Channel *ch = (struct GMPS_Channel *) arg;
    int idx;

    for (idx = 0; idx < CHAN_TEST_COUNT; idx++) {
        void *val = (void *)(intptr_t)(idx + 100);
        int rc = GMPS_ChannelSend(ch, val);
        (void) fprintf(stderr,
            "[BufChan] Sent %d, rc=%d\n",
            idx + 100, rc);
    }
    GMPS_ChannelClose(ch);
    (void) fprintf(stderr,
        "[BufChan] Producer done, channel closed\n");
}

static void chanBufConsumer(void *arg) {
    struct GMPS_Channel *ch = (struct GMPS_Channel *) arg;
    void *val;
    int rc;

    /* Drain until closed */
    while (TRUE) {
        rc = GMPS_ChannelRecv(ch, &val);
        if (!rc) {
            (void) fprintf(stderr,
                "[BufChan] Recv FALSE (closed)\n");
            break;
        }
        (void) fprintf(stderr,
            "[BufChan] Recv %d, rc=%d\n",
            (int)(intptr_t) val, rc);
    }
    (void) fprintf(stderr,
        "[BufChan] Consumer done\n");
}

/**
 * Where all of the fun begins!
 */
int main(int argc, char **argv) {
    char *svc = "5555";
    WXSocket svcConnectHandle;
    WXThread delayThread;
    int idx;

   /* Parse the command line arguments (most options come from config file) */
   for (idx = 1; idx < argc; idx++) {
        if (strcmp(argv[idx], "-s") == 0) {
            if (idx >= (argc - 1)) {
                (void) fprintf(stderr, "Error: missing -s <svc> argument\n");
                exit(1);
            }
            svc = argv[++idx];
        } else {
            (void) fprintf(stderr, "Error: Invalid argument: %s\n", argv[idx]);
            exit(1);
        }
    }

    /* Mark the process start in the log */
    (void) fprintf(stderr, "Scheduler test program starting\n");

    /* Open the bind socket, must be exclusive access */
    if (WXSocket_OpenTCPServer("0.0.0.0", svc, &svcConnectHandle) != WXNRC_OK) {
        (void) fprintf(stderr, "Failed to open primary bind socket: %s\n",
                       WXSocket_GetErrorStr(WXSocket_GetLastErrNo()));
        exit(1);
    }
    (void) fprintf(stdout, "Listening on 0.0.0.0:%s\n", svc);

    /* Force connect socket to non-blocking to cleanly handle multi-connect */
    if (WXSocket_SetNonBlockingState(svcConnectHandle, TRUE) != WXNRC_OK) {
        (void) fprintf(stderr, "Unable to unblock primary bind socket: %s\n",
                       WXSocket_GetErrorStr(WXSocket_GetLastErrNo()));
        exit(1);
    }

    /* Initialize the global scheduler instance */
    if (!GMPS_SchedulerInit(4)) {
        (void) fprintf(stderr, "Scheduler initialization failed\n");
        exit(1);
    }

    GMPS_Fiber *f1 = GMPS_Start(basicWork, (void*) 1);
    GMPS_Fiber *f2 = GMPS_Start(basicWork, (void*) 2);
    GMPS_Fiber *f3 = GMPS_Start(syscallWork, (void*) 3);

    GMPS_Fiber *acc = GMPS_Start(fcgi_accept_loop,
                          (void *)(uintptr_t) svcConnectHandle);

    /* Unbuffered channel: producer/consumer must rendezvous */
    struct GMPS_Channel *unbufCh = GMPS_ChannelCreate(0);
    (void) GMPS_Start(chanProducer, unbufCh);
    (void) GMPS_Start(chanConsumer, unbufCh);

    /* Buffered channel with close-drain test */
    struct GMPS_Channel *bufCh = GMPS_ChannelCreate(4);
    (void) GMPS_Start(chanBufProducer, bufCh);
    (void) GMPS_Start(chanBufConsumer, bufCh);

    /* External thread to periodically poll for network events */
    WXThread npThread;
    if (WXThread_Create(&npThread, netpollThread, NULL) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to create netpoll thread\n");
        exit(1);
    }

    GMPS_SchedulerStart();
}
