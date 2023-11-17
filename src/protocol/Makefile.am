#
# Makefile instance for the toolkit libraries.
#
# Copyright (C) 1997-2023 J.M. Heisz.  All Rights Reserved.
# See the LICENSE file accompanying the distribution your rights to use
# this software.
#

# Use the autoconf library constructor for compilation but don't install it
noinst_LTLIBRARIES = libprotocol.la

# Corresponding sources
libprotocol_la_SOURCES = fcgi.c

# Related inclusion locations and other options
AM_CPPFLAGS = -I$(top_srcdir) \
              -I$(top_srcdir)/src/lang \
              -I$(top_srcdir)/src/network \
              -I$(top_srcdir)/src/utility

# Removes the automake/autoconf generated files for ultra-clean source tree
MAINTAINERCLEANFILES = Makefile.in
