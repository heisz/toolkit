/*
 * PostgreSQL-specific implementations for the db facade layer.
 *
 * Copyright (C) 2003-2019 J.M. Heisz.  All Rights Reserved.
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
} WXPGSQLConnection;

typedef struct WXPGSQLStatement {
    /* This appears first for virtual inheritance */
    WXDBStatement base;
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
    PGresult *rslt;

    resetConnResults(conn);
    rslt = PQexec(pgConn->db, query);
    if (PQresultStatus(rslt) == PGRES_TUPLES_OK) {
        pgConn->lastConnRslt = rslt;
        return createResultSet(conn, NULL, rslt);
    } else if (PQresultStatus(rslt) == PGRES_COMMAND_OK) {
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
    WXPGSQLResultSet *rsltSet = (WXPGSQLResultSet *) rs;

    if (conn->lastConnRslt != rsltSet->rslt) PQclear(rsltSet->rslt);
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

    WXDBPGSQLRsltSet_ColumnCount,
    WXDBPGSQLRsltSet_ColumnName,
    WXDBPGSQLRsltSet_ColumnIsNull,
    WXDBPGSQLRsltSet_ColumnData,
    WXDBPGSQLRsltSet_NextRow,
    WXDBPGSQLRsltSet_Close
};
