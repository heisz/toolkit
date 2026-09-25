/*
 * PostgreSQL-specific implementations for the db facade layer.
 *
 * Copyright (C) 2003-2026 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "dbxfint.h"
#include <libpq-fe.h>
#include "buffer.h"
#include "hash.h"

/* PostgreSQL specific connection object */
typedef struct WXPGSQLConnection {
    /* This appears first for virtual inheritance */
    WXDBConnection base;

    /* PostgreSQL-specific elements follow */
    PGconn *db;
    uint32_t pstmtCount;
    int64_t lastRowsModified;
    uint64_t lastRowId;

    /* Track dangling queries in flight on the connection, for cleanup/reuse */
    int needsReset;
} WXPGSQLConnection;

/* Local copies of non-string bound variable content */
typedef struct WXPGSQLLocalParam {
    /* PostgreSQL takes parameter arguments as strings */
    char content[32];
} WXPGSQLLocalParam;

typedef struct WXPGSQLStatement {
    /* This appears first for virtual inheritance */
    WXDBStatement base;

    /* Dynamic statement name, generated from connection counter */
    char stmtName[64];

    /* Store the parameter count for optimized management */
    uint32_t paramCount;

    /* Binding elements for the prepared statement parameters (API names) */
    char **paramValues;
    int *paramLengths;
    int *paramFormats;

    /* Local storage instance for non-string parameters */
    WXPGSQLLocalParam *localParams;

    /* Like connection, results from last exec call (from the result) */
    int64_t lastRowsModified;
    uint64_t lastRowId;
} WXPGSQLStatement;

typedef struct WXPGSQLResultSet {
    /* This appears first for virtual inheritance */
    WXDBResultSet base;

    /* Associated query result set */
    PGresult *rslt;

    /* Store the column count for optimized error checking */
    uint32_t columnCount, currentRow, rowCount;
} WXPGSQLResultSet;

/* Bunch of utility methods for error recording */
static void recordConnError(PGconn *db, char *errorMsg) {
    char *msg = (db != NULL) ? PQerrorMessage(db) : NULL;

    if (errorMsg == NULL) return;
    if ((msg != NULL) && (*msg != '\0')) {
        _dbxfStrNCpy(errorMsg, msg, WXDB_FIXED_ERROR_SIZE);
    } else {
        /* Something else went wrong, not determined by libpq */
        (void) strcpy(errorMsg, "Database connection wait/abort failure");
    }
}

static int recordBindError(WXDBStatement *stmt, int paramIdx,
                           uint32_t paramCount) {
    (void) snprintf(stmt->lastErrorMsg, WXDB_FIXED_ERROR_SIZE,
                    "Bind parameter index %d out of range, statement has "
                    "%u parameter(s)", paramIdx, (unsigned int) paramCount);

    return WXDRC_SYS_ERROR;
}

static void recordResultError(PGresult *rslt, char *errorMsg) {
    char *msg = PQresultErrorMessage(rslt);

    if (errorMsg == NULL) return;
    if ((msg != NULL) && (*msg != '\0')) {
        _dbxfStrNCpy(errorMsg, msg, WXDB_FIXED_ERROR_SIZE);
        return;
    }

    /* Fall back to the generic status */
    (void) snprintf(errorMsg, WXDB_FIXED_ERROR_SIZE,
                    "Unexpected result status from database: %s",
                    PQresStatus(PQresultStatus(rslt)));
}

/* Similar utility to extract copies of result related data */
static void captureResultInfo(PGresult *rslt, int64_t *rowsRef,
                              uint64_t *rowIdRef) {
    char *cnt = PQcmdTuples(rslt);
    *rowsRef = (strlen(cnt) == 0) ? -1 : ((int64_t) atoll(cnt));
    *rowIdRef = (uint64_t) PQoidValue(rslt);
}

/* PQ poll handling function prototype (for next method) */
typedef PostgresPollingStatusType (*pgPollFn)(PGconn *);

/* Shared method for managing asynchronous connection-level poll operations */
static int connectPollHandler(PGconn *db, pgPollFn pollFn) {
    int sock, prevSock = -1, hadError = FALSE;
    PostgresPollingStatusType status;
    uint32_t conditions;

    /* Start in write state (poll handler starts with wait) */
    status = PGRES_POLLING_WRITING;
    while (TRUE) {
        /* Need to translate between the event indicators */
        if (status == PGRES_POLLING_READING) {
            conditions = WXNRC_READ_REQUIRED;
        } else if (status == PGRES_POLLING_WRITING) {
            conditions = WXNRC_WRITE_REQUIRED;
        } else {
            break;
        }

        /* libpq can alter the socket between calls, need to manage release */
        sock = PQsocket(db);
        if ((prevSock >= 0) && (prevSock != sock)) {
            _dbxfSocketRelease(prevSock);
        }
        prevSock = sock;

        /* Perform the wait */
        if (_dbxfSocketWait(sock, conditions) == 0) {
            hadError = TRUE;
            break;
        }

        /* And let the polling function handle the outcome */
        status = (*pollFn)(db);
    }

    /* Ensure detachment of any associated connection socket instances */
    if (prevSock >= 0) _dbxfSocketRelease(prevSock);
    sock = PQsocket(db);
    if ((sock >= 0) && (sock != prevSock)) _dbxfSocketRelease(sock);

    return ((!hadError) && (status == PGRES_POLLING_OK)) ? TRUE : FALSE;
}

