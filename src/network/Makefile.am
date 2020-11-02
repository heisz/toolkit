#
# Makefile instance for the toolkit libraries.
#
# Copyright (C) 1997-2020 J.M. Heisz.  All Rights Reserved.
# See the LICENSE file accompanying the distribution your rights to use
# this software.
#

# Use the autoconf library constructor for compilation but don't install it
noinst_LTLIBRARIES = libnetwork.la

# Corresponding sources
libnetwork_la_SOURCES = event.c socket.c stream.c

# Related inclusion locations and other options
AM_CPPFLAGS = -I$(top_srcdir) \
              -I$(top_srcdir)/src/utility

# Removes the automake/autoconf generated files for ultra-clean source tree
MAINTAINERCLEANFILES = Makefile.in
