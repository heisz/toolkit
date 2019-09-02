/*
 * MySQL-specific implementations for the db facade layer.
 *
 * Copyright (C) 1999-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "dbxfint.h"
#include <mysql.h>
#include "hash.h"
#include "mem.h"

/* This just makes my style of return comparison more readable */
#define WXDB_MYSQL_OK 0

/* MySQL specific database elements */
typedef struct WXMYSQLConnection {
    /* This appears first for virtual inheritance */
    WXDBConnection base;

    /* MySQL-specific elements follow */
    MYSQL *db;
} WXMYSQLConnection;

typedef struct WXMYSQLStatement {
    /* This appears first for virtual inheritance */
    WXDBStatement base;
} WXMYSQLStatement;

/* To support the content model, need to bind a column structure for content */
typedef struct WXMYSQLColumn {
    /* Field information for this column */
    MYSQL_FIELD *fieldInfo;

    /* Storage elements for column data return (buffer is in bind structure) */
    unsigned long dataLength;
    my_bool isNull;
} WXMYSQLColumn;

typedef struct WXMYSQLResultSet {
    /* This appears first for virtual inheritance */
    WXDBResultSet base;

    /* Associated statement information */
    MYSQL_STMT *stmt;

    /* Metadata and binding elements for the fieldset */
    uint32_t columnCount;
    MYSQL_RES *metadata;
    MYSQL_BIND *bindInfo;
    WXMYSQLColumn *columnInfo;
} WXMYSQLResultSet;

/* Forward declaration for error cleanup */
static void WXDBMYSQLRsltSet_Close(WXDBResultSet *rs);

/* Initial buffer size, set to tiny to test reallocation requirements */
#define BUFFER_INIT_SIZE 2

