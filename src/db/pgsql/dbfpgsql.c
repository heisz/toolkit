/*
 * PostgreSQL-specific implementations for the db facade layer.
 *
 * Copyright (C) 2003-2020 J.M. Heisz.  All Rights Reserved.
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
    PGresult *lastConnRslt;
    uint32_t pstmtCount;
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

    /* Last execution result for the prepared statement (exec or query) */
    PGresult *lastRslt;
} WXPGSQLStatement;

typedef struct WXPGSQLResultSet {
    /* This appears first for virtual inheritance */
    WXDBResultSet base;

    /* Associated query result set */
    PGresult *rslt;

    /* Store the column count for optimized error checking */
    uint32_t columnCount, currentRow, rowCount;
} WXPGSQLResultSet;

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

static int WXDBPGSQLConnection_Create(WXDBConnectionPool *pool,
                                      WXDBConnection **connRef) {
    WXHashTable *options = &(pool->options);
    char *opt, paramBuff[2048];
    WXPGSQLConnection *conn;
    WXBuffer params;

    /* Allocate the extended object instance */
    conn = (WXPGSQLConnection *) WXMalloc(sizeof(WXPGSQLConnection));
    if (conn == NULL) {
        _dbxfMemFail(pool->lastErrorMsg);
        return WXDRC_MEM_ERROR;
    }

    /* Build up the connection parameter string */
    WXBuffer_InitLocal(&params, paramBuff, sizeof(paramBuff));
    opt = (char *) WXHash_GetEntry(options, "unix_socket",
                                   WXHash_StrHashFn, WXHash_StrEqualsFn);
    if (opt != NULL) {
        if (!appendParameter(&params, "host", opt)) return WXDRC_MEM_ERROR;
    } else {
        opt = (char *) WXHash_GetEntry(options, "host",
                                       WXHash_StrHashFn, WXHash_StrEqualsFn);
        if (opt != NULL) {
            if (!appendParameter(&params, "host", opt)) return WXDRC_MEM_ERROR;
        }

        opt = (char *) WXHash_GetEntry(options, "port",
                                        WXHash_StrHashFn, WXHash_StrEqualsFn);
        if (opt != NULL) {
            if (!appendParameter(&params, "port", opt)) return WXDRC_MEM_ERROR;
        }
    }

    opt = (char *) WXHash_GetEntry(options, "dbname",
                                   WXHash_StrHashFn, WXHash_StrEqualsFn);
    if (opt != NULL) {
        if (!appendParameter(&params, "dbname", opt)) return WXDRC_MEM_ERROR;
    }

    opt = (char *) WXHash_GetEntry(options, "user",
                                   WXHash_StrHashFn, WXHash_StrEqualsFn);
    if (opt != NULL) {
        if (!appendParameter(&params, "user", opt)) return WXDRC_MEM_ERROR;
    }
    opt = (char *) WXHash_GetEntry(options, "password",
                                   WXHash_StrHashFn, WXHash_StrEqualsFn);
    if (opt != NULL) {
        if (!appendParameter(&params, "password", opt)) return WXDRC_MEM_ERROR;
    }

    /* Reach out and touch someone... */
    conn->db = PQconnectdb((char *) params.buffer);
    conn->lastConnRslt = NULL;
    WXBuffer_Destroy(&params);
    if (PQstatus(conn->db) != CONNECTION_OK) {
        _dbxfStrNCpy(pool->lastErrorMsg, PQerrorMessage(conn->db),
                     WXDB_FIXED_ERROR_SIZE);
        PQfinish(conn->db);
        return WXDRC_DB_ERROR;
    }
    conn->pstmtCount = 0;

    /* All done, connection is ready for use */
    *connRef = &(conn->base);
    return WXDRC_OK;
}

static void WXDBPGSQLConnection_Destroy(WXDBConnection *conn) {
    WXPGSQLConnection *pgConn = (WXPGSQLConnection *) conn;

    /* Close is pretty straightforward */
    if (pgConn->lastConnRslt != NULL) {
        PQclear(pgConn->lastConnRslt);
        pgConn->lastConnRslt = NULL;
    }
    PQfinish(pgConn->db);
}

static int WXDBPGSQLConnection_Ping(WXDBConnection *conn) {
    /* There really isn't a ping, just check current connection status */
    return (PQstatus(((WXPGSQLConnection *) conn)->db) == CONNECTION_OK) ?
                                                                  TRUE : FALSE;
}

/***** Connection query operations *****/

