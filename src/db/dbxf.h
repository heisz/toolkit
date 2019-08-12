/*
 * Definitions for the top-level database facade instance.
 *
 * Copyright (C) 1997-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_DBXF_H
#define WX_DBXF_H 1

/* Grab the standard definitions */
#include "stdconfig.h"
#include "hash.h"

/* Standardized error codes for database operations */
#define WXDRC_OK 0
#define WXDRC_SYS_ERROR -1
#define WXDRC_MEM_ERROR -2
#define WXDRC_DB_ERROR -3

/* Fixed error buffering */
#define WXDB_FIXED_ERROR_SIZE 2048

/* Externally, all of the facade elements are opaque structure references */
typedef struct WXDBResultSet WXDBResultSet;
typedef struct WXDBStatement WXDBStatement;
typedef struct WXDBConnection WXDBConnection;
typedef struct WXDBDriver WXDBDriver;

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
const char *WXDB_GetLastErrorMessage(void *obj);

/**
 * Definition of the connection pool management structure.  Like all of the
 * other elements, this should be treated as an opaque object, although the
 * static elements/options are safe for external access and in this specific
 * case it is exposed to support external memory allocation.
 */
typedef struct WXDBConnectionPool {
    /* All structures start with this to support abstract methods */
    uint32_t magic;

    /* Parsed details for the DSN, including the driver reference */
    char driverName[128];
    WXHashTable options;
    WXDBDriver *driver;

    /* Current list of allocated connection instances */
    WXDBConnection *connections;

    /* Storage element for pool-level error conditions (alloc safe) */
    char lastErrorMsg[WXDB_FIXED_ERROR_SIZE];
} WXDBConnectionPool;

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
                            uint32_t initialSize);

/**
 * Destroy the connection pool instance.  This will close all underlying
 * database connections and release internal resources, but will *not* release
 * the allocated pool structure instance.
 *
 * @param pool Reference to the pool instance to be destroyed (not freed).
 * @return One of the WXDRC_* result codes, depending on outcome.
 */
int WXDBConnectionPool_Destroy(WXDBConnectionPool *pool);

#endif
