#
# Makefile instance for the fiber scheduling library.
#
# Copyright (C) 2024-2026 J.M. Heisz.  All Rights Reserved.
# See the LICENSE file accompanying the distribution your rights to use
# this software.
#

# Use the autoconf library constructor for compilation but don't install it
noinst_LTLIBRARIES = libscheduler.la

# Corresponding sources
libscheduler_la_SOURCES = scheduler.c

if ARCH_XA64
    libscheduler_la_SOURCES += gmpctx_xa64.S
endif
if ARCH_ARM64
    libscheduler_la_SOURCES += gmpctx_arm64.S
endif

# Related inclusion locations and other options
AM_CPPFLAGS = -I$(top_srcdir) \
              -I$(top_srcdir)/src/network \
              -I$(top_srcdir)/src/utility

# Removes the automake/autoconf generated files for ultra-clean source tree
MAINTAINERCLEANFILES = Makefile.in
