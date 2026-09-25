/*
 * Test interface for the database facade wrapper.
 *
 * Copyright (C) 1997-2026 J.M. Heisz.  All Rights Reserved.
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

/* Common method to spit out a result set for visual checking */
static int dumpResultSet(WXDBResultSet *crs) {
    unsigned int *colLens;
    int idx, cols;
    char *nmv;

    cols = (int) WXDBResultSet_ColumnCount(crs);
    if (cols <= 0) return 0;
    colLens = (unsigned int *) WXMalloc(cols * sizeof(unsigned int));
    if (colLens == NULL) {
        (void) fprintf(stderr, "Out of memory for column tracking\n");
        return -1;
    }

    for (idx = 0; idx < cols; idx++) {
        nmv = (char *) WXDBResultSet_ColumnName(crs, idx);
        if (nmv == NULL) nmv = "?";
        colLens[idx] = strlen(nmv) + 4;
        (void) fprintf(stdout, "%s %s     ", ((idx == 0) ? "" : "|"), nmv);
    }
    (void) fprintf(stdout, "\n");

    while (WXDBResultSet_NextRow(crs)) {
        for (idx = 0; idx < cols; idx++) {
            if (WXDBResultSet_ColumnIsNull(crs, idx)) {
                (void) fprintf(stdout, "%s NULL%.*s ",
                               ((idx == 0) ? "" : "|"),
                               (int) (colLens[idx] - 4), blanks);
                continue;
            }

            nmv = (char *) WXDBResultSet_ColumnData(crs, idx);
            (void) fprintf(stdout, "%s %.*s ", ((idx == 0) ? "" : "|"),
                           (int) colLens[idx], nmv);
            if (strlen(nmv) < colLens[idx]) {
                (void) fprintf(stdout, "%.*s",
                               (int) (colLens[idx] - strlen(nmv)), blanks);
            }
        }
        (void) fprintf(stdout, "\n");
    }

    WXFree(colLens);
    return 0;
}

/* Count the rows in a result set and release */
static int countRowsAndClose(WXDBResultSet *rs) {
    int cnt = 0;

    if (rs == NULL) return -1;
    while (WXDBResultSet_NextRow(rs)) cnt++;
    WXDBResultSet_Close(rs);

    return cnt;
}

/* Test table rebuild/definitions (auto-setup and failure cleanup) */
static char *setupStmts[] = {
    "DROP TABLE IF EXISTS test",
    "DROP TABLE IF EXISTS test_uniq",
    "CREATE TABLE test (idx INTEGER, content VARCHAR(64))",
    "CREATE TABLE test_uniq (idx INTEGER PRIMARY KEY, content VARCHAR(64))"
};

#define SETUP_STMT_COUNT (sizeof(setupStmts) / sizeof(char *))

static void prepareTables(WXDBConnectionPool *pool) {
    WXDBConnection *conn;
    unsigned int idx;

    conn = WXDBConnectionPool_Obtain(pool);
    if (conn == NULL) {
        (void) fprintf(stderr, "Failed to obtain a setup connection: %s\n",
                       WXDB_GetLastErrorMessage(pool));
        exit(1);
    }

    for (idx = 0; idx < SETUP_STMT_COUNT; idx++) {
        if (WXDBConnection_Execute(conn, setupStmts[idx]) != WXDRC_OK) {
            (void) fprintf(stderr, "Setup '%s' failed: %s\n",
                           setupStmts[idx], WXDB_GetLastErrorMessage(conn));
            exit(1);
        }
    }

    WXDBConnectionPool_Return(conn);
}