/* Common method for result set creation from statement execution */
static WXDBResultSet *createResultSet(WXDBConnection *conn,
                                      WXDBStatement *pstmt,
                                      MYSQL_STMT *stmt) {
    WXMYSQLResultSet *res;
    WXMYSQLColumn *col;
    MYSQL_BIND *bind;
    uint32_t idx;

    /* Allocate up front */
    res = (WXMYSQLResultSet *) WXMalloc(sizeof(WXMYSQLResultSet));
    if (res == NULL) {
        /* Statement has been handed off, release if transient */
        if (pstmt == NULL) mysql_stmt_close(stmt);
        return NULL;
    }
    res->base.parentConn = conn;
    res->base.parentStmt = pstmt;
    res->base.driver = (conn != NULL) ? conn->driver : pstmt->driver;
    res->stmt = stmt;

    /* After this point, enough is defined that close() can be used on error */

    /* Validate result set and prepare column binding information */
    res->columnCount = mysql_stmt_field_count(stmt);
    if ((res->metadata = mysql_stmt_result_metadata(stmt)) == NULL) {
        WXDBMYSQLRsltSet_Close((WXDBResultSet *) res);
        return NULL;
    }
    if ((res->bindInfo = (MYSQL_BIND *)
                  WXCalloc(res->columnCount * sizeof(MYSQL_BIND))) == NULL) {
        WXDBMYSQLRsltSet_Close((WXDBResultSet *) res);
        return NULL;
    }
    if ((res->columnInfo = (WXMYSQLColumn *)
                  WXCalloc(res->columnCount * sizeof(WXMYSQLColumn))) == NULL) {
        WXDBMYSQLRsltSet_Close((WXDBResultSet *) res);
        return NULL;
    }

    /* Bind away! */
    for (idx = 0, col = res->columnInfo, bind = res->bindInfo;
                               idx < res->columnCount; col++, bind++, idx++) {
        col->fieldInfo = mysql_fetch_field_direct(res->metadata, idx);

        if ((bind->buffer = WXMalloc(BUFFER_INIT_SIZE + 1)) == NULL) {
            WXDBMYSQLRsltSet_Close((WXDBResultSet *) res);
            return NULL;
        }
        bind->buffer_type = MYSQL_TYPE_STRING;
        bind->buffer_length = BUFFER_INIT_SIZE;
        bind->is_null = &(col->isNull);
        bind->length = &(col->dataLength);
    }

    if (mysql_stmt_bind_result(stmt, res->bindInfo) != WXDB_MYSQL_OK) {
        if (pstmt != NULL) {
            _dbxfStrNCpy(pstmt->lastErrorMsg, mysql_stmt_error(stmt),
                         WXDB_FIXED_ERROR_SIZE);
        } else {
            _dbxfStrNCpy(conn->lastErrorMsg, mysql_stmt_error(stmt),
                         WXDB_FIXED_ERROR_SIZE);
        }
        WXDBMYSQLRsltSet_Close((WXDBResultSet *) res);
        return NULL;
    }

    return (WXDBResultSet *) res;
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

static int WXDBMYSQLConnection_Create(WXDBConnectionPool *pool,
                                      WXDBConnection **connRef) {
    char *host, *sport, *socket, *dbname, *user, *password, *opt;
    unsigned long flags = CLIENT_MULTI_STATEMENTS;
    WXHashTable *options = &(pool->options);
    WXMYSQLConnection *conn;
    int port;

    /* Allocate the extended object instance */
    conn = (WXMYSQLConnection *) WXMalloc(sizeof(WXMYSQLConnection));
    if (conn == NULL) {
        _dbxfMemFail(pool->lastErrorMsg); 
        return WXDRC_MEM_ERROR;
    }

    /* Create the database instance up front for configuration */
    conn->db = mysql_init(NULL);
    if (conn->db == NULL) {
        (void) strcpy(pool->lastErrorMsg,
                      "Unable to allocate/create MySQL database connection");
        /* Presume it's a memory issue */
        WXFree(conn);
        return WXDRC_MEM_ERROR;
    }

    /* Grab the various elements for the real-connect call */
    socket = (char *) WXHash_GetEntry(options, "unix_socket",
                                      WXHash_StrHashFn, WXHash_StrEqualsFn);
    if (socket != NULL) {
        host = "localhost";
        port = 0;
    } else {
        host = (char *) WXHash_GetEntry(options, "host",
                                        WXHash_StrHashFn, WXHash_StrEqualsFn);
        if (host == NULL) host = "localhost";

        sport = (char *) WXHash_GetEntry(options, "port",
                                         WXHash_StrHashFn, WXHash_StrEqualsFn);
        if (sport == NULL) sport = "3306";
        port = atoi(sport);
        if (port <= 0) port = 3306;
    }

    dbname = (char *) WXHash_GetEntry(options, "dbname",
                                      WXHash_StrHashFn, WXHash_StrEqualsFn);

    user = (char *) WXHash_GetEntry(options, "user",
                                    WXHash_StrHashFn, WXHash_StrEqualsFn);
    if (user == NULL) user = "";
    password = (char *) WXHash_GetEntry(options, "password",
                                        WXHash_StrHashFn, WXHash_StrEqualsFn);

    /* A lot of work to get a the real connection */
    if (mysql_real_connect(conn->db, host, user, password, dbname, port,
                           socket, flags) == NULL) {
        _dbxfStrNCpy(pool->lastErrorMsg, mysql_error(conn->db),
                     WXDB_FIXED_ERROR_SIZE);
        mysql_close(conn->db);
        WXFree(conn);
        return WXDRC_DB_ERROR;
    }

    /* All done, connection is ready for use */
    *connRef = &(conn->base);
    return WXDRC_OK;
}

static void WXDBMYSQLConnection_Destroy(WXDBConnection *conn) {
    /* Close is pretty straightforward */
    mysql_close(((WXMYSQLConnection *) conn)->db);
}

static int WXDBMYSQLConnection_Ping(WXDBConnection *conn) {
    /* As is ping */
    MYSQL *db = ((WXMYSQLConnection *) conn)->db;
    return (mysql_ping(db) == WXDB_MYSQL_OK) ? TRUE : FALSE;
}

/***** Connection query operations *****/

static void resetConnResults(WXDBConnection *conn) {
    *(conn->lastErrorMsg) = '\0';
}

static int WXDBMYSQLTxn_Begin(WXDBConnection *conn) {
    MYSQL *db = ((WXMYSQLConnection *) conn)->db;

    resetConnResults(conn);
    if (mysql_query(db, "START TRANSACTION") == WXDB_MYSQL_OK) return WXDRC_OK;
    _dbxfStrNCpy(conn->lastErrorMsg, mysql_error(db), WXDB_FIXED_ERROR_SIZE);
    return WXDRC_DB_ERROR;
}

static int WXDBMYSQLTxn_Savepoint(WXDBConnection *conn, const char *name) {
    MYSQL *db = ((WXMYSQLConnection *) conn)->db;
    char cmd[2048];

    resetConnResults(conn);
    (void) sprintf(cmd, "SAVEPOINT %s", name);
    if (mysql_query(db, cmd) == WXDB_MYSQL_OK) return WXDRC_OK;
    _dbxfStrNCpy(conn->lastErrorMsg, mysql_error(db), WXDB_FIXED_ERROR_SIZE);
    return WXDRC_DB_ERROR;
}

static int WXDBMYSQLTxn_Rollback(WXDBConnection *conn, const char *name) {
    MYSQL *db = ((WXMYSQLConnection *) conn)->db;
    char cmd[2048];
    int rc;

    resetConnResults(conn);
    if (name == NULL) {
        rc = mysql_query(db, "ROLLBACK");
    } else {
        (void) sprintf(cmd, "ROLLBACK TO %s", name);
        rc = mysql_query(db, cmd);
    }

    if (rc == WXDB_MYSQL_OK) return WXDRC_OK;
    _dbxfStrNCpy(conn->lastErrorMsg, mysql_error(db), WXDB_FIXED_ERROR_SIZE);
    return WXDRC_DB_ERROR;
}

static int WXDBMYSQLTxn_Commit(WXDBConnection *conn) {
    MYSQL *db = ((WXMYSQLConnection *) conn)->db;

    resetConnResults(conn);
    if (mysql_query(db, "COMMIT") == WXDB_MYSQL_OK) return WXDRC_OK;
    _dbxfStrNCpy(conn->lastErrorMsg, mysql_error(db), WXDB_FIXED_ERROR_SIZE);
    return WXDRC_DB_ERROR;
}

static int WXDBMYSQLQry_Execute(WXDBConnection *conn, const char *query) {
    MYSQL *db = ((WXMYSQLConnection *) conn)->db;

    resetConnResults(conn);
    if (mysql_query(db, query) == WXDB_MYSQL_OK) return WXDRC_OK;
    _dbxfStrNCpy(conn->lastErrorMsg, mysql_error(db), WXDB_FIXED_ERROR_SIZE);
    return WXDRC_DB_ERROR;
}

static WXDBResultSet *WXDBMYSQLQry_ExecuteQuery(WXDBConnection *conn,
                                                const char *query) {
    MYSQL *db = ((WXMYSQLConnection *) conn)->db;
    unsigned long cursorType = CURSOR_TYPE_READ_ONLY;
    MYSQL_STMT *stmt;

    /* Note: could just use mysql_query here and then mysql_store_result,
     *       but then the result set implementation would need two processing
     *       forms.  Just use a statement and be done with it...
     */
    resetConnResults(conn);
    if ((stmt = mysql_stmt_init(db)) == NULL) {
        _dbxfMemFail(conn->lastErrorMsg); 
        return NULL;
    }
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != WXDB_MYSQL_OK) {
        _dbxfStrNCpy(conn->lastErrorMsg, mysql_stmt_error(stmt),
                     WXDB_FIXED_ERROR_SIZE);
        mysql_stmt_close(stmt);
        return NULL;
    }

    /* Ensure this only executes a query (no error check) */
    (void) mysql_stmt_attr_set(stmt, STMT_ATTR_CURSOR_TYPE, &cursorType);

    /* And execute */
    if (mysql_stmt_execute(stmt) != WXDB_MYSQL_OK) {
        _dbxfStrNCpy(conn->lastErrorMsg, mysql_stmt_error(stmt),
                     WXDB_FIXED_ERROR_SIZE);
        mysql_stmt_close(stmt);
        return NULL;
    }

    return createResultSet(conn, NULL, stmt);
}