/* Similar function to handle asynchronous polling on query/result execution */
static PGresult *waitResult(WXPGSQLConnection *pgConn, char *errorMsg) {
    PGresult *rslt = NULL, *tmp;
    int rc, hadError = FALSE;
    ExecStatusType status;
    uint32_t events;

    /* First we need to ensure the outgoing query has been sent to the server */
    while (TRUE) {
        /* Flush outstanding write buffer, zero indicates complete */
        rc = PQflush(pgConn->db);
        if (rc == 0) break;
        if (rc < 0) {
            hadError = TRUE;
            break;
        }

        /* Wait for additional write, also need read for exchanges */
        events = _dbxfSocketWait(PQsocket(pgConn->db),
                                 WXNRC_READ_REQUIRED | WXNRC_WRITE_REQUIRED);
        if (events == 0) {
            hadError = TRUE;
            break;
        }
        if (((events & WXNRC_READ_REQUIRED) != 0) &&
                (PQconsumeInput(pgConn->db) != 1)) {
            hadError = TRUE;
            break;
        }
    }

    /* Now process the results/response content */
    while (!hadError) {
        /* Wait and process inbound until an end marker is hit (non-busy) */
        while (PQisBusy(pgConn->db) == 1) {
            events = _dbxfSocketWait(PQsocket(pgConn->db),
                                    WXNRC_READ_REQUIRED);
            if ((events == 0) || (PQconsumeInput(pgConn->db) != 1)) {
                hadError = TRUE;
                break;
            }
        }
        if (hadError) break;

        /* Translate result, discarding prior if found (highlander) */
        tmp = PQgetResult(pgConn->db);
        if (tmp == NULL) break;
        if (rslt != NULL) PQclear(rslt);
        rslt = tmp;

        /* This API does not support copy operations (abort in reset mode) */
        status = PQresultStatus(rslt);
        if ((status == PGRES_COPY_IN) || (status == PGRES_COPY_OUT) ||
                (status == PGRES_COPY_BOTH)) {
            pgConn->needsReset = TRUE;
            break;
        }
        if (PQstatus(pgConn->db) == CONNECTION_BAD) {
            pgConn->needsReset = TRUE;
            break;
        }
    }

    /* Ensure detachment of any associated connection socket instances */
    _dbxfSocketRelease(PQsocket(pgConn->db));

    if (hadError) {
        /* On any error, there may be incomplete traffic to resolve */
        pgConn->needsReset = TRUE;

        if (rslt != NULL) {
            PQclear(rslt);
            rslt = NULL;
        }
        recordConnError(pgConn->db, errorMsg);
    }

    return rslt;
}

/* As mentioned above, any interrupted/errored handling needs explicit reset */
static int pgsqlResetConn(WXPGSQLConnection *pgConn, char *errorMsg) {
    if (PQresetStart(pgConn->db) != 1) {
        recordConnError(pgConn->db, errorMsg);
        return FALSE;
    }
    if (!connectPollHandler(pgConn->db, PQresetPoll)) {
        recordConnError(pgConn->db, errorMsg);
        return FALSE;
    }
    if (PQsetnonblocking(pgConn->db, 1) != 0) {
        recordConnError(pgConn->db, errorMsg);
        return FALSE;
    }

    /* NOTE: at this point all data (including prepares) are destroyed */

    pgConn->needsReset = FALSE;
    return TRUE;
}

/* Common method for result set creation from statement execution */
static WXDBResultSet *createResultSet(WXDBConnection *conn,
                                      WXDBStatement *pstmt,
                                      PGresult *rslt) {
    WXPGSQLResultSet *res;

    /* Allocate up front */
    res = (WXPGSQLResultSet *) WXMalloc(sizeof(WXPGSQLResultSet));
    if (res == NULL) {
        return NULL;
    }
    res->base.parentConn = conn;
    res->base.parentStmt = pstmt;
    res->base.driver = (conn != NULL) ? conn->driver : pstmt->driver;
    res->rslt = rslt;

    /* Optimize */
    res->columnCount = (uint32_t) PQnfields(rslt);
    res->currentRow = (uint32_t) -1;
    res->rowCount = PQntuples(rslt);

    return (WXDBResultSet *) res;
}

