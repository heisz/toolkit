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
        case WXDB_MAGIC_STMT:
        case WXDB_MAGIC_RSLT:
            break;
    }

    return "Invalid/unrecognized database object instance";
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
}

/* Used more than once!  In-place string lowercase converter */
static void strtolower(char *ptr) {
    while (*ptr != '\0') {
        *ptr = tolower(*ptr);
        ptr++;
    }
}

/* As well as the method that pushes duplicated properties */
static int pushPoolOption(WXDBConnectionPool *pool,
                          const char *srckey, int keylen,
                          const char *srcval, int vallen) {
    char *key, *val, *oldkey, *oldval;

    if (((key = (char *) WXMalloc(keylen + 1)) == NULL) || 
            ((val = (char *) WXMalloc(vallen + 1)) == NULL)) {
        if (key != NULL) WXFree(key);
        return FALSE;
    }
    (void) strncpy(key, srckey, keylen);
    key[keylen] = '\0';
    (void) strncpy(val, srcval, vallen);
    val[vallen] = '\0';
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
                            WXDBConnection **connRef) {
    WXDBConnection *conn, *lastConn;
    int rc;

    /* Allocate and initialize the base connection data instance */
    conn = (WXDBConnection *) WXMalloc(sizeof(WXDBConnection));
    if (conn == NULL) return WXDRC_MEM_ERROR;
    conn->pool = pool;
    conn->driver = pool->driver;
    conn->next = NULL;
    conn->vdata = NULL;

    /* Hand off to the driver instance to actually create the connection */
    rc = (conn->driver->connCreate)(conn);
    if (rc != WXDRC_OK) {
        WXFree(conn);
        return rc;
    }

    /* Got to here, record the connection instance (under lock share) */
    if (pool->connections == NULL) {
        pool->connections = conn;
    } else {
        lastConn = pool->connections;
        while (lastConn->next != NULL) lastConn = lastConn->next;
        lastConn->next = conn;
    }
    if (connRef != NULL) *connRef = conn;

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

    /* Parse the DSN content, starting with the driver */
    ptr = strchr(dsn, ':');
    if (ptr == NULL) {
        (void) strcpy(pool->lastErrorMsg,
                      "Invalid DSN, missing driver separator (:)");
        return WXDRC_SYS_ERROR;
    }
    if ((len = ptr - dsn) > 127) len = 127;
    (void) strncpy(pool->driverName, dsn, len);
    pool->driverName[len] = '\0';
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
        rc = createConnection(pool, NULL);
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
    conn = pool->connections;
    pool->connections = NULL;

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