static int64_t WXDBMYSQLQry_RowsModified(WXDBConnection *conn) {
    MYSQL *db = ((WXMYSQLConnection *) conn)->db;
    uint64_t rows = mysql_affected_rows(db);

    /* In theory, this is broken for reallllllly large datasets */
    return (rows == (uint64_t) -1) ? -1 : (int64_t) rows;
}

static uint64_t WXDBMYSQLQry_LastRowId(WXDBConnection *conn) {
    MYSQL *db = ((WXMYSQLConnection *) conn)->db;

    /* See the MySQL documentation for lots of comments on the return value */
    return mysql_insert_id(db);
}

/***** Result set operations *****/

static uint32_t WXDBMYSQLRsltSet_ColumnCount(WXDBResultSet *rs) {
    /* Already have from the binding operation */
    return ((WXMYSQLResultSet *) rs)->columnCount;
}

static const char *WXDBMYSQLRsltSet_ColumnName(WXDBResultSet *rs,
                                               uint32_t colIdx) {
    WXMYSQLResultSet *rsltSet = (WXMYSQLResultSet *) rs;

    if (colIdx >= rsltSet->columnCount) return NULL;
    return rsltSet->columnInfo[colIdx].fieldInfo->name;
}

static int WXDBMYSQLRsltSet_ColumnIsNull(WXDBResultSet *rs, 
                                         uint32_t colIdx) {
    WXMYSQLResultSet *rsltSet = (WXMYSQLResultSet *) rs;

    if (colIdx >= rsltSet->columnCount) return TRUE;
    return (rsltSet->columnInfo[colIdx].isNull) ? TRUE : FALSE;
}