/* Utility method to quote/escape a string value into the provided buffer */
static int appendParameter(WXBuffer *buffer, char *param, char *val) {
    int len = strlen(param) + 1 + 2 * strlen(val) + 2 + 2;
    char ch;

    if (WXBuffer_EnsureCapacity(buffer, len, TRUE) == NULL) return FALSE;

    if (buffer->length != 0) buffer->buffer[buffer->length++] = ' ';
    (void) WXBuffer_Append(buffer, param, strlen(param), TRUE);
    buffer->buffer[buffer->length++] = '=';
    buffer->buffer[buffer->length++] = '\'';
    while ((ch = *(val++)) != '\0') {
        if ((ch == '\\') || (ch == '\'')) {
            buffer->buffer[buffer->length++] = '\\';
        }
        buffer->buffer[buffer->length++] = ch;
    }
    buffer->buffer[buffer->length++] = '\'';
    buffer->buffer[buffer->length] = '\0';

    return TRUE;
}

/**
 * Connection (DSN) Options:
 *     host - the hostname for the database connection (not unix socket)
 *     port - the associated port for the above
 *     unix_socket - the name of the unix socket to connect to (overrides any
 *                   host/port specification)
 *     dbname - the name of the initial database to connect to
 *     charset - the default character set for the connection
 *
 *     <user> - the authentication username (from DSN or options)
 *     <password> - the authentication password (ditto)
 */

/***** Connection management operations *****/

/* Create the PgSQL connection string from the connection pool options */
static int buildConnectDSN(WXDBConnectionPool *pool, WXBuffer *params) {
    WXHashTable *options = &(pool->options);
    char *opt;

    opt = (char *) WXHash_GetEntry(options, "unix_socket",
                                   WXHash_StrHashFn, WXHash_StrEqualsFn);
    if (opt != NULL) {
        if (!appendParameter(params, "host", opt)) return FALSE;
    } else {
        opt = (char *) WXHash_GetEntry(options, "host",
                                       WXHash_StrHashFn, WXHash_StrEqualsFn);
        if (opt != NULL) {
            if (!appendParameter(params, "host", opt)) return FALSE;
        }

        opt = (char *) WXHash_GetEntry(options, "port",
                                        WXHash_StrHashFn, WXHash_StrEqualsFn);
        if (opt != NULL) {
            if (!appendParameter(params, "port", opt)) return FALSE;
        }
    }

    opt = (char *) WXHash_GetEntry(options, "dbname",
                                   WXHash_StrHashFn, WXHash_StrEqualsFn);
    if (opt != NULL) {
        if (!appendParameter(params, "dbname", opt)) return FALSE;
    }

    opt = (char *) WXHash_GetEntry(options, "user",
                                   WXHash_StrHashFn, WXHash_StrEqualsFn);
    if (opt != NULL) {
        if (!appendParameter(params, "user", opt)) return FALSE;
    }
    opt = (char *) WXHash_GetEntry(options, "password",
                                   WXHash_StrHashFn, WXHash_StrEqualsFn);
    if (opt != NULL) {
        if (!appendParameter(params, "password", opt)) return FALSE;
    }

    return TRUE;
}

static int WXDBPGSQLConnection_Create(WXDBConnectionPool *pool,
                                      WXDBConnection **connRef) {
    WXPGSQLConnection *conn;
    char paramBuff[2048];
    WXBuffer params;

    /* Allocate the extended object instance */
    conn = (WXPGSQLConnection *) WXMalloc(sizeof(WXPGSQLConnection));
    if (conn == NULL) {
        _dbxfMemFail(pool->lastErrorMsg);
        return WXDRC_MEM_ERROR;
    }
    conn->db = NULL;
    conn->pstmtCount = 0;
    conn->needsReset = FALSE;
    conn->lastRowsModified = -1;
    conn->lastRowId = 0;

    /* Build up the connection string */
    WXBuffer_InitLocal(&params, paramBuff, sizeof(paramBuff));
    *paramBuff = '\0';
    if (!buildConnectDSN(pool, &params)) {
        _dbxfMemFail(pool->lastErrorMsg);
        WXBuffer_Destroy(&params);
        WXFree(conn);
        return WXDRC_MEM_ERROR;
    }

    /* Reach out and touch someone... */
    conn->db = PQconnectStart((char *) params.buffer);
    WXBuffer_Destroy(&params);
    if (conn->db == NULL) {
        _dbxfMemFail(pool->lastErrorMsg);
        WXFree(conn);
        return WXDRC_MEM_ERROR;
    }
    if (PQstatus(conn->db) == CONNECTION_BAD) {
        recordConnError(conn->db, pool->lastErrorMsg);
        PQfinish(conn->db);
        WXFree(conn);
        return WXDRC_DB_ERROR;
    }

    /* Handle synch/asynch connection via the handler */
    if (!connectPollHandler(conn->db, PQconnectPoll)) {
        recordConnError(conn->db, pool->lastErrorMsg);
        PQfinish(conn->db);
        WXFree(conn);
        return WXDRC_DB_ERROR;
    }

    /* Force the write to be non-blocking for large statement/parameter sets */
    if (PQsetnonblocking(conn->db, 1) != 0) {
        recordConnError(conn->db, pool->lastErrorMsg);
        PQfinish(conn->db);
        WXFree(conn);
        return WXDRC_DB_ERROR;
    }

    /* All done, connection is ready for use */
    *connRef = &(conn->base);
    return WXDRC_OK;
}

