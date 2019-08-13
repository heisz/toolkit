/*
 * Implementation of the core elements of the database facade/abstraction.
 *
 * Copyright (C) 1997-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "dbxfint.h"
#include <ctype.h>
#include "mem.h"

/* Share error messaging */
static char *memErrorMsg = "Memory allocation failure";
static char *mtxErrorMsg = "Mutex init/lock error for connection pool";

/* Jiggery-pokery to dynamically linked the vendor driver information */
#ifdef HAVE_MYSQL_DB
extern WXDBDriver _WXDBMYSQLDriver;
#endif
#ifdef HAVE_PGSQL_DB
extern WXDBDriver _WXDBPGSQLDriver;
#endif
static WXDBDriver *drivers[] = {
#ifdef HAVE_MYSQL_DB
    &_WXDBMYSQLDriver,
#endif
#ifdef HAVE_PGSQL_DB
    &_WXDBPGSQLDriver,
#endif
    NULL
};

#define DRIVER_COUNT (sizeof(drivers) / sizeof(void *) - 1)

/**
 * Obtain the last error message related to the provided object.  This can
 * take any of the WXDB data objects, based on the object that returned the
 * error condition.
 *
 * @param obj The WXDB object instance that returned an error condition for
 *            a database operation.
 * @return The last error message from said object.  The return value should
 *         be used immediately (either logged or cloned) as the content could
 *         be transient based on other operations.
 */
const char *WXDB_GetLastErrorMessage(void *obj) {
    /* All objects have a standard format with a magic identifier */
    switch (((WXDBConnectionPool *) obj)->magic) {
        case WXDB_MAGIC_POOL:
            return ((WXDBConnectionPool *) obj)->lastErrorMsg;
        case WXDB_MAGIC_CONN:
            return ((WXDBConnection *) obj)->lastErrorMsg;
        case WXDB_MAGIC_STMT:
        case WXDB_MAGIC_RSLT:
            break;
    }

    return "Invalid/unrecognized database object type/instance";
}

/* Common methods for connection pool cleanup */
static int propFlush(WXHashTable *table, void *key, void *obj, void *userData) {
    WXFree(key); WXFree(obj);
    return FALSE;
}
static void poolFlush(WXDBConnectionPool *pool) {
    /* Note: this only cleans content, not vendor resources */
    (void) WXHash_Scan(&(pool->options), propFlush, NULL);
    WXHash_Destroy(&(pool->options));

    WXThread_MutexDestroy(&(pool->connLock));
}

/* Various string utilities for use in the core and driver instances */
static void strtolower(char *ptr) {
    while (*ptr != '\0') {
        *ptr = tolower(*ptr);
        ptr++;
    }
}
void _dbxfStrNCpy(char *dst, const char *src, int len) {
    char ch;

    while ((--len) > 0) {
        ch = *(dst++) = *(src++);
        if (ch == '\0') return;
    }

    *dst = '\0';
}

/* As well as the method that pushes duplicated properties */
static int pushPoolOption(WXDBConnectionPool *pool,
                          const char *srckey, int keylen,
                          const char *srcval, int vallen) {
    char *key, *val, *oldkey, *oldval;

    /* Watch the preincrements, they allocate for the terminator copy */
    if (((key = (char *) WXMalloc(++keylen)) == NULL) || 
            ((val = (char *) WXMalloc(++vallen)) == NULL)) {
        if (key != NULL) WXFree(key);
        return FALSE;
    }
    _dbxfStrNCpy(key, srckey, keylen);
    _dbxfStrNCpy(val, srcval, vallen);
    strtolower(key);

    if (!WXHash_PutEntry(&(pool->options), key, val,
                         (void **) &oldkey, (void **) &oldval,
                         WXHash_StrHashFn, WXHash_StrEqualsFn)) {
        WXFree(key); WXFree(val);
        return FALSE;
    }

    if (oldkey != NULL) WXFree(oldkey);
    if (oldval != NULL) WXFree(oldval);

    return TRUE;
}