static const char *WXDBMYSQLRsltSet_ColumnData(WXDBResultSet *rs, 
                                               uint32_t colIdx) {
    WXMYSQLResultSet *rsltSet = (WXMYSQLResultSet *) rs;
    char *buffer;

    if (colIdx >= rsltSet->columnCount) return NULL;
    if (rsltSet->columnInfo[colIdx].isNull) return NULL;

    (buffer = rsltSet->bindInfo[colIdx].buffer)
                     [rsltSet->columnInfo[colIdx].dataLength] = '\0';
    return buffer;
}

static int WXDBMYSQLRsltSet_NextRow(WXDBResultSet *rs) {
    WXMYSQLResultSet *rsltSet = (WXMYSQLResultSet *) rs;
    unsigned long len;
    MYSQL_BIND *bind;
    uint8_t *buffer;
    uint32_t idx;
    int rc;

    rc = mysql_stmt_fetch(rsltSet->stmt);
    if (rc == MYSQL_DATA_TRUNCATED) {
        /* Adjust appropriately and grab the column data */
        for (idx = 0, bind = rsltSet->bindInfo;
                            idx < rsltSet->columnCount; idx++, bind++) {
            if ((len = rsltSet->columnInfo[idx].dataLength) <=
                                             bind->buffer_length) continue;
            if ((buffer = WXRealloc(bind->buffer, len + 1)) == NULL) {
                // TODO - error?
                return FALSE;
            }
            bind->buffer = buffer;
            bind->buffer_length = len;
            if (mysql_stmt_fetch_column(rsltSet->stmt, bind,
                                        idx, 0) != WXDB_MYSQL_OK) {
                // TODO - error?
                return FALSE;
            }
        }

        /* Update the bind instances (not sure how to handle error) */
        (void) mysql_stmt_bind_result(rsltSet->stmt, rsltSet->bindInfo);
        return TRUE;
    }
    return (rc == WXDB_MYSQL_OK) ? TRUE : FALSE;
}

static void WXDBMYSQLRsltSet_Close(WXDBResultSet *rs) {
    WXMYSQLResultSet *rsltSet = (WXMYSQLResultSet *) rs;
    uint32_t idx;

    if (rsltSet->bindInfo != NULL) {
        for (idx = 0; idx < rsltSet->columnCount; idx++) {
            if (rsltSet->bindInfo[idx].buffer != NULL)
                                WXFree(rsltSet->bindInfo[idx].buffer);
        }
        WXFree(rsltSet->bindInfo);
    }
    if (rsltSet->columnInfo != NULL) {
        WXFree(rsltSet->columnInfo);
    }

    mysql_stmt_free_result(rsltSet->stmt);
    if (rsltSet->metadata != NULL) {
        mysql_free_result(rsltSet->metadata);
    }
    if (rsltSet->base.parentStmt == NULL) mysql_stmt_close(rsltSet->stmt);
    WXFree(rs);
}

/* Exposed driver implementation for linking */
WXDBDriver _WXDBMYSQLDriver = {
    "mysql",
    WXDBMYSQLConnection_Create,
    WXDBMYSQLConnection_Destroy,
    WXDBMYSQLConnection_Ping,

    WXDBMYSQLTxn_Begin,
    WXDBMYSQLTxn_Savepoint,
    WXDBMYSQLTxn_Rollback,
    WXDBMYSQLTxn_Commit,

    WXDBMYSQLQry_Execute,
    WXDBMYSQLQry_ExecuteQuery,
    WXDBMYSQLQry_RowsModified,
    WXDBMYSQLQry_LastRowId,

    WXDBMYSQLRsltSet_ColumnCount,
    WXDBMYSQLRsltSet_ColumnName,
    WXDBMYSQLRsltSet_ColumnIsNull,
    WXDBMYSQLRsltSet_ColumnData,
    WXDBMYSQLRsltSet_NextRow,
    WXDBMYSQLRsltSet_Close
};