/* Test result (set) draining and setup for connection reuse on error */
static void testReuseOnError(WXDBConnectionPool *pool) {
    WXDBConnection *conn;
    WXDBResultSet *rs;

    conn = WXDBConnectionPool_Obtain(pool);
    if (conn == NULL) {
        (void) fprintf(stderr, "Failed to obtain a reuse connection: %s\n",
                       WXDB_GetLastErrorMessage(pool));
        exit(1);
    }

    /* First, a result set query that fails */
    if (WXDBConnection_Execute(conn,
                        "SELECT * FROM no_such_table_xyz") == WXDRC_OK) {
        (void) fprintf(stderr, "Bad query did not fail (reuse)\n");
        exit(1);
    }
    if (strlen(WXDB_GetLastErrorMessage(conn)) == 0) {
        (void) fprintf(stderr, "Bad query missing error message (reuse)\n");
        exit(1);
    }

    /* Should still be able to continue with valid query */
    if (WXDBConnection_Execute(conn, "INSERT INTO test_uniq(idx, content) "
                                         "VALUES(1, 'first')") != WXDRC_OK) {
        (void) fprintf(stderr, "Insert failed after error (reuse): %s\n",
                       WXDB_GetLastErrorMessage(conn));
        exit(1);
    }
    if (WXDBConnection_RowsModified(conn) != 1) {
        (void) fprintf(stderr, "Incorrect insert modified count (reuse)\n");
        exit(1);
    }

    /* Second, fail on a DML query */
    if (WXDBConnection_Execute(conn, "INSERT INTO test_uniq(idx, content) "
                                     "VALUES(1, 'duplicate')") == WXDRC_OK) {
        (void) fprintf(stderr, "Duplicate key insert succeeded (reuse)?\n");
        exit(1);
    }
    if (strlen(WXDB_GetLastErrorMessage(conn)) == 0) {
        (void) fprintf(stderr, "Duplicate key error has no error message\n");
        exit(1);
    }

    /* And a subsequent result set query should succeed */
    rs = WXDBConnection_ExecuteQuery(conn, "SELECT idx FROM test_uniq");
    if (rs == NULL) {
        (void) fprintf(stderr, "Query after dml error (reuse): %s\n",
                       WXDB_GetLastErrorMessage(conn));
        exit(1);
    }
    if (countRowsAndClose(rs) != 1) {
        (void) fprintf(stderr, "Incorrect query row count after error "
                               "(reuse)\n");
        exit(1);
    }

    /* Check connection still functional after bounce back to pool */
    WXDBConnectionPool_Return(conn);
    conn = WXDBConnectionPool_Obtain(pool);
    if (conn == NULL) {
        (void) fprintf(stderr, "Failed to re-obtain a connection (reuse): %s\n",
                       WXDB_GetLastErrorMessage(pool));
        exit(1);
    }
    if (WXDBConnection_Execute(conn, "DELETE FROM test_uniq") != WXDRC_OK) {
        (void) fprintf(stderr, "Test cleanup error (reuse): %s\n",
                       WXDB_GetLastErrorMessage(conn));
        exit(1);
    }

    WXDBConnectionPool_Return(conn);
}

/* Test multi-statement with failure - report error and rollback internally */
static void testMultiStatement(WXDBConnectionPool *pool) {
    WXDBConnection *conn;
    WXDBResultSet *rs;

    conn = WXDBConnectionPool_Obtain(pool);
    if (conn == NULL) {
        (void) fprintf(stderr, "Failed to obtain a multi connection: %s\n",
                       WXDB_GetLastErrorMessage(pool));
        exit(1);
    }

    if (WXDBConnection_Execute(conn,
                "INSERT INTO test_uniq(idx, content) VALUES(99, 'x'); "
                    "this is not valid sql") == WXDRC_OK) {
        (void) fprintf(stderr, "Multi statement with invalid succeeded?\n");
        exit(1);
    }
    if (strlen(WXDB_GetLastErrorMessage(conn)) == 0) {
        (void) fprintf(stderr, "Multi invalid has no error message\n");
        exit(1);
    }

    rs = WXDBConnection_ExecuteQuery(conn,
                        "SELECT idx FROM test_uniq WHERE idx = 99");
    if (rs == NULL) {
        (void) fprintf(stderr, "Rollback test query error: %s\n",
                       WXDB_GetLastErrorMessage(conn));
        exit(1);
    }
    if (countRowsAndClose(rs) != 0) {
        (void) fprintf(stderr, "Rollback - row was not removed\n");
        exit(1);
    }

    WXDBConnectionPool_Return(conn);
}

