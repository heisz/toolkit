/*
 * Test interface for the networking elements.
 *
 * Copyright (C) 1997-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "stdconfig.h"
#include "stream.h"
#include "socket.h"
#include "mem.h"

#include <sys/types.h>
#ifndef WIN32
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#else
#include <mstcpip.h>
#endif
#include <openssl/ssl.h>
#include <openssl/bio.h>

/* For the test program the SSL details can be static */
SSL_CTX *sslCtx;
SSL *sslHandle;

/* Raw encoding handlers for the streams */
/* Note that in this case the read/write flows are very linear */
static int sslReadEncoder(WXSocketStream *strm) {
    WXBuffer *rd = &(strm->readBuffer);
    int l, sslError;

    /* Very similar to the one in stream, except for the reader */
    l = SSL_read(sslHandle, rd->buffer + rd->length,
                 rd->allocLength - rd->length);
    if (l > 0) {
        rd->length += l;
        return WXNRC_OK_WITH_DATA;
    }

    /* Fallthrough is error conditons */
    sslError = SSL_get_error(sslHandle, l);
    if (sslError == SSL_ERROR_WANT_READ) {
        return WXNRC_READ_REQUIRED;
    } else if (sslError == SSL_ERROR_WANT_WRITE) {
        return WXNRC_WRITE_REQUIRED;
    }

    /* Perhaps translate sslError or even system errnum for details */
    return WXNRC_SYS_ERROR;
}

static int sslWriteEncoder(WXSocketStream *strm) {
    WXBuffer *wr = &(strm->writeBuffer);
    int l, sslError;

    l = SSL_write(sslHandle, wr->buffer + wr->offset,
                  wr->length - wr->offset);
    if (l > 0) {
        wr->offset += l;
        return WXNRC_OK;
    }

    /* Fallthrough is error conditons */
    sslError = SSL_get_error(sslHandle, l);
    if (sslError == SSL_ERROR_WANT_READ) {
        return WXNRC_READ_REQUIRED;
    } else if (sslError == SSL_ERROR_WANT_WRITE) {
        return WXNRC_WRITE_REQUIRED;
    }

    /* Perhaps translate sslError or even system errnum for details */
    return WXNRC_SYS_ERROR;
}

/**
 * Main testing entry point.  Just a bunch of manipulations of networking
 * bits, at Google's expense...
 */
