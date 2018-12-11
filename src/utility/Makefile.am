#
# Makefile instance for the toolkit libraries.
#
# Copyright (C) 1999-2018 J.M. Heisz.  All Rights Reserved.
# See the LICENSE file accompanying the distribution your rights to use
# this software.
#

# Use the autoconf library constructor for compilation but don't install it
noinst_LTLIBRARIES = libutility.la

# Corresponding sources
libutility_la_SOURCES = buffer.c mem.c

# Related inclusion locations and other options
AM_CPPFLAGS = -I$(top_srcdir)

# Removes the automake/autoconf generated files for ultra-clean source tree
MAINTAINERCLEANFILES = Makefile.in
