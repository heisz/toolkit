/*
 * Test interface for the database facade wrapper.
 *
 * Copyright (C) 1997-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "dbxf.h"
#include "mem.h"

static char *blanks =
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            "
    "                                                            ";

/**
 * Main testing entry point.  Just a bunch of test instances.
 */
int main(int argc, char **argv) {
    char *user = NULL, *password = NULL, *dsn = NULL;
    char *execQuery = NULL, *rsQuery = NULL, *nmv;
    WXDBConnection *conna, *connb;
    int idx, rc, cols, *colLens;
    WXDBConnectionPool *pool;
    WXDBResultSet *crs;

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
        } else if (strcmp(argv[idx], "-x") == 0) {
            if (idx >= (argc - 1)) {
                (void) fprintf(stderr, "Error: missing -e <qry> argument\n");
                exit(1);
            }
            execQuery = argv[++idx];
        } else if (strcmp(argv[idx], "-q") == 0) {
            if (idx >= (argc - 1)) {
                (void) fprintf(stderr, "Error: missing -q <qry> argument\n");
                exit(1);
            }
            rsQuery = argv[++idx];
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

    /* Basic query instances */
    if (execQuery != NULL) {
        conna = WXDBConnectionPool_Obtain(pool);
        if (WXDBConnection_Execute(conna, execQuery) != WXDRC_OK) {
            (void) fprintf(stderr, "Failed to execute query: %s\n",
                           WXDB_GetLastErrorMessage(conna));
            exit(1);
        }
        (void) fprintf(stdout, "Query complete.  "
                               "%lld rows modified, last id %lld\n",
                       (long long int) WXDBConnection_RowsModified(conna),
                       (long long int) WXDBConnection_LastRowId(conna));

        WXDBConnectionPool_Destroy(pool);
        WXFree(pool);
        exit(0);
    }
    if (rsQuery != NULL) {
        conna = WXDBConnectionPool_Obtain(pool);
        if ((crs = WXDBConnection_ExecuteQuery(conna, rsQuery)) == NULL) {
            (void) fprintf(stderr, "Failed to execute data query: %s\n",
                           WXDB_GetLastErrorMessage(conna));
            exit(1);
        }
        cols = WXDBResultSet_ColumnCount(crs);
        colLens = (int *) WXMalloc(cols * sizeof(int));
        for (int idx = 0; idx < WXDBResultSet_ColumnCount(crs); idx++) {
            nmv = (char *) WXDBResultSet_ColumnName(crs, idx);
            colLens[idx] = strlen(nmv) + 4;
            (void) fprintf(stdout, "%s %s     ", ((idx == 0) ? "" : "|"), nmv);
        }
        (void) fprintf(stdout, "\n");

        while (WXDBResultSet_NextRow(crs)) {
            for (int idx = 0; idx < WXDBResultSet_ColumnCount(crs); idx++) {
                if (WXDBResultSet_ColumnIsNull(crs, idx)) {
                    (void) fprintf(stdout, "%s NULL%.*s ",
                                   ((idx == 0) ? "" : "|"),
                                   (int) (colLens[idx] - 4), blanks);
                    continue;
                }

                nmv = (char *) WXDBResultSet_ColumnData(crs, idx);
                (void) fprintf(stdout, "%s %.*s ", ((idx == 0) ? "" : "|"),
                               colLens[idx], nmv);
                if (strlen(nmv) < colLens[idx]) {
                    (void) fprintf(stdout, "%.*s", 
                                   (int) (colLens[idx] - strlen(nmv)), blanks);
                }
            }
            (void) fprintf(stdout, "\n");
        }
        WXFree(colLens);
        WXDBResultSet_Close(crs);

        WXDBConnectionPool_Destroy(pool);
        WXFree(pool);
        exit(0);
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

    if (WXDBConnection_Execute(conna, "INSERTEH INTO TABLEH") != WXDRC_OK) {
        (void) fprintf(stderr, "Expected error for bad query: %s\n",
                       WXDB_GetLastErrorMessage(conna));
    }

    if (WXDBConnection_ExecuteQuery(conna,
                                    "SELECT X FROM TABLEH") == NULL)  {
        (void) fprintf(stderr, "Expected error for bad query: %s\n",
                       WXDB_GetLastErrorMessage(conna));
    }

    /*
     * The following tests expect a table named 'test', first column integer
     * called 'idx' and the second a character column called 'content'.
     * Trying to stay at the lowest common denominator...
     */
    if (WXDBConnection_Execute(conna,
                               "INSERT INTO test(idx, content) "
                                   "VALUES(1, 'abc')") != WXDRC_OK) {
        (void) fprintf(stderr, "Failed to execute data insertion: %s\n",
                       WXDB_GetLastErrorMessage(conna));
        exit(1);
    }

    if ((crs = WXDBConnection_ExecuteQuery(conna,
                                 "SELECT idx, content FROM test")) == NULL) {
        (void) fprintf(stderr, "Failed to execute data query: %s\n",
                       WXDB_GetLastErrorMessage(conna));
        exit(1);
    }

    if (WXDBResultSet_ColumnCount(crs) != 2) {
        (void) fprintf(stderr, "Incorrect column count from query\n");
        exit(1);
    }

    /* Put your toys back when you are finished */
    WXDBConnectionPool_Return(conna);
    WXDBConnectionPool_Return(connb);

    /* And clean up */
    WXDBConnectionPool_Destroy(pool);
    WXFree(pool);
}
