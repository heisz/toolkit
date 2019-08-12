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
    WXDBConnectionPool *pool;
    int idx, rc;
    char *user = NULL, *password = NULL, *dsn = NULL;

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

    WXDBConnectionPool_Destroy(pool);
    WXFree(pool);
}
