#
# Makefile instance for the PostgreSQL-specific db facade elements.
#
# Copyright (C) 1997-2019 J.M. Heisz.  All Rights Reserved.
# See the LICENSE file accompanying the distribution your rights to use
# this software.
#

# Use the autoconf library constructor for compilation but don't install it
noinst_LTLIBRARIES = libdbfpgsql.la

# Corresponding sources
libdbfpgsql_la_SOURCES = dbfpgsql.c

# Related inclusion locations and other options
AM_CPPFLAGS = -I$(top_srcdir) \
              -I$(top_srcdir)/src/utility \
              -I$(top_srcdir)/src/db \
              @PGSQL_INCLUDE@

# Removes the automake/autoconf generated files for ultra-clean source tree
MAINTAINERCLEANFILES = Makefile.in