static void WXDBPGSQLConnection_Destroy(WXDBConnection *conn) {
    WXPGSQLConnection *pgConn = (WXPGSQLConnection *) conn;

    /* Make sure to drop any pollInfo attached socket */
    _dbxfSocketRelease(PQsocket(pgConn->db));
    PQfinish(pgConn->db);
}

static int WXDBPGSQLConnection_Ping(WXDBConnection *conn) {
    /* There really isn't a ping, just check current connection status */
    return (PQstatus(((WXPGSQLConnection *) conn)->db) == CONNECTION_OK) ?
                                                                  TRUE : FALSE;
}

/***** Connection query operations *****/

/* Handle result data reset as well as connection flush/reset */
static int resetConnResults(WXDBConnection *conn) {
    WXPGSQLConnection *pgConn = (WXPGSQLConnection *) conn;

    pgConn->lastRowsModified = -1;
    pgConn->lastRowId = 0;
    *(conn->lastErrorMsg) = '\0';

    /* Clean up from any earlier connection failures */
    if (pgConn->needsReset) {
        if (!pgsqlResetConn(pgConn, conn->lastErrorMsg)) {
            return WXDRC_DB_ERROR;
        }
    }

    return WXDRC_OK;
}

/* Common method for transaction control operations (fixed statements) */
static int txnCommand(WXDBConnection *conn, const char *cmd) {
    WXPGSQLConnection *pgConn = (WXPGSQLConnection *) conn;
    PGresult *rslt;
    int rc;

    if ((rc = resetConnResults(conn)) != WXDRC_OK) return rc;

    if (PQsendQuery(pgConn->db, cmd) != 1) {
        _dbxfStrNCpy(conn->lastErrorMsg, PQerrorMessage(pgConn->db),
                     WXDB_FIXED_ERROR_SIZE);
        return WXDRC_DB_ERROR;
    }
    rslt = waitResult(pgConn, conn->lastErrorMsg);
    if (rslt == NULL) return WXDRC_DB_ERROR;

    if (PQresultStatus(rslt) == PGRES_COMMAND_OK) {
        PQclear(rslt);
        return WXDRC_OK;
    }
    recordResultError(rslt, conn->lastErrorMsg);
    PQclear(rslt);
    return WXDRC_DB_ERROR;
}

static int WXDBPGSQLTxn_Begin(WXDBConnection *conn) {
    return txnCommand(conn, "BEGIN TRANSACTION");
}

static int WXDBPGSQLTxn_Savepoint(WXDBConnection *conn, const char *name) {
    char cmd[2048];

    (void) snprintf(cmd, sizeof(cmd), "SAVEPOINT %s", name);
    return txnCommand(conn, cmd);
}

static int WXDBPGSQLTxn_Rollback(WXDBConnection *conn, const char *name) {
    char cmd[2048];

    if (name == NULL) return txnCommand(conn, "ROLLBACK TRANSACTION");
    (void) snprintf(cmd, sizeof(cmd), "ROLLBACK TO %s", name);
    return txnCommand(conn, cmd);
}

static int WXDBPGSQLTxn_Commit(WXDBConnection *conn) {
    return txnCommand(conn, "COMMIT TRANSACTION");
}

