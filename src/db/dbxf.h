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
#include "thread.h"

/* Standardized error codes for database operations */
#define WXDRC_OK 0
#define WXDRC_SYS_ERROR -1
#define WXDRC_MEM_ERROR -2
#define WXDRC_DB_ERROR -3

/* Externally, all of the facade elements are opaque structure references */
typedef struct WXDBResultSet WXDBResultSet;
typedef struct WXDBStatement WXDBStatement;
typedef struct WXDBConnection WXDBConnection;
typedef struct WXDBDriver WXDBDriver;

/* Fixed error buffering size */
#define WXDB_FIXED_ERROR_SIZE 2048

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

    /* Current list of allocated connection instances and mutex for access */
    WXDBConnection *connections;
    WXThread_Mutex connLock;

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
 * Obtain a connection instance from the pool, either reusing a previous
 * connection or allocating a new one.  Note that this method is thread-safe.
 *
 * @param pool Reference to the pool to pull a connection from.
 * @return A connection instance or NULL on allocation or connection error.
 */
WXDBConnection *WXDBConnectionPool_Obtain(WXDBConnectionPool *pool);

/**
 * Return a connection instance to the pool, allowing it to be recycled for
 * other requests.
 *
 * @param conn Reference to connection to be returned to the pool.
 */
void WXDBConnectionPool_Return(WXDBConnection *conn);

/**
 * Destroy the connection pool instance.  This will close all underlying
 * database connections and release internal resources, but will *not* release
 * the allocated pool structure instance.
 *
 * @param pool Reference to the pool instance to be destroyed (not freed).
 * @return One of the WXDRC_* result codes, depending on outcome.
 */
int WXDBConnectionPool_Destroy(WXDBConnectionPool *pool);

/**
 * Begin a transaction on the associated database connection.
 *
 * @param conn Reference to connection to begin a transaction on.
 * @return One of the WXDRC_* result codes, depending on outcome.
 */
void WXDBConnectionPool_Return(WXDBConnection *conn);

/**
 * Begin a transaction on the associated database connection.
 *
 * @param conn Reference to connection to begin a transaction on.
 * @return One of the WXDRC_* result codes, depending on outcome.
 */
int WXDBConnection_TxnBegin(WXDBConnection *conn);

/**
 * Mark a savepoint on the associated database connection.  Must be in a
 * transaction for this to work.
 *
 * @param conn Reference to connection to mark a savepoint for.
 * @param name Name of the savepoint to create, used to match for rollback.
 * @return One of the WXDRC_* result codes, depending on outcome.
 */
int WXDBConnection_TxnSavepoint(WXDBConnection *conn, const char *name);

/**
 * Rollback the current transaction to the indicated savepoint (or entire
 * transaction).
 *
 * @param conn Reference to connection to rollback.
 * @param name Name of the savepoint to roll back to or NULL for entire
 *             transaction.
 * @return One of the WXDRC_* result codes, depending on outcome.
 */
int WXDBConnection_TxnRollback(WXDBConnection *conn, const char *name);

/**
 * Commit all operations in the current transaction.
 *
 * @param conn Reference to connection to commit.
 * @return One of the WXDRC_* result codes, depending on outcome.
 */
int WXDBConnection_TxnCommit(WXDBConnection *conn);

/**
 * Execute a non-prepared statement, typically an insert or update as this
 * method cannot return select data (refer to ExecuteQuery for that method).
 *
 * @param conn Reference to the connection to execute the query against.
 * @param query The database query to be executed.
 * @return One of the WXDRC_* result codes, depending on outcome.
 */
int WXDBConnection_Execute(WXDBConnection *conn, const char *query);

/**
 * Execute a non-prepared statement that returns a result set (select).  Note
 * that the cursor is positioned before the first row.
 *
 * @param conn Reference to the connection to execute the query against.
 * @param query The database query to be executed.
 * @return A result set instance to retrieve data from (use Next() to get the
 *         first row, where applicable) or NULL on a query or memory failure.
 */
WXDBResultSet *WXDBConnection_ExecuteQuery(WXDBConnection *conn,
                                           const char *query);

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
int64_t WXDBConnection_RowsModified(WXDBConnection *conn);

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
uint64_t WXDBConnection_LastRowId(WXDBConnection *conn);