/* Common method to allocate and initialize a connection instance */
static int createConnection(WXDBConnectionPool *pool,
                            WXDBConnection **connRef, int inUse) {
    WXDBConnection *conn, *lastConn;
    int rc;

    /* Allocate and initialize the base connection data instance */
    conn = (WXDBConnection *) WXMalloc(sizeof(WXDBConnection));
    if (conn == NULL) return WXDRC_MEM_ERROR;
    conn->magic = WXDB_MAGIC_CONN;
    conn->pool = pool;
    conn->driver = pool->driver;
    conn->next = NULL;
    conn->inUse = inUse;
    conn->vdata = NULL;
    conn->qdata = NULL;
    *(conn->lastErrorMsg) = '\0';

    /* Hand off to the driver instance to actually create the connection */
    rc = (conn->driver->connCreate)(conn);
    if (rc != WXDRC_OK) {
        WXFree(conn);
        return rc;
    }

    /* Got to here, record the connection instance (under lock share) */
    if (WXThread_MutexLock(&(pool->connLock)) != WXTRC_OK) {
        (void) strcpy(pool->lastErrorMsg, mtxErrorMsg);
        (conn->driver->connDestroy)(conn);
        WXFree(conn);
        return WXDRC_SYS_ERROR;
    }

    if (pool->connections == NULL) {
        pool->connections = conn;
    } else {
        lastConn = pool->connections;
        while (lastConn->next != NULL) lastConn = lastConn->next;
        lastConn->next = conn;
    }
    if (connRef != NULL) *connRef = conn;

    if (WXThread_MutexUnlock(&(pool->connLock)) != WXTRC_OK) {
        (void) strcpy(pool->lastErrorMsg, mtxErrorMsg);
        (conn->driver->connDestroy)(conn);
        WXFree(conn);
        return WXDRC_SYS_ERROR;
    }

    return WXDRC_OK;
}

/**
 * Initialize a connection pool instance for the target data source name.
 *
 * @param pool Reference to the pool instance to be initialized.
 * @param dsn Data source name for the underlying database connections.
 * @param user Username to use for authenticating connections in this pool.
 *             If null, must be provided from the DSN if required by the
 *             database.
 * @param password Password to authenticate with, if unspecified must be
 *                 provided by the DSN if required.
 * @param initialSize Number of initial connections to open, will never be
 *                    less than one to determine DSN validity.
 * @return One of the WXDRC_* result codes, depending on outcome.  Note that
 *         a DB error is from the underlying connection, the pool will be
 *         initialized in case the connection issue is transient.
 */