static int WXDBPGSQLQry_Execute(WXDBConnection *conn, const char *query) {
    WXPGSQLConnection *pgConn = (WXPGSQLConnection *) conn;
    ExecStatusType status;
    PGresult *rslt;
    int rc;

    if ((rc = resetConnResults(conn)) != WXDRC_OK) return rc;

    /* Issue the query and await the (non-tuples) result */
    if (PQsendQuery(pgConn->db, query) != 1) {
        _dbxfStrNCpy(conn->lastErrorMsg, PQerrorMessage(pgConn->db),
                     WXDB_FIXED_ERROR_SIZE);
        return WXDRC_DB_ERROR;
    }
    rslt = waitResult(pgConn, conn->lastErrorMsg);
    if (rslt == NULL) return WXDRC_DB_ERROR;

    if ((status = PQresultStatus(rslt)) == PGRES_COMMAND_OK) {
        captureResultInfo(rslt, &(pgConn->lastRowsModified),
                          &(pgConn->lastRowId));
        PQclear(rslt);
        return WXDRC_OK;
    }

    /* Catch a RS for a command execute or just the error outright */
    if (status == PGRES_TUPLES_OK) {
        (void) strcpy(conn->lastErrorMsg,
                      "Execute called with query returning result set");
    } else {
        recordResultError(rslt, conn->lastErrorMsg);
    }
    PQclear(rslt);
    return WXDRC_DB_ERROR;
}

static WXDBResultSet *WXDBPGSQLQry_ExecuteQuery(WXDBConnection *conn,
                                                const char *query) {
    WXPGSQLConnection *pgConn = (WXPGSQLConnection *) conn;
    ExecStatusType status;
    WXDBResultSet *res;
    PGresult *rslt;

    if (resetConnResults(conn) != WXDRC_OK) return NULL;

    /* Issue the query and await the (tuples) result */
    if (PQsendQuery(pgConn->db, query) != 1) {
        _dbxfStrNCpy(conn->lastErrorMsg, PQerrorMessage(pgConn->db),
                     WXDB_FIXED_ERROR_SIZE);
        return NULL;
    }
    rslt = waitResult(pgConn, conn->lastErrorMsg);
    if (rslt == NULL) return NULL;

    if ((status = PQresultStatus(rslt)) == PGRES_TUPLES_OK) {
        captureResultInfo(rslt, &(pgConn->lastRowsModified),
                          &(pgConn->lastRowId));

        /* Pass the result to the result set instance for return */
        res = createResultSet(conn, NULL, rslt);
        if (res == NULL) {
            _dbxfMemFail(conn->lastErrorMsg);
            PQclear(rslt);
        }
        return res;
    } else if (status == PGRES_COMMAND_OK) {
        (void) strcpy(conn->lastErrorMsg,
                      "ExecuteQuery called with non-result-set query");
    } else {
        recordResultError(rslt, conn->lastErrorMsg);
    }
    PQclear(rslt);
    return NULL;
}

/* Easier and more accurate with the stored result details */
static int64_t WXDBPGSQLQry_RowsModified(WXDBConnection *conn) {
    return ((WXPGSQLConnection *) conn)->lastRowsModified;
}

static uint64_t WXDBPGSQLQry_LastRowId(WXDBConnection *conn) {
    return ((WXPGSQLConnection *) conn)->lastRowId;
}

/***** Statement operations */

/* As per the connection form above, returns a WXDRC_* code */
static int resetStmtResults(WXPGSQLStatement *pstmt) {
    WXPGSQLConnection *pgConn = (WXPGSQLConnection *) pstmt->base.parentConn;

    pstmt->lastRowsModified = -1;
    pstmt->lastRowId = 0;
    *(pstmt->base.lastErrorMsg) = '\0';

    /* Clean up from any earlier connection failures */
    if (pgConn->needsReset) {
        if (!pgsqlResetConn(pgConn, pstmt->base.lastErrorMsg)) {
            return WXDRC_DB_ERROR;
        }
    }

    return WXDRC_OK;
}

static void freeStatement(WXPGSQLStatement *pstmt) {
    if (pstmt->paramValues != NULL) WXFree(pstmt->paramValues);
    if (pstmt->paramLengths != NULL) WXFree(pstmt->paramLengths);
    if (pstmt->paramFormats != NULL) WXFree(pstmt->paramFormats);
    if (pstmt->localParams != NULL) WXFree(pstmt->localParams);
    WXFree(pstmt);
}