static void resetConnResults(WXDBConnection *conn) {
    WXPGSQLConnection *pgConn = (WXPGSQLConnection *) conn;

    if (pgConn->lastConnRslt != NULL) {
        PQclear(pgConn->lastConnRslt);
        pgConn->lastConnRslt = NULL;
    }
    *(conn->lastErrorMsg) = '\0';
}

static int WXDBPGSQLTxn_Begin(WXDBConnection *conn) {
    PGconn *db = ((WXPGSQLConnection *) conn)->db;
    PGresult *rslt;

    resetConnResults(conn);
    rslt = PQexec(db, "BEGIN TRANSACTION");
    if (PQresultStatus(rslt) == PGRES_COMMAND_OK) {
        PQclear(rslt);
        return WXDRC_OK;
    }
    _dbxfStrNCpy(conn->lastErrorMsg, PQresultErrorMessage(rslt),
                 WXDB_FIXED_ERROR_SIZE);
    PQclear(rslt);
    return WXDRC_DB_ERROR;
} 

static int WXDBPGSQLTxn_Savepoint(WXDBConnection *conn, const char *name) {
    PGconn *db = ((WXPGSQLConnection *) conn)->db;
    char cmd[2048];
    PGresult *rslt;

    resetConnResults(conn);
    (void) sprintf(cmd, "SAVEPOINT %s", name);
    rslt = PQexec(db, cmd);
    if (PQresultStatus(rslt) == PGRES_COMMAND_OK) {
        PQclear(rslt);
        return WXDRC_OK;
    }
    _dbxfStrNCpy(conn->lastErrorMsg, PQresultErrorMessage(rslt),
                 WXDB_FIXED_ERROR_SIZE);
    PQclear(rslt);
    return WXDRC_DB_ERROR;
}

static int WXDBPGSQLTxn_Rollback(WXDBConnection *conn, const char *name) {
    PGconn *db = ((WXPGSQLConnection *) conn)->db;
    char cmd[2048];
    PGresult *rslt;

    resetConnResults(conn);
    if (name == NULL) {
        rslt = PQexec(db, "ROLLBACK TRANSACTION");
    } else {
        (void) sprintf(cmd, "ROLLBACK TO %s", name);
        rslt = PQexec(db, cmd);
    }
    if (PQresultStatus(rslt) == PGRES_COMMAND_OK) {
        PQclear(rslt);
        return WXDRC_OK;
    }
    _dbxfStrNCpy(conn->lastErrorMsg, PQresultErrorMessage(rslt),
                 WXDB_FIXED_ERROR_SIZE);
    PQclear(rslt);
    return WXDRC_DB_ERROR;
}   

static int WXDBPGSQLTxn_Commit(WXDBConnection *conn) {
    PGconn *db = ((WXPGSQLConnection *) conn)->db;
    PGresult *rslt;

    resetConnResults(conn);
    rslt = PQexec(db, "COMMIT TRANSACTION");
    if (PQresultStatus(rslt) == PGRES_COMMAND_OK) {
        PQclear(rslt);
        return WXDRC_OK;
    }
    _dbxfStrNCpy(conn->lastErrorMsg, PQresultErrorMessage(rslt),
                 WXDB_FIXED_ERROR_SIZE);
    PQclear(rslt);
    return WXDRC_DB_ERROR;
}

static int WXDBPGSQLQry_Execute(WXDBConnection *conn, const char *query) {
    WXPGSQLConnection *pgConn = (WXPGSQLConnection *) conn;
    PGresult *rslt;

    resetConnResults(conn);
    rslt = PQexec(pgConn->db, query);
    if (PQresultStatus(rslt) == PGRES_COMMAND_OK) {
        pgConn->lastConnRslt = rslt;
        return WXDRC_OK;
    }
    _dbxfStrNCpy(conn->lastErrorMsg, PQresultErrorMessage(rslt),
                 WXDB_FIXED_ERROR_SIZE);
    PQclear(rslt);
    return WXDRC_DB_ERROR;
}

static WXDBResultSet *WXDBPGSQLQry_ExecuteQuery(WXDBConnection *conn,
                                                const char *query) {
    WXPGSQLConnection *pgConn = (WXPGSQLConnection *) conn;
    ExecStatusType status;
    PGresult *rslt;

    resetConnResults(conn);
    rslt = PQexec(pgConn->db, query);
    if ((status = PQresultStatus(rslt)) == PGRES_TUPLES_OK) {
        pgConn->lastConnRslt = rslt;
        return createResultSet(conn, NULL, rslt);
    } else if (status == PGRES_COMMAND_OK) {
        (void) strcpy(conn->lastErrorMsg,
                      "ExecuteQuery called with non-result-set query");
    } else {
        _dbxfStrNCpy(conn->lastErrorMsg, PQresultErrorMessage(rslt),
                     WXDB_FIXED_ERROR_SIZE);
    }
    PQclear(rslt);
    return NULL;
}

