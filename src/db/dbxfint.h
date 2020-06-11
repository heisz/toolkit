/*
 * Internal definitions for the db facade library - not to be read externally.
 *
 * Copyright (C) 1997-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_DBXFINT_H
#define WX_DBXFINT_H 1

/* Obviously we start with the external definitions */
#include "dbxf.h"

/* Special form of strncpy that truncates with terminator and the malloc err */
void _dbxfStrNCpy(char *dst, const char *src, int len);
void _dbxfMemFail(char *dst);

/* A bunch of magic numbers for the various db data structures */
#define WXDB_MAGIC_POOL 0x6C55BE73
#define WXDB_MAGIC_CONN 0x18099DC0
#define WXDB_MAGIC_STMT 0x6AC23B48
#define WXDB_MAGIC_RSLT 0x3E1ACAB1

/* Complete the opaque data structures */
struct WXDBResultSet {
    /* All structures start with this to support abstract methods */
    uint32_t magic;

    /* Parent origin of the result set, statement and/or connection */
    WXDBConnection *parentConn;
    WXDBStatement *parentStmt;
    WXDBDriver *driver;

    /* Like the others, use a local buffer for rs error messaging */
    char lastErrorMsg[WXDB_FIXED_ERROR_SIZE];
};

struct WXDBStatement {
    /* All structures start with this to support abstract methods */
    uint32_t magic;

    /* Parent connection information for the statement */
    WXDBConnection *parentConn;
    WXDBDriver *driver;

    /* Like the others, use a local buffer for statement error messaging */
    char lastErrorMsg[WXDB_FIXED_ERROR_SIZE];
};

struct WXDBConnection {
    /* All structures start with this to support abstract methods */
    uint32_t magic;

    /* The pool this connection belongs to and the associated driver */
    WXDBConnectionPool *pool;
    WXDBDriver *driver;

    /* Tracking of the pool connections is done through a linked list */
    WXDBConnection *next;

    /* Marker for tracking pool entries that are in use */
    int inUse;

    /* Like the pool, use a local buffer for connection error messaging */
    char lastErrorMsg[WXDB_FIXED_ERROR_SIZE];
};

/* Definition structure for each driver-specific implementation handler */
struct WXDBDriver {
    /* The identifying DSN name for this driver implementation */
    const char *name;

    /* Methods to create, destroy and test (T/F) a connection instance */
    int (*connCreate)(WXDBConnectionPool *pool, WXDBConnection **connRef);
    void (*connDestroy)(WXDBConnection *conn);
    int (*connPing)(WXDBConnection *conn);

    /* Transaction management */
    int (*txnBegin)(WXDBConnection *conn);
    int (*txnSavepoint)(WXDBConnection *conn, const char *name);
    int (*txnRollback)(WXDBConnection *conn, const char *name);
    int (*txnCommit)(WXDBConnection *conn);

    /* Standard connection-level query elements */
    int (*qryExecute)(WXDBConnection *conn, const char *query);
    WXDBResultSet *(*qryExecuteQuery)(WXDBConnection *conn, const char *query);
    int64_t (*qryRowsModified)(WXDBConnection *conn);
    uint64_t (*qryLastRowId)(WXDBConnection *conn);

    /* Prepared statement fun */
    WXDBStatement *(*stmtPrepare)(WXDBConnection *conn, const char *stmt);
    int (*stmtBindInt)(WXDBStatement *stmt, int paramIdx, int val);
    int (*stmtBindLong)(WXDBStatement *stmt, int paramIdx, long long val);
    int (*stmtBindDouble)(WXDBStatement *stmt, int paramIdx, double val);
    int (*stmtBindString)(WXDBStatement *stmt, int paramIdx, char *val);
    int (*stmtExecute)(WXDBStatement *stmt);
    WXDBResultSet *(*stmtExecuteQuery)(WXDBStatement *stmt);
    int64_t (*stmtRowsModified)(WXDBStatement *stmt);
    uint64_t (*stmtLastRowId)(WXDBStatement *stmt);
    void (*stmtClose)(WXDBStatement *stmt);

    /* Result set handling */
    uint32_t (*rsColumnCount)(WXDBResultSet *rs);
    const char *(*rsColumnName)(WXDBResultSet *rs, uint32_t columnIdx);
    int (*rsColumnIsNull)(WXDBResultSet *rs, uint32_t columnIdx);
    const char *(*rsColumnData)(WXDBResultSet *rs, uint32_t columnIdx);
    int (*rsNextRow)(WXDBResultSet *rs);
    void (*rsClose)(WXDBResultSet *rs);
};

#endif