int WXDBConnectionPool_Init(WXDBConnectionPool *pool, const char *dsn,
                            const char *user, const char *password,
                            uint32_t initialSize) {
    char *ptr, *key, *val, *oldkey, *oldval;
    int rc, idx, len, keylen, vallen;

    /* Basic initialization of the static pool content, for cleanup */
    pool->magic = WXDB_MAGIC_POOL;
    pool->lastErrorMsg[0] = '\0';
    (void) WXHash_InitTable(&(pool->options), 0);
    pool->connections = NULL;
    if (WXThread_MutexInit(&(pool->connLock), FALSE) != WXTRC_OK) {
        (void) strcpy(pool->lastErrorMsg, mtxErrorMsg);
        return WXDRC_SYS_ERROR;
    }

    /* Parse the DSN content, starting with the driver */
    ptr = strchr(dsn, ':');
    if (ptr == NULL) {
        (void) strcpy(pool->lastErrorMsg,
                      "Invalid DSN, missing driver separator (:)");
        WXThread_MutexDestroy(&(pool->connLock));
        return WXDRC_SYS_ERROR;
    }
    if ((len = ptr - dsn) > 127) len = 127;
    _dbxfStrNCpy(pool->driverName, dsn, len + 1);
    strtolower(pool->driverName);
    dsn = ptr + 1;

    /* Right up front, validate that the indicated driver is available */
    for (idx = 0; idx < DRIVER_COUNT; idx++) {
        if (strcmp(drivers[idx]->name, pool->driverName) == 0) {
            pool->driver = drivers[idx];
            break;
        }
    }
    if (pool->driver == NULL) {
        (void) strcpy(pool->lastErrorMsg,
                      "Unrecognized/unsupported driver specified in DSN");
        WXThread_MutexDestroy(&(pool->connLock));
        return WXDRC_SYS_ERROR;
    }

    /* And then the option elements */
    while (*dsn != '\0') {
        /* Watch for empty elements */
        while (*dsn == ';') dsn++;
        if (*dsn == '\0') break;

        /* Parse elements of <key>[=<val>][;] */
        ptr = strchr(dsn, ';');
        len = (ptr == NULL) ? strlen(dsn) : (ptr - dsn);
        ptr = strchr(dsn, '=');
        if (ptr > dsn + len) ptr = NULL;
        keylen = (ptr == NULL) ? len : (ptr - dsn);
        vallen = (ptr == NULL) ? 0 : (len - keylen - 1);

        /* Store into options array */
        if (!pushPoolOption(pool, dsn, keylen,
                            (ptr == NULL) ? dsn : ptr + 1, vallen)) {
            poolFlush(pool);
            (void) strcpy(pool->lastErrorMsg, memErrorMsg);
            return WXDRC_MEM_ERROR;
        }

        dsn += len;
    }

    /* If username/password provided, overload the DSN values (if present) */
    if (user != NULL) {
        if (!pushPoolOption(pool, "user", 4, user, strlen(user))) {
            poolFlush(pool);
            (void) strcpy(pool->lastErrorMsg, memErrorMsg);
            return WXDRC_MEM_ERROR;
        }
    }
    if (password != NULL) {
        if (!pushPoolOption(pool, "password", 8, password, strlen(password))) {
            poolFlush(pool);
            (void) strcpy(pool->lastErrorMsg, memErrorMsg);
            return WXDRC_MEM_ERROR;
        }
    }

    /* And away we go, create the initial connection set (validates login) */
    if (initialSize == 0) initialSize = 1;
    while (initialSize > 0) {
        rc = createConnection(pool, NULL, FALSE);
        if (rc != WXDRC_OK) {
            /* For non-database errors, the pool is toast */
            if (rc != WXDRC_DB_ERROR) {
                if (rc == WXDRC_MEM_ERROR) {
                    (void) strcpy(pool->lastErrorMsg, memErrorMsg);
                }
                poolFlush(pool);
            }

            /* Regardless, any error stops pool initialization */
            return rc;
        }
        initialSize--;
    }

    return WXDRC_OK;
}

/**
 * Obtain a connection instance from the pool, either reusing a previous
 * connection or allocating a new one.  Note that this method is thread-safe.
 *
 * @param pool Reference to the pool to pull a connection from.
 * @return A connection instance or NULL on allocation or connection error.
 */
WXDBConnection *WXDBConnectionPool_Obtain(WXDBConnectionPool *pool) {
    WXDBConnection *conn;

    /* Find an unused connection in the current pool (under lock) */
    if (WXThread_MutexLock(&(pool->connLock)) != WXTRC_OK) {
        (void) strcpy(pool->lastErrorMsg, mtxErrorMsg);
        return NULL;
    }

    /* Let your fingers do the walking... */
    conn = pool->connections;
    while (conn != NULL) {
        if (!conn->inUse) {
            conn->inUse = TRUE;
            break;
        }

        conn = conn->next;
    }

    if (WXThread_MutexUnlock(&(pool->connLock)) != WXTRC_OK) {
        (void) strcpy(pool->lastErrorMsg, mtxErrorMsg);
        return NULL;
    }

    /* And if one isn't found, expand the pool (currently unlimited) */
    if (conn == NULL) {
        if (createConnection(pool, &conn, TRUE) != WXDRC_OK) return NULL;
    }

    return conn;
}

