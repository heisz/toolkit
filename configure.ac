#
# Autoconf macro definitions for the generics toolkits.
#
# Copyright (C) 1999-2026 J.M. Heisz.  All Rights Reserved.
# See the LICENSE file accompanying the distribution your rights to use
# this software.
#

####### Part A - Standard Autoconf Header Elements #######

AC_REVISION(0.1.0)dnl

# Standard initialization sequence defines package details
AC_INIT([toolkit], [0.1.0])
# Secret location for the standard configuration scripts
AC_CONFIG_AUX_DIR(configure-scripts)
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_MSG_RESULT([Configuring Toolkit -Version 0.1.0])
AM_CONFIG_HEADER(config.h)
AC_CANONICAL_HOST

# Uses libtool notation for sub-library compilation
AM_PROG_AR
AM_PROG_AS
LT_INIT

# Choose a working default prefix
dtag=`date "+%y%m%d%H%M%S"`
ac_default_prefix="/tmp/wrdg-tk-${dtag}"
year=`date "+%G"`


####### Part B - Build/Compilation/Release Tool Determination #######

# Standard subdirectory make and compilation tests
AC_PROG_MAKE_SET
AC_PROG_CC

####### Part C - Target and Build Type Determination #######

configDate=`date "+%y%m%d %T"`
AC_DEFINE_UNQUOTED([CONFIGUREDATE], ["$configDate"],
                   [Configuration/build date for version display])

# Define current year for copyright purposes
AC_DEFINE_UNQUOTED([CURRENT_YEAR], ["$year"],
                   [Current year used for copyright])

# Support build labelling
AC_ARG_WITH(label,
[  --with-label            provide an explicit label for this build ],
[
    AC_DEFINE_UNQUOTED([BUILDLABEL], ["Label: \"${withval}\"\\n"],
                       [Explicit label associated with the build])
],
[
    AC_DEFINE_UNQUOTED([BUILDLABEL], [""],
                       [Empty placeholder for label associated with the build])
])

# Handle architecture specific compilation for assembly elements
case "$host_cpu" in
     x86_64* | amd64*)
        AM_CONDITIONAL([ARCH_XA64], [true])
        AM_CONDITIONAL([ARCH_ARM64], [false])
        ;;
    aarch64* | arm64*)
        AM_CONDITIONAL([ARCH_XA64], [false])
        AM_CONDITIONAL([ARCH_ARM64], [true])
        ;;
    *)
        AC_MSG_ERROR([Unsupported architecture: $host_cpu])
        ;;
esac

####### Part D - Dependent Product/Library Determination #######

# Determine if MySQL is available and provide associated definitions
AC_CHECK_PROGS(MYSQL_CONFIG, mysql_config)
if test -n "${MYSQL_CONFIG}"; then
    AC_DEFINE([HAVE_MYSQL_DB], [1],
              [Define to 1 if you have build support for MySQL client])
    MYSQL_SRCDIR="mysql"
    MYSQL_INCLUDE=`${MYSQL_CONFIG} --include`
    MYSQL_LIB=`${MYSQL_CONFIG} --libs`
    MYSQL_DRV_OBJ="\$(top_builddir)/src/db/mysql/dbfmysql.o"
    AC_SUBST(MYSQL_SRCDIR)
    AC_SUBST(MYSQL_INCLUDE)
    AC_SUBST(MYSQL_LIB)
    AC_SUBST(MYSQL_DRV_OBJ)
fi

# Determine if PostgreSQL is available and provide associated definitions
AC_CHECK_PROGS(PGSQL_CONFIG, pg_config)
if test -n "${MYSQL_CONFIG}"; then
    AC_DEFINE([HAVE_PGSQL_DB], [1],
              [Define to 1 if you have build support for PostgreSQL client])
    PGSQL_SRCDIR="pgsql"
    PGSQL_INCLUDE_DIR=`${PGSQL_CONFIG} --includedir`
    PGSQL_INCLUDE="-I${PGSQL_INCLUDE_DIR}"
    PGSQL_LIB_DIR=`${PGSQL_CONFIG} --libdir`
    PGSQL_LIB="-L${PGSQL_LIB_DIR} -lpq"
    PGSQL_DRV_OBJ="\$(top_builddir)/src/db/pgsql/dbfpgsql.o"
    AC_SUBST(PGSQL_SRCDIR)
    AC_SUBST(PGSQL_INCLUDE)
    AC_SUBST(PGSQL_LIB)
    AC_SUBST(PGSQL_DRV_OBJ)
fi

####### Part E - System-Specific Headers/Libraries/Options #######

# Build config.h for header availability determination
AC_CHECK_HEADERS([fcntl.h])
AC_CHECK_HEADERS([sys/time.h])
AC_CHECK_HEADERS([arpa/inet.h])
AC_CHECK_HEADERS([netinet/in.h])
AC_CHECK_HEADERS([endian.h])
AC_CHECK_HEADERS([sys/endian.h])
AC_CHECK_HEADERS([byteswap.h])
AC_CHECK_HEADERS([poll.h])
AC_CHECK_HEADERS([sys/epoll.h])
AC_CHECK_HEADERS([valgrind/valgrind.h])

####### All Done - Generate Configuration Specific Files #######

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 src/db/Makefile
                 src/db/mysql/Makefile
                 src/db/pgsql/Makefile
                 src/lang/Makefile
                 src/network/Makefile
                 src/protocol/Makefile
                 src/scheduler/Makefile
                 src/utility/Makefile
                 tests/Makefile])
AC_OUTPUT