static int64_t WXDBPGSQLQry_RowsModified(WXDBConnection *conn) {
    PGresult *rslt = ((WXPGSQLConnection *) conn)->lastConnRslt;
    const char *cnt;

    if (rslt == NULL) return -1;
    cnt = PQcmdTuples(rslt);
    return (strlen(cnt) == 0) ? -1 : ((int64_t) atoll(PQcmdTuples(rslt)));
}

static uint64_t WXDBPGSQLQry_LastRowId(WXDBConnection *conn) {
    PGresult *rslt = ((WXPGSQLConnection *) conn)->lastConnRslt;

    if (rslt == NULL) return 0;
    return (uint64_t) PQoidValue(rslt);
}

/***** Statement operations */

static void resetStmtResults(WXPGSQLStatement *pstmt) {
    if (pstmt->lastRslt != NULL) {
        PQclear(pstmt->lastRslt);
        pstmt->lastRslt = NULL;
    }
    *(pstmt->base.lastErrorMsg) = '\0';
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

    /* Convert the '?' delimiter to the $nnn format (estimated) */
    cnt = 0; ptr = (char *) stmt;
    while (*ptr != '\0') if (*(ptr++) == '?') cnt++;
    str = fstmt = (char *) WXMalloc(ptr - stmt + cnt * 3);
    if (fstmt == NULL) {
        WXFree(pstmt);
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
    resetConnResults(conn);
    rslt = PQprepare(pgConn->db, pstmt->stmtName, fstmt, 0, NULL);
    status = PQresultStatus(rslt);
    WXFree(fstmt);

    if ((status == PGRES_EMPTY_QUERY) || (status == PGRES_COMMAND_OK) ||
            (status == PGRES_TUPLES_OK)) {
        PQclear(rslt);
        return (WXDBStatement *) pstmt;
    }
    _dbxfStrNCpy(conn->lastErrorMsg, PQresultErrorMessage(rslt),
                 WXDB_FIXED_ERROR_SIZE);
    PQclear(rslt);
    freeStatement(pstmt);
    return NULL;
}

static int WXDBPGSQLStmt_BindInt(WXDBStatement *stmt, int paramIdx,
                                 int val) {
    WXPGSQLStatement *pgStmt = (WXPGSQLStatement *) stmt;
    char *str = pgStmt->localParams[paramIdx].content;

    if ((paramIdx < 0) || (paramIdx >= (int) pgStmt->paramCount)) {
        return WXDRC_SYS_ERROR;
    }

    pgStmt->paramValues[paramIdx] = str;
    (void) sprintf(str, "%d", val);
    pgStmt->paramLengths[paramIdx] = pgStmt->paramFormats[paramIdx] = 0;

    return WXDRC_OK;
}

static int WXDBPGSQLStmt_BindLong(WXDBStatement *stmt, int paramIdx,
                                  long long val) {
    WXPGSQLStatement *pgStmt = (WXPGSQLStatement *) stmt;
    char *str = pgStmt->localParams[paramIdx].content;

    if ((paramIdx < 0) || (paramIdx >= (int) pgStmt->paramCount)) {
        return WXDRC_SYS_ERROR;
    }

    pgStmt->paramValues[paramIdx] = str;
    (void) sprintf(str, "%lld", val);
    pgStmt->paramLengths[paramIdx] = pgStmt->paramFormats[paramIdx] = 0;

    return WXDRC_OK;
}

static int WXDBPGSQLStmt_BindDouble(WXDBStatement *stmt, int paramIdx,
                                    double val) {
    WXPGSQLStatement *pgStmt = (WXPGSQLStatement *) stmt;
    char *str = pgStmt->localParams[paramIdx].content;

    if ((paramIdx < 0) || (paramIdx >= (int) pgStmt->paramCount)) {
        return WXDRC_SYS_ERROR;
    }

    pgStmt->paramValues[paramIdx] = str;
    (void) sprintf(str, "%lf", val);
    pgStmt->paramLengths[paramIdx] = pgStmt->paramFormats[paramIdx] = 0;

    return WXDRC_OK;
}

static int WXDBPGSQLStmt_BindString(WXDBStatement *stmt, int paramIdx,
                                    char * val) {
    WXPGSQLStatement *pgStmt = (WXPGSQLStatement *) stmt;

    if ((paramIdx < 0) || (paramIdx >= (int) pgStmt->paramCount)) {
        return WXDRC_SYS_ERROR;
    }

    pgStmt->paramValues[paramIdx] = val;
    pgStmt->paramLengths[paramIdx] = pgStmt->paramFormats[paramIdx] = 0;

    return WXDRC_OK;
}

static int WXDBPGSQLStmt_Execute(WXDBStatement *stmt) {
    WXPGSQLStatement *pgStmt = (WXPGSQLStatement *) stmt;
    WXPGSQLConnection *pgConn = (WXPGSQLConnection *) pgStmt->base.parentConn;
    PGresult *rslt;

    resetStmtResults(pgStmt);
    rslt = PQexecPrepared(pgConn->db, pgStmt->stmtName, pgStmt->paramCount,
                          (const char **) pgStmt->paramValues,
                          pgStmt->paramLengths, pgStmt->paramFormats, 0);
    if (PQresultStatus(rslt) == PGRES_COMMAND_OK) {
        pgStmt->lastRslt = rslt;
        return WXDRC_OK;
    }
    _dbxfStrNCpy(stmt->lastErrorMsg, PQresultErrorMessage(rslt),
                 WXDB_FIXED_ERROR_SIZE);
    PQclear(rslt);
    return WXDRC_DB_ERROR;
}

static WXDBResultSet *WXDBPGSQLStmt_ExecuteQuery(WXDBStatement *stmt) {
    WXPGSQLStatement *pgStmt = (WXPGSQLStatement *) stmt;
    WXPGSQLConnection *pgConn = (WXPGSQLConnection *) pgStmt->base.parentConn;
    ExecStatusType status;
    PGresult *rslt;

    resetStmtResults(pgStmt);
    rslt = PQexecPrepared(pgConn->db, pgStmt->stmtName, pgStmt->paramCount,
                          (const char **) pgStmt->paramValues,
                          pgStmt->paramLengths, pgStmt->paramFormats, 0);
    if ((status = PQresultStatus(rslt)) == PGRES_TUPLES_OK) {
        pgStmt->lastRslt = rslt;
        return createResultSet(&(pgConn->base), stmt, rslt);
    } else if (status == PGRES_COMMAND_OK) {
        (void) strcpy(stmt->lastErrorMsg,
                      "ExecuteQuery called with non-result-set query");
    } else {
        _dbxfStrNCpy(stmt->lastErrorMsg, PQresultErrorMessage(rslt),
                     WXDB_FIXED_ERROR_SIZE);
    }
    PQclear(rslt);

    return NULL;
}

static int64_t WXDBPGSQLStmt_RowsModified(WXDBStatement *stmt) {
    PGresult *rslt = ((WXPGSQLStatement *) stmt)->lastRslt;
    const char *cnt;

    if (rslt == NULL) return -1;
    cnt = PQcmdTuples(rslt);
    return (strlen(cnt) == 0) ? -1 : ((int64_t) atoll(PQcmdTuples(rslt)));
}

static uint64_t WXDBPGSQLStmt_LastRowId(WXDBStatement *stmt) {
    PGresult *rslt = ((WXPGSQLStatement *) stmt)->lastRslt;

    if (rslt == NULL) return 0;
    return (uint64_t) PQoidValue(rslt);
}

static void WXDBPGSQLStmt_Close(WXDBStatement *stmt) {
    WXPGSQLStatement *pgStmt = (WXPGSQLStatement *) stmt;
    char query[128];

    /* Clean up execution elements */
    if (pgStmt->lastRslt != NULL) {
        PQclear(pgStmt->lastRslt);
        pgStmt->lastRslt = NULL;
    }

    /* Execute the deallocation and release the memory elements */
    (void) sprintf(query, "DEALLOCATE \"%s\";", pgStmt->stmtName);
    PQclear(PQexec(((WXPGSQLConnection *) pgStmt->base.parentConn)->db, query));
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
    WXPGSQLConnection *conn = (WXPGSQLConnection *) rs->parentConn;
    WXPGSQLStatement *stmt = (WXPGSQLStatement *) rs->parentStmt;
    WXPGSQLResultSet *rsltSet = (WXPGSQLResultSet *) rs;

    if ((conn->lastConnRslt != rsltSet->rslt) &&
            ((stmt == NULL) || (stmt->lastRslt != rsltSet->rslt))) {
        /* Result set is disocciated from original query, clear ourselves */
        PQclear(rsltSet->rslt);
    }
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