/* Very large result set, generating multiple network round trips */
static void testLargeResult(WXDBConnectionPool *pool) {
    WXDBConnection *conn;
    WXDBResultSet *rs;

    conn = WXDBConnectionPool_Obtain(pool);
    if (conn == NULL) {
        (void) fprintf(stderr, "Failed to obtain a large connection: %s\n",
                       WXDB_GetLastErrorMessage(pool));
        exit(1);
    }

    rs = WXDBConnection_ExecuteQuery(conn,
                        "SELECT generate_series(1, 50000) AS num");
    if (rs == NULL) {
        (void) fprintf(stderr, "Large result set query error: %s\n",
                       WXDB_GetLastErrorMessage(conn));
        exit(1);
    }
    if (countRowsAndClose(rs) != 50000) {
        (void) fprintf(stderr, "Incorrect large result set row count\n");
        exit(1);
    }

    WXDBConnectionPool_Return(conn);
}

/* Collection of actions based on prepared statement instances */
static void testPreparedStatements(WXDBConnectionPool *pool) {
    WXDBStatement *stmt;
    WXDBConnection *conn;
    WXDBResultSet *rs;

    conn = WXDBConnectionPool_Obtain(pool);
    if (conn == NULL) {
        (void) fprintf(stderr, "Failed to obtain a prepare connection: %s\n",
                       WXDB_GetLastErrorMessage(pool));
        exit(1);
    }

    /* One prepared statement, two separate binds/executes/inserts */
    stmt = WXDBConnection_Prepare(conn,
                     "INSERT INTO test_uniq(idx, content) VALUES(?, ?)");
    if (stmt == NULL) {
        (void) fprintf(stderr, "Prepare of insert failed: %s\n",
                       WXDB_GetLastErrorMessage(conn));
        exit(1);
    }

    if ((WXDBStatement_BindInt(stmt, 0, 12) != WXDRC_OK) ||
            (WXDBStatement_BindString(stmt, 1, "twelve") != WXDRC_OK)) {
        (void) fprintf(stderr, "Prepared bind failure: %s\n",
                       WXDB_GetLastErrorMessage(stmt));
        exit(1);
    }
    if (WXDBStatement_Execute(stmt) != WXDRC_OK) {
        (void) fprintf(stderr, "First prepared execute failed: %s\n",
                       WXDB_GetLastErrorMessage(stmt));
        exit(1);
    }
    if (WXDBStatement_RowsModified(stmt) != 1) {
        (void) fprintf(stderr, "Incorrect first execute modified count\n");
        exit(1);
    }

    if ((WXDBStatement_BindInt(stmt, 0, 24) != WXDRC_OK) ||
            (WXDBStatement_BindString(stmt, 1, "two-four") != WXDRC_OK)) {
        (void) fprintf(stderr, "Prepared rebind failure: %s\n",
                       WXDB_GetLastErrorMessage(stmt));
        exit(1);
    }
    if (WXDBStatement_Execute(stmt) != WXDRC_OK) {
        (void) fprintf(stderr, "Second prepared execute failed: %s\n",
                       WXDB_GetLastErrorMessage(stmt));
        exit(1);
    }
    if (WXDBStatement_RowsModified(stmt) != 1) {
        (void) fprintf(stderr, "Incorrect second execute modified count\n");
        exit(1);
    }

    /* Check for error handling with incorrect parameter indices */
    if (WXDBStatement_BindInt(stmt, 5, 1) != WXDRC_SYS_ERROR) {
        (void) fprintf(stderr, "Invalid bind index was accepted\n");
        exit(1);
    }
    if (WXDBStatement_BindInt(stmt, -1, 1) != WXDRC_SYS_ERROR) {
        (void) fprintf(stderr, "Negative bind index was accepted\n");
        exit(1);
    }

    WXDBStatement_Close(stmt);

    /* Now repeat with query/result set execution */
    stmt = WXDBConnection_Prepare(conn,
                     "SELECT idx, content FROM test_uniq WHERE idx = ?");
    if (stmt == NULL) {
        (void) fprintf(stderr, "Prepare of select failed: %s\n",
                       WXDB_GetLastErrorMessage(conn));
        exit(1);
    }

    if (WXDBStatement_BindInt(stmt, 0, 12) != WXDRC_OK) {
        (void) fprintf(stderr, "Bind for prepared select failed: %s\n",
                       WXDB_GetLastErrorMessage(stmt));
        exit(1);
    }
    rs = WXDBStatement_ExecuteQuery(stmt);
    if (rs == NULL) {
        (void) fprintf(stderr, "Prepared execute query failed: %s\n",
                       WXDB_GetLastErrorMessage(stmt));
        exit(1);
    }
    if (WXDBResultSet_ColumnCount(rs) != 2) {
        (void) fprintf(stderr, "Incorrect prepared select column count\n");
        exit(1);
    }
    if (!WXDBResultSet_NextRow(rs)) {
        (void) fprintf(stderr, "Prepared select returned no rows\n");
        exit(1);
    }
    if (strcmp(WXDBResultSet_ColumnData(rs, 1), "twelve") != 0) {
        (void) fprintf(stderr, "Incorrect prepared select content value\n");
        exit(1);
    }

    /* Execute again with open result set, not allowed */
    if (WXDBStatement_BindInt(stmt, 0, 24) != WXDRC_OK) {
        (void) fprintf(stderr, "Rebind for re-execute failed: %s\n",
                       WXDB_GetLastErrorMessage(stmt));
        exit(1);
    }
    if (WXDBStatement_Execute(stmt) == WXDRC_OK) {
        (void) fprintf(stderr, "Execute with open result set did not fail\n");
        exit(1);
    }
    WXDBResultSet_Close(rs);
    WXDBStatement_Close(stmt);

    /* Attempt a zero parameter prepared query (previously failed) */
    stmt = WXDBConnection_Prepare(conn, "SELECT 1");
    if (stmt == NULL) {
        (void) fprintf(stderr, "Prepare of zero parameter query failed: %s\n",
                       WXDB_GetLastErrorMessage(conn));
        exit(1);
    }
    rs = WXDBStatement_ExecuteQuery(stmt);
    if (rs == NULL) {
        (void) fprintf(stderr, "Zero parameter execute failed: %s\n",
                       WXDB_GetLastErrorMessage(stmt));
        exit(1);
    }
    if (countRowsAndClose(rs) != 1) {
        (void) fprintf(stderr, "Incorrect zero parameter row count\n");
        exit(1);
    }
    WXDBStatement_Close(stmt);

    /* Test a quoted/string-embedded parameter marker (not!) */
    stmt = WXDBConnection_Prepare(conn,
               "SELECT idx FROM test_uniq WHERE content = 'a?b' AND idx = ?");
    if (stmt == NULL) {
        (void) fprintf(stderr, "Prepare with quoted marker failed: %s\n",
                       WXDB_GetLastErrorMessage(conn));
        exit(1);
    }
    if (WXDBStatement_BindInt(stmt, 0, 10) != WXDRC_OK) {
        (void) fprintf(stderr, "Quoted marker (actual) bind failure: %s\n",
                       WXDB_GetLastErrorMessage(stmt));
        exit(1);
    }
    if (WXDBStatement_BindInt(stmt, 1, 10) != WXDRC_SYS_ERROR) {
        (void) fprintf(stderr, "Incorrect param limit with quoted marker\n");
        exit(1);
    }
    rs = WXDBStatement_ExecuteQuery(stmt);
    if (rs == NULL) {
        (void) fprintf(stderr, "Quoted marker execute failed: %s\n",
                       WXDB_GetLastErrorMessage(stmt));
        exit(1);
    }
    (void) countRowsAndClose(rs);
    WXDBStatement_Close(stmt);

    /* A very large double value (overflow) */
    stmt = WXDBConnection_Prepare(conn, "SELECT ?::float8 AS val");
    if (stmt == NULL) {
        (void) fprintf(stderr, "Prepare of double failed: %s\n",
                       WXDB_GetLastErrorMessage(conn));
        exit(1);
    }
    if (WXDBStatement_BindDouble(stmt, 0, 1.0e300) != WXDRC_OK) {
        (void) fprintf(stderr, "Bind of very large double value failed: %s\n",
                       WXDB_GetLastErrorMessage(stmt));
        exit(1);
    }
    rs = WXDBStatement_ExecuteQuery(stmt);
    if (rs == NULL) {
        (void) fprintf(stderr, "Large double execute failed: %s\n",
                       WXDB_GetLastErrorMessage(stmt));
        exit(1);
    }
    (void) countRowsAndClose(rs);
    WXDBStatement_Close(stmt);

    /* Purge the test instance data */
    if (WXDBConnection_Execute(conn, "DELETE FROM test_uniq") != WXDRC_OK) {
        (void) fprintf(stderr, "Prepared statement cleanup error: %s\n",
                       WXDB_GetLastErrorMessage(conn));
        exit(1);
    }

    WXDBConnectionPool_Return(conn);
}