int main(int argc, char **argv) {
    struct addrinfo hints, *addrInfo = NULL;
    char *svc, *req, *ptr;
    WXSocketStream strm;
    int rc, idx, idy;
    WXSocket hndl;

    /* At some point, put the MTraq testcase identifiers in here */

    /* Setup for the SSL support */
    sslCtx = SSL_CTX_new(TLS_method());
    if (sslCtx == NULL) {
        (void) fprintf(stderr, "Unable to initialize SSL context instance\n");
        exit(1);
    }

    /* Play with the validator */
    if (WXSocket_ValidateHostIpAddr("not.a.host.eh") != WXNRC_DATA_ERROR) {
        (void) fprintf(stderr, "Unexpected result for bad host validate\n");
        exit(1);
    }
    if (WXSocket_ValidateHostIpAddr("www.google.ca") != WXNRC_OK) {
        (void) fprintf(stderr, "Unexpected result for google validate\n");
        exit(1);
    }

    /* Stream some hand-coded HTTP requests to exercise the socket/streams */
    for (idx = 0; idx < 2; idx++) {
        /* Outer loop, HTTP versus HTTPS */
        svc = (idx == 0) ? "80" : "443";

        for (idy = 0; idy < 2; idy++) {
            /* Inner loop, repeat for synchronous and asynchronous */
            (void) fprintf(stdout, "Issuing %s %s request\n",
                           ((idy == 0) ? "synchronous" : "asynchronous"),
                           ((idx == 0) ? "HTTP" : "HTTPS"));
            if (idy == 0) {
                if (WXSocket_OpenTCPClient("www.google.ca", svc, &hndl,
                                           NULL) != WXNRC_OK) {
                    (void) fprintf(stderr, "Failed to open connection\n");
                    exit(1);
                }
            } else {
                /* Stolen from the socket code, maybe make global? */
                (void) memset(&hints, 0, sizeof(struct addrinfo));
                /* Only checking hostname, TCP/UDP targets are irrelevant */
                hints.ai_family = AF_UNSPEC;
                hints.ai_flags = AI_ADDRCONFIG;
                if (getaddrinfo("www.google.ca", svc, &hints, &addrInfo) != 0) {
                    (void) fprintf(stderr, "Failed to resolve google host\n");
                    exit(1);
                }

                /* Async open, wait for writability */
                rc = WXSocket_OpenTCPClientByAddrAsync(addrInfo, &hndl);
                if (rc != WXNRC_OK) {
                    (void) fprintf(stderr, "Failed to create connection\n");
                    exit(1);
                }
                rc = WXSocket_Wait(hndl, WXNRC_WRITE_REQUIRED, NULL);
                if ((rc & WXNRC_WRITE_REQUIRED) == 0) {
                    (void) fprintf(stderr, "Failed to establish connection\n");
                    exit(1);
                }
            }
            (void) fprintf(stderr, "Connection established\n");

            /* Setup SSL */
            if (idx != 0) {
                sslHandle = SSL_new(sslCtx);
                if (sslHandle == NULL) {
                    (void) fprintf(stderr, "Unable to allocate SSL handle\n");
                    exit(1);
                }
                SSL_set_fd(sslHandle, hndl);
                SSL_set_connect_state(sslHandle);
                SSL_set_tlsext_host_name(sslHandle, "www.google.ca");
            }

            /* Create the streaming instance */
            if (WXSockStrm_Init(&strm, hndl, -1) != WXNRC_OK) {
                (void) fprintf(stderr, "Failed to initialize stream\n");
                exit(1);
            }
            if (idx != 0) {
                strm.rawReader = sslReadEncoder;
                strm.rawWriter = sslWriteEncoder;
            }

            (void) fprintf(stderr, "SSL/stream initialized\n");

            /* Stage a big GET request */
            req = "GET /\n"
                  "\n";
            if (WXBuffer_Append(&(strm.writeBuffer), req,
                                strlen(req) - 1, TRUE) == NULL) {
                (void) fprintf(stderr, "Failed to stage the outgoing request\n");
                exit(1);
            }

            /* Flush it */
            if (WXSockStream_Flush(&strm) != WXNRC_OK) {
                (void) fprintf(stderr, "Failed to send request\n");
                exit(1);
            }
            (void) fprintf(stderr, "Write issued\n");

            /* Read something */
            while (strm.readBuffer.length == 0) {
                rc = WXSockStream_Read(&strm, 0);
                if (rc < 0) {
                    (void) fprintf(stderr, "Failed on read response %d\n", rc);
                    exit(1);
                }
                if (rc != WXNRC_OK_WITH_DATA) {
                    rc = WXSocket_Wait(strm.socketHandle, rc, NULL);
                    if (rc < 0) {
                        (void) fprintf(stderr, "Failed on read wait\n");
                        exit(1);
                    }
                }
            }

            (void) fprintf(stdout, "Read %d bytes of response\n",
                           (int) strm.readBuffer.length);
            ptr = strchr(strm.readBuffer.buffer, '\n');
            if (ptr != NULL) {
                (void) fprintf(stdout, "-> %.*s\n",
                               (int) (ptr - (char *) strm.readBuffer.buffer) + 1,
                               strm.readBuffer.buffer);
            }

            /* Cleanup */
            WXSockStrm_Destroy(&strm);
        }
    }

    (void) fprintf(stdout, "All tests passed\n");
    exit(0);
}
