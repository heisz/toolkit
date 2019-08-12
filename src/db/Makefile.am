#
# Makefile instance for the toolkit libraries.
#
# Copyright (C) 1997-2019 J.M. Heisz.  All Rights Reserved.
# See the LICENSE file accompanying the distribution your rights to use
# this software.
#

# Each vendor-specific implementation is driven by config as a subdirectory
SUBDIRS = @MYSQL_SRCDIR@ @PGSQL_SRCDIR@

# Use the autoconf library constructor for compilation but don't install it
noinst_LTLIBRARIES = libdbxf.la

# Corresponding sources
libdbxf_la_SOURCES = dbxf.c

# Related inclusion locations and other options
AM_CPPFLAGS = -I$(top_srcdir) \
              -I$(top_srcdir)/src/utility

# Removes the automake/autoconf generated files for ultra-clean source tree
MAINTAINERCLEANFILES = Makefile.in
