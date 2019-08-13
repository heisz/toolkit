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

/**
 * Connection (DSN) Options:
 *     host - the hostname for the database connection (not unix socket)
 *     port - the associated port for the above
 *     unix_socket - the name of the unix socket to connect to (overrides any
 *                   host/port specification)
 *     schema - the name of the initial database (schema) to connect to
 *     charset - the default character set for the connection
 *
 *     <user> - the authentication username (from DSN or options)
 *     <password> - the authentication password (ditto)
 */

/* Connection-level operations */
static int WXDBMYSQLConnection_Create(WXDBConnection *conn) {
    char *host, *sport, *socket, *schema, *user, *password, *opt;
    WXHashTable *options = &(conn->pool->options);
    unsigned long flags = CLIENT_MULTI_STATEMENTS;
    MYSQL *db;
    int port;

    /* Create the database instance up front for configuration */
    db = mysql_init(NULL);
    if (db == NULL) {
        (void) strcpy(conn->pool->lastErrorMsg,
                      "Unable to allocate/create MySQL database connection");
        /* Presume it's a memory issue */
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

    schema = (char *) WXHash_GetEntry(options, "schema",
                                      WXHash_StrHashFn, WXHash_StrEqualsFn);

    user = (char *) WXHash_GetEntry(options, "user",
                                    WXHash_StrHashFn, WXHash_StrEqualsFn);
    if (user == NULL) user = "";
    password = (char *) WXHash_GetEntry(options, "password",
                                        WXHash_StrHashFn, WXHash_StrEqualsFn);

    /* A lot of work to get a the real connection */
    if (mysql_real_connect(db, host, user, password, schema, port,
                           socket, flags) == NULL) {
        (void) strncpy(conn->pool->lastErrorMsg, mysql_error(db),
                       WXDB_FIXED_ERROR_SIZE);
        conn->pool->lastErrorMsg[WXDB_FIXED_ERROR_SIZE - 1] = '\0';
        mysql_close(db);
        return WXDRC_DB_ERROR;
    }

    /* If we get here, congrats!  Store the handle for subsequent requests */
    conn->vdata = db;

    return WXDRC_OK;
}

static void WXDBMYSQLConnection_Destroy(WXDBConnection *conn) {
    /* Close is pretty straightforward */
    mysql_close((MYSQL *) conn->vdata);
}

static int WXDBMYSQLConnection_Ping(WXDBConnection *conn) {
    /* As is ping */
    return (mysql_ping((MYSQL *) conn->vdata) == 0) ? TRUE : FALSE;
}

/****/

static void resetConnResults(WXDBConnection *conn) {
    *(conn->lastErrorMsg) = '\0';
}

static int WXDBMYSQLTxn_Begin(WXDBConnection *conn) {
    MYSQL *db = (MYSQL *) conn->vdata;

    resetConnResults(conn);
    if (mysql_query(db, "START TRANSACTION") == 0) return WXDRC_OK;
    _dbxfStrNCpy(conn->lastErrorMsg, mysql_error(db), WXDB_FIXED_ERROR_SIZE);
    return WXDRC_DB_ERROR;
}

static int WXDBMYSQLTxn_Savepoint(WXDBConnection *conn, const char *name) {
    MYSQL *db = (MYSQL *) conn->vdata;
    char cmd[2048];

    resetConnResults(conn);
    (void) sprintf(cmd, "SAVEPOINT %s", name);
    if (mysql_query(db, cmd) == 0) return WXDRC_OK;
    _dbxfStrNCpy(conn->lastErrorMsg, mysql_error(db), WXDB_FIXED_ERROR_SIZE);
    return WXDRC_DB_ERROR;
}

static int WXDBMYSQLTxn_Rollback(WXDBConnection *conn, const char *name) {
    MYSQL *db = (MYSQL *) conn->vdata;
    char cmd[2048];
    int rc;

    resetConnResults(conn);
    if (name == NULL) {
        rc = mysql_query(db, "ROLLBACK");
    } else {
        (void) sprintf(cmd, "ROLLBACK TO %s", name);
        rc = mysql_query(db, cmd);
    }

    if (rc == 0) return WXDRC_OK;
    _dbxfStrNCpy(conn->lastErrorMsg, mysql_error(db), WXDB_FIXED_ERROR_SIZE);
    return WXDRC_DB_ERROR;
}

static int WXDBMYSQLTxn_Commit(WXDBConnection *conn) {
    MYSQL *db = (MYSQL *) conn->vdata;

    resetConnResults(conn);
    if (mysql_query(db, "COMMIT") == 0) return WXDRC_OK;
    _dbxfStrNCpy(conn->lastErrorMsg, mysql_error(db), WXDB_FIXED_ERROR_SIZE);
    return WXDRC_DB_ERROR;
}

static int64_t WXDBMYSQLQry_RowsModified(WXDBConnection *conn) {
    MYSQL *db = (MYSQL *) conn->vdata;
    uint64_t rows = mysql_affected_rows(db);

    /* In theory, this is broken for reallllllly large datasets */
    return (rows == (uint64_t) -1) ? -1 : (int64_t) rows;
}

static uint64_t WXDBMYSQLQry_LastRowId(WXDBConnection *conn) {
    MYSQL *db = (MYSQL *) conn->vdata;

    /* See the MySQL documentation for lots of comments on the return value */
    return mysql_insert_id(db);
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

    WXDBMYSQLQry_RowsModified,
    WXDBMYSQLQry_LastRowId
};