/**
 * Main testing entry point.  Just a bunch of test instances.
 */
int main(int argc, char **argv) {
    char *user = NULL, *password = NULL, *dsn = NULL;
    char *execQuery = NULL, *rsQuery = NULL;
    WXDBConnection *conna, *connb;
    WXDBConnectionPool *pool;
    WXDBResultSet *crs;
    int idx;

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
    if (pool == NULL) {
        (void) fprintf(stderr, "Failed to allocate the test pool\n");
        exit(1);
    }
    if (WXDBConnectionPool_Init(pool, dsn, user, password, 1) < 0) {
        (void) fprintf(stderr, "Failed to initialize the test pool: %s\n",
                       WXDB_GetLastErrorMessage(pool));
        WXFree(pool);
        exit(1);
    }

    /* Basic query instances */
    if (execQuery != NULL) {
        conna = WXDBConnectionPool_Obtain(pool);
        if (conna == NULL) {
            (void) fprintf(stderr, "Failed to obtain a connection: %s\n",
                           WXDB_GetLastErrorMessage(pool));
            WXDBConnectionPool_Destroy(pool);
            WXFree(pool);
            exit(1);
        }
        if (WXDBConnection_Execute(conna, execQuery) != WXDRC_OK) {
            (void) fprintf(stderr, "Failed to execute query: %s\n",
                           WXDB_GetLastErrorMessage(conna));
            WXDBConnectionPool_Destroy(pool);
            WXFree(pool);
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
        if (conna == NULL) {
            (void) fprintf(stderr, "Failed to obtain a connection: %s\n",
                           WXDB_GetLastErrorMessage(pool));
            WXDBConnectionPool_Destroy(pool);
            WXFree(pool);
            exit(1);
        }
        if ((crs = WXDBConnection_ExecuteQuery(conna, rsQuery)) == NULL) {
            (void) fprintf(stderr, "Failed to execute data query: %s\n",
                           WXDB_GetLastErrorMessage(conna));
            WXDBConnectionPool_Destroy(pool);
            WXFree(pool);
            exit(1);
        }
        (void) dumpResultSet(crs);
        WXDBResultSet_Close(crs);

        WXDBConnectionPool_Destroy(pool);
        WXFree(pool);
        exit(0);
    }

    /* Grab a couple of connections */
    conna = WXDBConnectionPool_Obtain(pool);
    connb = WXDBConnectionPool_Obtain(pool);
    if ((conna == NULL) || (connb == NULL)) {
        (void) fprintf(stderr, "Failed to obtain test connections: %s\n",
                       WXDB_GetLastErrorMessage(pool));
        WXDBConnectionPool_Destroy(pool);
        WXFree(pool);
        exit(1);
    }

    if (!WXDBConnection_Ping(conna)) {
        (void) fprintf(stderr, "Ping failed on a fresh connection: %s\n",
                       WXDB_GetLastErrorMessage(conna));
        exit(1);
    }

    /* (Re)build the underlying test tables */
    prepareTables(pool);

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

    /* Rollback outside of a transaction is an error */
    if (WXDBConnection_TxnRollback(conna, "test") == WXDRC_OK) {
        (void) fprintf(stderr, "Rollback outside a transaction did not "
                               "fail\n");
        exit(1);
    }
    if (strlen(WXDB_GetLastErrorMessage(conna)) == 0) {
        (void) fprintf(stderr, "Invalid rollback has no error message\n");
        exit(1);
    }

    if (WXDBConnection_RowsModified(connb) > 0) {
        (void) fprintf(stderr, "Rows modified for non-update scenario\n");
        exit(1);
    }
    if (WXDBConnection_LastRowId(connb) != 0) {
        (void) fprintf(stderr, "Row id for non-insert scenario\n");
        exit(1);
    }

    if (WXDBConnection_Execute(conna, "INSERTEH INTO TABLEH") == WXDRC_OK) {
        (void) fprintf(stderr, "Invalid statement did not fail\n");
        exit(1);
    }
    if (strlen(WXDB_GetLastErrorMessage(conna)) == 0) {
        (void) fprintf(stderr, "Invalid statement has no error message\n");
        exit(1);
    }

    if (WXDBConnection_ExecuteQuery(conna, "SELECT X FROM TABLEH") != NULL) {
        (void) fprintf(stderr, "Query on missing table did not fail\n");
        exit(1);
    }
    if (strlen(WXDB_GetLastErrorMessage(conna)) == 0) {
        (void) fprintf(stderr, "Failed query has no error message\n");
        exit(1);
    }

    /* Executing a result-set query is an error */
    if (WXDBConnection_Execute(conna, "SELECT 1") == WXDRC_OK) {
        (void) fprintf(stderr, "Result-set query through Execute did not "
                               "fail\n");
        exit(1);
    }
    if (strlen(WXDB_GetLastErrorMessage(conna)) == 0) {
        (void) fprintf(stderr, "Result-set Execute has no error message\n");
        exit(1);
    }

    if (WXDBConnection_Execute(conna,
                               "INSERT INTO test(idx, content) "
                                   "VALUES(1, 'abc')") != WXDRC_OK) {
        (void) fprintf(stderr, "Failed to execute data insertion: %s\n",
                       WXDB_GetLastErrorMessage(conna));
        exit(1);
    }
    if (WXDBConnection_RowsModified(conna) != 1) {
        (void) fprintf(stderr, "Incorrect insert modified count\n");
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
    if (strcmp(WXDBResultSet_ColumnName(crs, 0), "idx") != 0) {
        (void) fprintf(stderr, "Incorrect first column name\n");
        exit(1);
    }

    /* Select should have row count and result set persists locally */
    if (WXDBConnection_RowsModified(conna) <= 0) {
        (void) fprintf(stderr, "Missing row count for select\n");
        exit(1);
    }
    if (WXDBConnection_Execute(conna, "DELETE FROM test") != WXDRC_OK) {
        (void) fprintf(stderr, "Delete with open result set error: %s\n",
                       WXDB_GetLastErrorMessage(conna));
        exit(1);
    }
    if (!WXDBResultSet_NextRow(crs)) {
        (void) fprintf(stderr, "Open result set failed to traverse\n");
        exit(1);
    }
    if (WXDBResultSet_ColumnData(crs, 1) == NULL) {
        (void) fprintf(stderr, "Open result set failed to read data\n");
        exit(1);
    }
    WXDBResultSet_Close(crs);

    /* Put your toys back when you are finished */
    WXDBConnectionPool_Return(conna);
    WXDBConnectionPool_Return(connb);

    /* Other test areas are self-contained */
    testReuseOnError(pool);
    testMultiStatement(pool);
    testLargeResult(pool);
    testPreparedStatements(pool);

    /* And clean up */
    WXDBConnectionPool_Destroy(pool);
    WXFree(pool);

    (void) fprintf(stdout, "All database tests passed.\n");

    return 0;
}