static WXDBStatement *WXDBPGSQLStmt_Prepare(WXDBConnection *conn,
                                            const char *stmt) {
    WXPGSQLConnection *pgConn = (WXPGSQLConnection *) conn;
    WXPGSQLStatement *pstmt;
    char *ptr, *str, *fstmt;
    ExecStatusType status;
    PGresult *rslt;
    char ch, qt;
    int cnt;

    /* Allocate the base record up front (working copy) */
    pstmt = (WXPGSQLStatement *) WXCalloc(sizeof(WXPGSQLStatement));
    if (pstmt == NULL) {
        _dbxfMemFail(conn->lastErrorMsg);
        return NULL;
    }

    /* Generate a unique name in the connection for this statement */
    (void) sprintf(pstmt->stmtName, "_pg_%u",
                   (unsigned int) (++pgConn->pstmtCount));
    pstmt->lastRowsModified = -1;
    pstmt->lastRowId = 0;

    /* Convert the '?' delimiter to the $nnn format (estimated) */
    cnt = 0; ptr = (char *) stmt;
    while (*ptr != '\0') if (*(ptr++) == '?') cnt++;

    str = fstmt = (char *) WXMalloc(ptr - stmt + cnt * 3 + 1);
    if (fstmt == NULL) {
        _dbxfMemFail(conn->lastErrorMsg);
        freeStatement(pstmt);
        return NULL;
    }
    qt = '\0';
    cnt = 0; ptr = (char *) stmt;
    while ((ch = *ptr) != '\0') {
        /* Properly handle quoting, no substitution in those situations */
        if (qt != '\0') {
            if (qt == ch) qt = '\0';
            *(str++) = ch; ptr++; continue;
        }

        /* Otherwise, copy and translate markers */
        if ((ch == '\'') || (ch == '"')) {
            *(str++) = qt = ch;
        } else if (ch == '?') {
            *(str++) = '$';
            str += sprintf(str, "%d", (++cnt));
        } else {
            *(str++) = ch;
        }

        ptr++;
    }
    *str = '\0';
    pstmt->paramCount = cnt++;

    /* Allocate for bindings */
    if (((pstmt->paramValues = WXCalloc(cnt * sizeof(char *))) == NULL) ||
            ((pstmt->paramLengths = WXCalloc(cnt * sizeof(int))) == NULL) ||
            ((pstmt->paramFormats = WXCalloc(cnt * sizeof(int))) == NULL) ||
            ((pstmt->localParams =
                       WXCalloc(cnt * sizeof(WXPGSQLLocalParam))) == NULL)) {
        _dbxfMemFail(conn->lastErrorMsg);
        freeStatement(pstmt);
        return NULL;
    }

    /* All set, create the statement */
    if (resetConnResults(conn) != WXDRC_OK) {
        WXFree(fstmt);
        freeStatement(pstmt);
        return NULL;
    }

    if (PQsendPrepare(pgConn->db, pstmt->stmtName, fstmt, 0, NULL) != 1) {
        _dbxfStrNCpy(conn->lastErrorMsg, PQerrorMessage(pgConn->db),
                     WXDB_FIXED_ERROR_SIZE);
        WXFree(fstmt);
        freeStatement(pstmt);
        return NULL;
    }
    rslt = waitResult(pgConn, conn->lastErrorMsg);
    WXFree(fstmt);
    if (rslt == NULL) {
        freeStatement(pstmt);
        return NULL;
    }
    status = PQresultStatus(rslt);

    if ((status == PGRES_EMPTY_QUERY) || (status == PGRES_COMMAND_OK) ||
            (status == PGRES_TUPLES_OK)) {
        PQclear(rslt);
        return (WXDBStatement *) pstmt;
    }
    recordResultError(rslt, conn->lastErrorMsg);
    PQclear(rslt);
    freeStatement(pstmt);
    return NULL;
}

static int WXDBPGSQLStmt_BindInt(WXDBStatement *stmt, int paramIdx,
                                 int val) {
    WXPGSQLStatement *pgStmt = (WXPGSQLStatement *) stmt;
    char *str;

    if ((paramIdx < 0) || (paramIdx >= (int) pgStmt->paramCount)) {
        return recordBindError(stmt, paramIdx, pgStmt->paramCount);
    }
    str = pgStmt->localParams[paramIdx].content;

    pgStmt->paramValues[paramIdx] = str;
    (void) sprintf(str, "%d", val);
    pgStmt->paramLengths[paramIdx] = pgStmt->paramFormats[paramIdx] = 0;

    return WXDRC_OK;
}

static int WXDBPGSQLStmt_BindLong(WXDBStatement *stmt, int paramIdx,
                                  long long val) {
    WXPGSQLStatement *pgStmt = (WXPGSQLStatement *) stmt;
    char *str;

    if ((paramIdx < 0) || (paramIdx >= (int) pgStmt->paramCount)) {
        return recordBindError(stmt, paramIdx, pgStmt->paramCount);
    }
    str = pgStmt->localParams[paramIdx].content;

    pgStmt->paramValues[paramIdx] = str;
    (void) sprintf(str, "%lld", val);
    pgStmt->paramLengths[paramIdx] = pgStmt->paramFormats[paramIdx] = 0;

    return WXDRC_OK;
}