/**
 * Return a connection instance to the pool, allowing it to be recycled for
 * other requests.
 * 
 * @param conn Reference to connection to be returned to the pool.
 */
void WXDBConnectionPool_Return(WXDBConnection *conn) {
    /* Technically, this would be mutexed but a bit is pretty atomic */
    conn->inUse = FALSE;
}

/**
 * Destroy the connection pool instance.  This will close all underlying
 * database connections and release internal resources, but will *not* release
 * the allocated pool structure instance.
 *
 * @param pool Reference to the pool instance to be destroyed (not freed).
 * @return One of the WXDRC_* result codes, depending on outcome.
 */
int WXDBConnectionPool_Destroy(WXDBConnectionPool *pool) {
    WXDBConnection *conn, *next;;

    /* Rip out connections array from pool */
    (void) WXThread_MutexLock(&(pool->connLock));
    conn = pool->connections;
    pool->connections = NULL;
    (void) WXThread_MutexUnlock(&(pool->connLock));

    /* Shutdown all allocated connections where created */
    while (conn != NULL) {
        next = conn->next;
        conn->driver->connDestroy(conn);
        WXFree(conn);
        conn = next;
    }

    /* Use the common method for local cleanup */
    poolFlush(pool);
}

/**
 * Begin a transaction on the associated database connection.
 *
 * @param conn Reference to connection to begin a transaction on.
 * @return One of the WXDRC_* result codes, depending on outcome.
 */
int WXDBConnection_TxnBegin(WXDBConnection *conn) {
    return (conn->driver->txnBegin)(conn);
}

/**
 * Mark a savepoint on the associated database connection.  Must be in a
 * transaction for this to work.
 *
 * @param conn Reference to connection to mark a savepoint for.
 * @param name Name of the savepoint to create, used to match for rollback.
 * @return One of the WXDRC_* result codes, depending on outcome.
 */
int WXDBConnection_TxnSavepoint(WXDBConnection *conn, const char *name) {
    return (conn->driver->txnSavepoint)(conn, name);
}

/**
 * Rollback the current transaction to the indicated savepoint (or entire
 * transaction).
 *
 * @param conn Reference to connection to rollback.
 * @param name Name of the savepoint to roll back to or NULL for entire
 *             transaction.
 * @return One of the WXDRC_* result codes, depending on outcome.
 */
int WXDBConnection_TxnRollback(WXDBConnection *conn, const char *name) {
    return (conn->driver->txnRollback)(conn, name);
}

/**
 * Commit all operations in the current transaction.
 *
 * @param conn Reference to connection to commit.
 * @return One of the WXDRC_* result codes, depending on outcome.
 */
int WXDBConnection_TxnCommit(WXDBConnection *conn) {
    return (conn->driver->txnCommit)(conn);
}

/**
 * Retrieve a count of the number of rows affected by the last execute action
 * in the database.  This should only be used for connection-level execute
 * action, the result for prepared statements is found below.
 *
 * @param conn Reference to the connection that executed an update/insert.
 * @return Count of rows modified by the last query executed on the connection.
 *         Returns -1 or 0 where applicable/possible if no update was executed
 *         (vendor dependent).
 */
int64_t WXDBConnection_RowsModified(WXDBConnection *conn) {
    return (conn->driver->qryRowsModified)(conn);
}

/**
 * Retrieve the row identifier for the record inserted in the last query
 * executed on the connection.  This should only be used for connection-level
 * insert queries, the result for prepared statements is found below.
 *
 * @param conn Reference to the connection that executed an insert.
 * @return Row identifier of the last row inserted by the database, which
 *         is very vendor dependent and complicated by multiple row inserts
 *         or stored procedure instances.  Returns zero (where possible) if
 *         the last statement was not an insert or failed.
 */
uint64_t WXDBConnection_LastRowId(WXDBConnection *conn) {
    return (conn->driver->qryLastRowId)(conn);
}

