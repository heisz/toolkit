/*
 * Test interface for the database facade wrapper.
 *
 * Copyright (C) 1997-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "dbxf.h"
#include "mem.h"

/**
 * Main testing entry point.  Just a bunch of test instances.
 */
int main(int argc, char **argv) {
    char *user = NULL, *password = NULL, *dsn = NULL;
    WXDBConnection *conna, *connb;
    WXDBConnectionPool *pool;
    int idx, rc;

    /* Handle optional pool arguments */
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

    /* Initialize the connection pool instance */
    pool = WXMalloc(sizeof(WXDBConnectionPool));
    if (WXDBConnectionPool_Init(pool, dsn, user, password, 1) < 0) {
        (void) fprintf(stderr, "Failed to initialize the test pool: %s\n",
                       WXDB_GetLastErrorMessage(pool));
        exit(1);
    }

    /* Grab a couple of connections */
    conna = WXDBConnectionPool_Obtain(pool);
    connb = WXDBConnectionPool_Obtain(pool);

    /* A bit of transactional silliness (with error handling) */
    if (WXDBConnection_TxnBegin(conna) != WXDRC_OK) {
        (void) fprintf(stderr, "Unexpected begin error: %s\n",
                       WXDB_GetLastErrorMessage(conna));
        exit(1);
    }
    if (WXDBConnection_TxnSavepoint(conna, "test") != WXDRC_OK) {
        (void) fprintf(stderr, "Unexpected savepoint error: %s\n",
                       WXDB_GetLastErrorMessage(conna));
        exit(1);
    }
    if (WXDBConnection_TxnRollback(conna, "test") != WXDRC_OK) {
        (void) fprintf(stderr, "Unexpected rollback error: %s\n",
                       WXDB_GetLastErrorMessage(conna));
        exit(1);
    }
    if (WXDBConnection_TxnCommit(conna) != WXDRC_OK) {
        (void) fprintf(stderr, "Unexpected commit error: %s\n",
                       WXDB_GetLastErrorMessage(conna));
        exit(1);
    }
    if (WXDBConnection_TxnRollback(conna, "test") != WXDRC_OK) {
        (void) fprintf(stderr, "Expected rollback error: %s\n",
                       WXDB_GetLastErrorMessage(conna));
    }

    if (WXDBConnection_RowsModified(connb) > 0) {
        (void) fprintf(stderr, "Rows modified for non-update scenario\n");
        exit(1);
    }
    if (WXDBConnection_LastRowId(connb) != 0) {
        (void) fprintf(stderr, "Row id for non-insert scenario\n");
        exit(1);
    }

    /* Put your toys back when you are finished */
    WXDBConnectionPool_Return(conna);
    WXDBConnectionPool_Return(connb);

    /* And clean up */
    WXDBConnectionPool_Destroy(pool);
    WXFree(pool);
}