static int WXDBPGSQLStmt_BindDouble(WXDBStatement *stmt, int paramIdx,
                                    double val) {
    WXPGSQLStatement *pgStmt = (WXPGSQLStatement *) stmt;
    char *str;

    if ((paramIdx < 0) || (paramIdx >= (int) pgStmt->paramCount)) {
        return recordBindError(stmt, paramIdx, pgStmt->paramCount);
    }
    str = pgStmt->localParams[paramIdx].content;

    pgStmt->paramValues[paramIdx] = str;
    (void) snprintf(str, sizeof(pgStmt->localParams[paramIdx].content),
                    "%.17g", val);
    pgStmt->paramLengths[paramIdx] = pgStmt->paramFormats[paramIdx] = 0;

    return WXDRC_OK;
}

static int WXDBPGSQLStmt_BindString(WXDBStatement *stmt, int paramIdx,
                                    char * val) {
    WXPGSQLStatement *pgStmt = (WXPGSQLStatement *) stmt;

    if ((paramIdx < 0) || (paramIdx >= (int) pgStmt->paramCount)) {
        return recordBindError(stmt, paramIdx, pgStmt->paramCount);
    }

    pgStmt->paramValues[paramIdx] = val;
    pgStmt->paramLengths[paramIdx] = pgStmt->paramFormats[paramIdx] = 0;

    return WXDRC_OK;
}

static int WXDBPGSQLStmt_Execute(WXDBStatement *stmt) {
    WXPGSQLStatement *pgStmt = (WXPGSQLStatement *) stmt;
    WXPGSQLConnection *pgConn = (WXPGSQLConnection *) pgStmt->base.parentConn;
    ExecStatusType status;
    PGresult *rslt;
    int rc;

    if ((rc = resetStmtResults(pgStmt)) != WXDRC_OK) return rc;

    /* Like connection, issue query and await the (non-tuples) result */
    if (PQsendQueryPrepared(pgConn->db, pgStmt->stmtName, pgStmt->paramCount,
                            (const char **) pgStmt->paramValues,
                            pgStmt->paramLengths,
                            pgStmt->paramFormats, 0) != 1) {
        _dbxfStrNCpy(stmt->lastErrorMsg, PQerrorMessage(pgConn->db),
                     WXDB_FIXED_ERROR_SIZE);
        return WXDRC_DB_ERROR;
    }
    rslt = waitResult(pgConn, stmt->lastErrorMsg);
    if (rslt == NULL) return WXDRC_DB_ERROR;

    if ((status = PQresultStatus(rslt)) == PGRES_COMMAND_OK) {
        captureResultInfo(rslt, &(pgStmt->lastRowsModified),
                          &(pgStmt->lastRowId));
        PQclear(rslt);
        return WXDRC_OK;
    }

    /* Ditto, catch a RS for a command execute or just the error outright */
    if (status == PGRES_TUPLES_OK) {
        (void) strcpy(stmt->lastErrorMsg,
                      "Execute called with query returning result set");
    } else {
        recordResultError(rslt, stmt->lastErrorMsg);
    }
    PQclear(rslt);
    return WXDRC_DB_ERROR;
}

static WXDBResultSet *WXDBPGSQLStmt_ExecuteQuery(WXDBStatement *stmt) {
    WXPGSQLStatement *pgStmt = (WXPGSQLStatement *) stmt;
    WXPGSQLConnection *pgConn = (WXPGSQLConnection *) pgStmt->base.parentConn;
    ExecStatusType status;
    WXDBResultSet *res;
    PGresult *rslt;

    if (resetStmtResults(pgStmt) != WXDRC_OK) return NULL;

    /* Like connection, issue query and await the (tuples) result */
    if (PQsendQueryPrepared(pgConn->db, pgStmt->stmtName, pgStmt->paramCount,
                            (const char **) pgStmt->paramValues,
                            pgStmt->paramLengths,
                            pgStmt->paramFormats, 0) != 1) {
        _dbxfStrNCpy(stmt->lastErrorMsg, PQerrorMessage(pgConn->db),
                     WXDB_FIXED_ERROR_SIZE);
        return NULL;
    }
    rslt = waitResult(pgConn, stmt->lastErrorMsg);
    if (rslt == NULL) return NULL;

    if ((status = PQresultStatus(rslt)) == PGRES_TUPLES_OK) {
        captureResultInfo(rslt, &(pgStmt->lastRowsModified),
                          &(pgStmt->lastRowId));

        /* Pass the result to the result set instance for return */
        res = createResultSet(&(pgConn->base), stmt, rslt);
        if (res == NULL) {
            _dbxfMemFail(stmt->lastErrorMsg);
            PQclear(rslt);
        }
        return res;
    } else if (status == PGRES_COMMAND_OK) {
        (void) strcpy(stmt->lastErrorMsg,
                      "ExecuteQuery called with non-result-set query");
    } else {
        recordResultError(rslt, stmt->lastErrorMsg);
    }
    PQclear(rslt);

    return NULL;
}

