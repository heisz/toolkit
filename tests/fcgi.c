/**
 * Test/debug wrapper for the FCGI protocol implementation.
 * 
 * Copyright (C) 2007-2023 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "stdconfig.h"
#include "fcgi.h"
#include "event.h"
#include "mem.h"
#include <fcntl.h>

static WXEvent_Registry *evtRegistry;

/* Some standard responses */
static char *errorResponse =
    "Status: 404\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: 36\r\n"
    "\r\n"
    "<html><body>Not Found</body></html>\n";

/**
 * Where all of the fun begins!
 */
int main(int argc, char **argv) {
    char *svc = "5555";
    int rc, idx, doErrors = FALSE;
    WXSocket svcConnectHandle;
    WXEvent *event, *eventBuffer;
    WXFCGI_Connection *conn;
    WXEvent_UserData data;
    WXSocket acceptHandle;
    char acceptAddr[256];
    ssize_t evtCnt;

   /* Parse the command line arguments (most options come from config file) */
   for (idx = 1; idx < argc; idx++) {
        if (strcmp(argv[idx], "-s") == 0) {
            if (idx >= (argc - 1)) {
                (void) fprintf(stderr, "Error: missing -s <svc> argument\n");
                exit(1);
            }
            svc = argv[++idx];
        } else if (strcmp(argv[idx], "-e") == 0) {
            doErrors = TRUE;
        } else {
            (void) fprintf(stderr, "Error: Invalid argument: %s\n", argv[idx]);
            exit(1);
        }
    }

    /* Mark the process start in the log */
    (void) fprintf(stderr, "FCGI test program starting\n");

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

    /* Setup event handling for incoming requests */
    if (WXEvent_CreateRegistry(1024, &evtRegistry) != WXNRC_OK) {
        (void) fprintf(stderr, "Failed to create primary event registry: %s\n",
                       WXSocket_GetErrorStr(WXSocket_GetLastErrNo()));
        exit(1);
    }
    data.ptr = NULL;
    if (WXEvent_RegisterEvent(evtRegistry, svcConnectHandle,
                              WXEVENT_IN, data) != WXNRC_OK) {
        (void) fprintf(stderr, "Failed to register bind socket: %s\n",
                       WXSocket_GetErrorStr(WXSocket_GetLastErrNo()));
        exit(1);
    }

    /* Allocate the event processing buffer */
    eventBuffer = (WXEvent *) WXMalloc(1024 * sizeof(WXEvent));
    if (eventBuffer == NULL)
    {
        (void) fprintf(stderr, "Failed to allocate event processing buffer");
        exit(1);
    }

    /* And do it... */
    while (TRUE) {
        evtCnt = WXEvent_Wait(evtRegistry, eventBuffer, 1024, NULL);
        if (evtCnt < 0) {
            (void) fprintf(stderr, "Error in wait event (rc %d): %s\n",
                           (int) evtCnt,
                           WXSocket_GetErrorStr(WXSocket_GetLastErrNo()));
            usleep(100000);
            continue;
        }

        for (idx = 0, event = eventBuffer; idx < evtCnt; idx++, event++) {
            /* Handle incoming connection establish actions */
            /* Note that this loops until accept times out, for multiples */
            while (event->socketHandle == svcConnectHandle) {
                rc = WXSocket_Accept(svcConnectHandle, &acceptHandle,
                                     acceptAddr, sizeof(acceptAddr));
                if (rc == WXNRC_TIMEOUT) break;
                if (rc != WXNRC_OK) {
                    (void) fprintf(stderr, "Error on client accept: %s\n",
                                 WXSocket_GetErrorStr(WXSocket_GetLastErrNo()));
                } else {
                    (void) fprintf(stderr, "Incoming client connect from %s\n",
                                   acceptAddr);

                    /* Make a tracking/receipt instance */
                    conn = WXFCGI_Allocate(acceptHandle);
                    if (conn == NULL) abort();
                    data.ptr = conn;
                    if (WXEvent_RegisterEvent(evtRegistry, acceptHandle,
                                              WXEVENT_IN, data) != WXNRC_OK) {
                        (void) fprintf(stderr, "Failed to register accept");
                        WXSocket_Close(acceptHandle);
                        WXFCGI_Release(conn);
                    }
                }
            }
            if (event->socketHandle == svcConnectHandle) continue;

            /* Handle event outcomes */
            if ((event->events & WXEVENT_OUT) != 0) {
                conn = (WXFCGI_Connection *) event->userData.ptr;
                rc = WXFCGI_Write(conn);
                if (rc < 0) {
                    if (rc != WXNRC_COMPLETE_CLOSE) {
                        (void) fprintf(stderr, "Write error for response: %s",
                                WXSocket_GetErrorStr(WXSocket_GetLastErrNo()));
                    }
                    (void) WXEvent_UnregisterEvent(evtRegistry,
                                                   conn->sockConn);
                    WXFCGI_Release(conn);
                    continue;
                }
                if (rc != WXNRC_WRITE_REQUIRED) {
                    (void) WXEvent_UpdateEvent(evtRegistry, conn->sockConn,
                                               WXEVENT_IN);
                }
                continue;
            }

            if ((event->events & WXEVENT_IN) != 0) {
                conn = (WXFCGI_Connection *) event->userData.ptr;
                rc = WXFCGI_Read(conn);
                if (rc < 0) {
fprintf(stderr, "WTF %d\n", rc);
                    (void) fprintf(stderr, "Read error for response: %s\n",
                                WXSocket_GetErrorStr(WXSocket_GetLastErrNo()));
                    (void) WXEvent_UnregisterEvent(evtRegistry,
                                                   conn->sockConn);
                    WXFCGI_Release(conn);
                    continue;
                }
                if (rc == WXNRC_OK_WITH_DATA) {
                    (void) fprintf(stderr, "Incoming request: %d\n",
                                   conn->request.requestId);
                    WXFCGI_NameValuePair *param = conn->request.params;
                    while (param != NULL) {
                        (void) fprintf(stderr, "Param: '%s' -> '%s'\n",
                                       param->name, param->value);
                        param = param->next;
                    }
                    (void) fprintf(stderr, "Content: %d bytes\n",
                                   conn->request.stdinLen);

                    /* Generate some responses */
                    if (doErrors) {
                        rc = WXFCGI_WriteResponse(conn, conn->request.requestId,
                                                  FALSE, "Error: kaboom", 13);
                        if (rc < 0) {
                            (void) fprintf(stderr,
                                "Write error for response: %s\n",
                                WXSocket_GetErrorStr(WXSocket_GetLastErrNo()));
                            (void) WXEvent_UnregisterEvent(evtRegistry,
                                                           conn->sockConn);
                            WXFCGI_Release(conn);
                            continue;
                        }

                        rc = WXFCGI_WriteResponse(conn, conn->request.requestId,
                                                  TRUE, errorResponse,
                                                  strlen(errorResponse));
                        if (rc < 0) {
                            (void) fprintf(stderr,
                                "Write error for response: %s\n",
                                WXSocket_GetErrorStr(WXSocket_GetLastErrNo()));
                            (void) WXEvent_UnregisterEvent(evtRegistry,
                                                           conn->sockConn);
                            WXFCGI_Release(conn);
                            continue;
                        }

                        rc = WXFCGI_WriteEndRequest(conn,
                                                  conn->request.requestId,
                                                  404, WXFCGI_REQUEST_COMPLETE);
                        if (rc < 0) {
                            if (rc != WXNRC_COMPLETE_CLOSE) {
                                (void) fprintf(stderr,
                                "Write error for response: %s\n",
                                 WXSocket_GetErrorStr(WXSocket_GetLastErrNo()));
                            }
                            (void) WXEvent_UnregisterEvent(evtRegistry,
                                                           conn->sockConn);
                            WXFCGI_Release(conn);
                            continue;
                        }
                        if (rc == WXNRC_WRITE_REQUIRED) {
                            (void) WXEvent_UpdateEvent(evtRegistry,
                                                    conn->sockConn,
                                                    WXEVENT_IN | WXEVENT_OUT);
                        }
                    }
                }
                continue;
            }
        }
    }
}