/**
 * Create a prepared statement instance for the given SQL statement (standard
 * execute or result query).
 *
 * @param conn Reference to the connection to prepare the statement against.
 * @param stmt SQL statement to be prepared.  Per the standard, parameter
 *             insertion locations are marked by the '?' character.
 * @return A prepared statement instance or NULL on memory or SQL error
 *         (refer to error buffer in connection).
 */
WXDBStatement *WXDBConnection_Prepare(WXDBConnection *conn, const char *stmt);

/**
 * Execute a prepared statement, typically an insert or update as this
 * method cannot return select data (refer to ExecuteQuery for that method).
 *
 * @param stmt Reference to the prepared statement to be executed.
 * @return One of the WXDRC_* result codes, depending on outcome.
 */
int WXDBStatement_Execute(WXDBStatement *stmt);

/**
 * Execute a prepared statement that returns a result set (select).  Note
 * that the cursor is positioned before the first row.
 *
 * @param stmt Reference to the prepared statement to be executed.
 * @return A result set instance to retrieve data from (use Next() to get the
 *         first row, where applicable) or NULL on a query or memory failure.
 */
WXDBResultSet *WXDBStatement_ExecuteQuery(WXDBStatement *stmt);

/**
 * Retrieve a count of the number of rows affected by the last execute action
 * for the prepared statement.
 *
 * @param stmt Reference to the statement that executed an update/insert.
 * @return Count of rows modified by the last statement execution.  Returns -1
 *         or 0 where applicable/possible if no update was executed (vendor
 *         dependent).
 */
int64_t WXDBStatement_RowsModified(WXDBStatement *stmt);

/**
 * Retrieve the row identifier for the record inserted in the last query
 * executed on the prepared statement.
 *
 * @param conn Reference to the statement that executed an insert.
 * @return Row identifier of the last row inserted by the statement, which
 *         is very vendor dependent and complicated by multiple row inserts
 *         or stored procedure instances.  Returns zero (where possible) if
 *         the last statement was not an insert or failed.
 */
uint64_t WXDBStatement_LastRowId(WXDBStatement *stmt);

/**
 * Retrieve the number of returned columns in the result set.  Used for
 * generic display tooling (query should know column count).
 *
 * @param rs Reference to the result set to retrieve column count for.
 * @return Number of columns in the result set.
 */
uint32_t WXDBResultSet_ColumnCount(WXDBResultSet *rs);

/**
 * Retrieve the name of the indicated column, used for generic display tooling
 * (again, query should know).
 *
 * @param rs Reference to the result set to retrieve the column name for.
 * @param columnIdx Numeric index of the column, starting from zero.
 * @return Name of the column, as an internal reference from the underlying
 *         driver (must be copied to retain or modify).
 */
const char *WXDBResultSet_ColumnName(WXDBResultSet *rs, uint32_t columnIdx);

/**
 * Determine if the database value was NULL for the indicated column (assuming
 * null-ability).
 *
 * @param rs Reference to the result set to retrieve the null state for.
 * @param columnIdx Numeric index of the column, starting from zero.
 * @return TRUE (non-zero) if the column in the current row is NULL,
 *         FALSE (zero) if a value exists in the indicated column.
 */
int WXDBResultSet_ColumnIsNull(WXDBResultSet *rs, uint32_t columnIdx);

/**
 * Retrieve the string conversion of the data for the indicated column.  Note
 * that NULL string conversion is driver dependent, so use the IsNull method
 * instead of looking for NULL values from this method.
 *
 * @param rs Reference to the result set to retrieve the column data for.
 * @param columnIdx Numeric index of the column, starting from zero.
 * @return String representation of the underlying column data for the current
 *         row, as an internal reference from the driver (must be copied to
 *         retain or modify).
 */
const char *WXDBResultSet_ColumnData(WXDBResultSet *rs, uint32_t columnIdx);

/**
 * Advance the result set position to the next row (on creation, the cursor
 * location is before the first row).
 *
 * @param rs Reference to the result set to retrieve the next row for.
 * @return TRUE (non-zero) if the next row was retrieved, FALSE (zero) if 
 *         there were no more rows in the result set.
 */
int WXDBResultSet_NextRow(WXDBResultSet *rs);

/**
 * Release the resources associated to this result set, including the
 * result set instance.
 *
 * @param rs Reference to the result set to close/release.
 */
void WXDBResultSet_Close(WXDBResultSet *rs);

#endif