static int64_t WXDBPGSQLStmt_RowsModified(WXDBStatement *stmt) {
    return ((WXPGSQLStatement *) stmt)->lastRowsModified;
}

static uint64_t WXDBPGSQLStmt_LastRowId(WXDBStatement *stmt) {
    return ((WXPGSQLStatement *) stmt)->lastRowId;
}

static void WXDBPGSQLStmt_Close(WXDBStatement *stmt) {
    WXPGSQLStatement *pgStmt = (WXPGSQLStatement *) stmt;
    WXPGSQLConnection *pgConn;
    char query[128];

    /* Execute the deallocation and release the memory elements */
    pgConn = (WXPGSQLConnection *) pgStmt->base.parentConn;
    if (!pgConn->needsReset) {
        (void) snprintf(query, sizeof(query), "DEALLOCATE \"%s\";",
                        pgStmt->stmtName);
        if (PQsendQuery(pgConn->db, query) == 1) {
            PQclear(waitResult(pgConn, NULL));
        }
    }

    freeStatement(pgStmt);
}

/***** Result set operations *****/

static uint32_t WXDBPGSQLRsltSet_ColumnCount(WXDBResultSet *rs) {
    /* Use the optimized value */
    return ((WXPGSQLResultSet *) rs)->columnCount;
}

static const char *WXDBPGSQLRsltSet_ColumnName(WXDBResultSet *rs,
                                               uint32_t colIdx) {
    WXPGSQLResultSet *rsltSet = (WXPGSQLResultSet *) rs;

    if (colIdx >= rsltSet->columnCount) return NULL;
    return PQfname(rsltSet->rslt, colIdx);
}

static int WXDBPGSQLRsltSet_ColumnIsNull(WXDBResultSet *rs,
                                         uint32_t colIdx) {
    WXPGSQLResultSet *rsltSet = (WXPGSQLResultSet *) rs;

    if (colIdx >= rsltSet->columnCount) return TRUE;
    return PQgetisnull(rsltSet->rslt, rsltSet->currentRow, colIdx) ?
                                                              TRUE : FALSE;
}

static const char *WXDBPGSQLRsltSet_ColumnData(WXDBResultSet *rs,
                                               uint32_t colIdx) {
    WXPGSQLResultSet *rsltSet = (WXPGSQLResultSet *) rs;

    if (colIdx >= rsltSet->columnCount) return NULL;
    if (PQgetisnull(rsltSet->rslt, rsltSet->currentRow, colIdx)) return NULL;
    return PQgetvalue(rsltSet->rslt, rsltSet->currentRow, colIdx);
}

static int WXDBPGSQLRsltSet_NextRow(WXDBResultSet *rs) {
    WXPGSQLResultSet *rsltSet = (WXPGSQLResultSet *) rs;

    rsltSet->currentRow++;
    return (rsltSet->currentRow < rsltSet->rowCount) ? TRUE : FALSE;
}

static void WXDBPGSQLRsltSet_Close(WXDBResultSet *rs) {
    WXPGSQLResultSet *rsltSet = (WXPGSQLResultSet *) rs;

    PQclear(rsltSet->rslt);
    WXFree(rsltSet);
}

/* Exposed driver implementation for linking */
WXDBDriver _WXDBPGSQLDriver = {
    "pgsql",
    WXDBPGSQLConnection_Create,
    WXDBPGSQLConnection_Destroy,
    WXDBPGSQLConnection_Ping,

    WXDBPGSQLTxn_Begin,
    WXDBPGSQLTxn_Savepoint,
    WXDBPGSQLTxn_Rollback,
    WXDBPGSQLTxn_Commit,

    WXDBPGSQLQry_Execute,
    WXDBPGSQLQry_ExecuteQuery,
    WXDBPGSQLQry_RowsModified,
    WXDBPGSQLQry_LastRowId,

    WXDBPGSQLStmt_Prepare,
    WXDBPGSQLStmt_BindInt,
    WXDBPGSQLStmt_BindLong,
    WXDBPGSQLStmt_BindDouble,
    WXDBPGSQLStmt_BindString,
    WXDBPGSQLStmt_Execute,
    WXDBPGSQLStmt_ExecuteQuery,
    WXDBPGSQLStmt_RowsModified,
    WXDBPGSQLStmt_LastRowId,
    WXDBPGSQLStmt_Close,

    WXDBPGSQLRsltSet_ColumnCount,
    WXDBPGSQLRsltSet_ColumnName,
    WXDBPGSQLRsltSet_ColumnIsNull,
    WXDBPGSQLRsltSet_ColumnData,
    WXDBPGSQLRsltSet_NextRow,
    WXDBPGSQLRsltSet_Close
};
