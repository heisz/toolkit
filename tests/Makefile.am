#
# Makefile instance for the toolkit libraries
#
# Copyright (C) 1999-2019 J.M. Heisz.  All Rights Reserved.
# See the LICENSE file accompanying the distribution your rights to use
# this software.
#

# Just a bunch of test programs
bin_PROGRAMS = array buffer hash

# Lots of test cases
array_SOURCES = array.c
array_LDADD = $(top_builddir)/src/utility/array.lo \
              $(top_builddir)/src/utility/mem.lo \
              -ldl -lm
array_LDFLAGS = -static

buffer_SOURCES = buffer.c
buffer_LDADD = $(top_builddir)/src/utility/buffer.lo \
               $(top_builddir)/src/utility/mem.lo \
               -ldl -lm
buffer_LDFLAGS = -static

hash_SOURCES = hash.c
hash_LDADD = $(top_builddir)/src/utility/hash.lo \
             $(top_builddir)/src/utility/mem.lo \
             -ldl -lm
hash_LDFLAGS = -static

# Related inclusion locations and other options
AM_CPPFLAGS = -I$(top_srcdir) \
              -I$(top_srcdir)/src/network \
              -I$(top_srcdir)/src/utility

# Removes the automake/autoconf generated files for ultra-clean source tree
MAINTAINERCLEANFILES = Makefile.in
