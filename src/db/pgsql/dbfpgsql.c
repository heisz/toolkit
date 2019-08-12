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
 *     schema - the name of the initial database (schema) to connect to
 *     charset - the default character set for the connection
 *
 *     <user> - the authentication username (from DSN or options)
 *     <password> - the authentication password (ditto)
 */

/* Connection-level operations */
static int WXDBPGSQLConnection_Create(WXDBConnection *conn) {
    WXHashTable *options = &(conn->pool->options);
    char *opt, paramBuff[2048];
    WXBuffer params;
    PGconn *db;

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

    opt = (char *) WXHash_GetEntry(options, "schema",
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
    db = PQconnectdb((char *) params.buffer);
    WXBuffer_Destroy(&params);
    if (PQstatus(db) != CONNECTION_OK) {
        (void) strncpy(conn->pool->lastErrorMsg, PQerrorMessage(db),
                       WXDB_FIXED_ERROR_SIZE);
        conn->pool->lastErrorMsg[WXDB_FIXED_ERROR_SIZE - 1] = '\0';
        PQfinish(db);
        return WXDRC_DB_ERROR;
    }

    /* If we get here, congrats!  Store the handle for subsequent requests */
    conn->vdata = db;

    return WXDRC_OK;
}

static void WXDBPGSQLConnection_Destroy(WXDBConnection *conn) {
    /* Close is pretty straightforward */
    PQfinish((PGconn *) conn->vdata);
}

static int WXDBPGSQLConnection_Ping(WXDBConnection *conn) {
    /* There really isn't a ping, just check current connection status */
    return (PQstatus((PGconn *) conn->vdata) == CONNECTION_OK) ? TRUE : FALSE;
}

/* Exposed driver implementation for linking */
WXDBDriver _WXDBPGSQLDriver = {
    "pgsql",
    WXDBPGSQLConnection_Create,
    WXDBPGSQLConnection_Destroy,
    WXDBPGSQLConnection_Ping
};
