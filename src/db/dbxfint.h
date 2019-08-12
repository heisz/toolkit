/*
 * Internal definitions for the db facade library - not to be read externally.
 *
 * Copyright (C) 1997-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_DBXFINT_H
#define WX_DBXFINT_H 1

/* Obviously we start with the external definitions */
#include "dbxf.h"

/* A bunch of magic numbers for the various db data structures */
#define WXDB_MAGIC_POOL 0x6C55BE73
#define WXDB_MAGIC_CONN 0x18099DC0
#define WXDB_MAGIC_STMT 0x6AC23B48
#define WXDB_MAGIC_RSLT 0x3E1ACAB1

/* Complete the opaque data structures */
struct WXDBResultSet {
    /* All structures start with this to support abstract methods */
    uint32_t magic;

    /* Vendor-specific data elements for the result set instance */
    void *vdata;
};

struct WXDBStatement {
    /* All structures start with this to support abstract methods */
    uint32_t magic;

    /* Vendor-specific data elements for the statement instance */
    void *vdata;
};

struct WXDBConnection {
    /* All structures start with this to support abstract methods */
    uint32_t magic;

    /* The pool this connection belongs to and the associated driver */
    WXDBConnectionPool *pool;
    WXDBDriver *driver;

    /* Tracking of the pool connections is done through a linked list */
    WXDBConnection *next;

    /* Vendor/driver-specific data elements for the connection instance */
    void *vdata;
};

/* Definition structure for each driver-specific implementation handler */
struct WXDBDriver {
    /* The identifying DSN name for this driver implementation */
    const char *name;

    /* Methods to create, destroy and test (T/F) a connection instance */
    int (*connCreate)(WXDBConnection *conn);
    void (*connDestroy)(WXDBConnection *conn);
    int (*connPing)(WXDBConnection *conn);
};

#endif