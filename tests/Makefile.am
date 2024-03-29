#
# Makefile instance for the toolkit libraries
#
# Copyright (C) 1999-2023 J.M. Heisz.  All Rights Reserved.
# See the LICENSE file accompanying the distribution your rights to use
# this software.
#

# Just a bunch of test programs
bin_PROGRAMS = array buffer db encoding fcgi hash json network \
               thread xml

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

encoding_SOURCES = encoding.c
encoding_LDADD = $(top_builddir)/src/lang/encoding.lo \
                 $(top_builddir)/src/utility/buffer.lo \
                 $(top_builddir)/src/utility/mem.lo \
                 -ldl -lm
encoding_LDFLAGS = -static

fcgi_SOURCES = fcgi.c
fcgi_LDADD = $(top_builddir)/src/protocol/fcgi.lo \
             $(top_builddir)/src/network/event.lo \
             $(top_builddir)/src/network/socket.lo \
             $(top_builddir)/src/utility/buffer.lo \
             $(top_builddir)/src/utility/mem.lo \
             -ldl -lm
fcgi_LDFLAGS = -static

hash_SOURCES = hash.c
hash_LDADD = $(top_builddir)/src/utility/hash.lo \
             $(top_builddir)/src/utility/mem.lo \
             -ldl -lm
hash_LDFLAGS = -static

json_SOURCES = json.c
json_LDADD = $(top_builddir)/src/lang/json.lo \
             $(top_builddir)/src/lang/encoding.lo \
             $(top_builddir)/src/utility/array.lo \
             $(top_builddir)/src/utility/hash.lo \
             $(top_builddir)/src/utility/buffer.lo \
             $(top_builddir)/src/utility/mem.lo \
             -ldl -lm
json_LDFLAGS = -static

network_SOURCES = network.c
network_LDADD = $(top_builddir)/src/network/stream.lo \
                $(top_builddir)/src/network/socket.lo \
                $(top_builddir)/src/utility/buffer.lo \
                $(top_builddir)/src/utility/mem.lo \
                -lssl -lcrypto -ldl -lm
network_LDFLAGS = -static

thread_SOURCES = thread.c
thread_LDADD = $(top_builddir)/src/utility/threadpool.lo \
               $(top_builddir)/src/utility/thread.lo \
               $(top_builddir)/src/utility/log.lo \
               $(top_builddir)/src/utility/mem.lo \
               -ldl -lm
thread_LDFLAGS = -lpthread -static

db_SOURCES = db.c
db_LDADD = $(top_builddir)/src/db/dbxf.lo \
           $(top_builddir)/src/utility/thread.lo \
           $(top_builddir)/src/utility/buffer.lo \
           $(top_builddir)/src/utility/hash.lo \
           $(top_builddir)/src/utility/mem.lo \
           @MYSQL_DRV_OBJ@ @PGSQL_DRV_OBJ@ \
           @MYSQL_LIB@ @PGSQL_LIB@ \
           -lpthread -ldl -lm
db_LDFLAGS = -static

xml_SOURCES = xml.c
xml_LDADD = $(top_builddir)/src/lang/xml.lo \
            $(top_builddir)/src/lang/encoding.lo \
            $(top_builddir)/src/utility/buffer.lo \
            $(top_builddir)/src/utility/mem.lo \
            -ldl -lm
xml_LDFLAGS = -static

http_SOURCES = http.c
http_LDADD = $(top_builddir)/src/protocol/http.lo \
             $(top_builddir)/src/network/event.lo \
             $(top_builddir)/src/network/socket.lo \
             $(top_builddir)/src/network/stream.lo \
             $(top_builddir)/src/utility/thread.lo \
             $(top_builddir)/src/utility/hash.lo \
             $(top_builddir)/src/utility/buffer.lo \
             $(top_builddir)/src/utility/log.lo \
             $(top_builddir)/src/utility/mem.lo \
             -lssl -lcrypto -lpthread -ldl -lm
http_LDFLAGS = -static

bayeux_SOURCES = bayeux.c
bayeux_LDADD = $(top_builddir)/src/protocol/bayeux.lo \
               $(top_builddir)/src/protocol/http.lo \
               $(top_builddir)/src/lang/encoding.lo \
               $(top_builddir)/src/lang/json.lo \
               $(top_builddir)/src/network/socket.lo \
               $(top_builddir)/src/network/stream.lo \
               $(top_builddir)/src/utility/array.lo \
               $(top_builddir)/src/utility/buffer.lo \
               $(top_builddir)/src/utility/hash.lo \
               $(top_builddir)/src/utility/log.lo \
               $(top_builddir)/src/utility/thread.lo \
               $(top_builddir)/src/utility/mem.lo \
               -lpthread -lssl -lcrypto -ldl -lm
bayeux_LDFLAGS = -static

# Related inclusion locations and other options
AM_CPPFLAGS = -I$(top_srcdir) \
              -I$(top_srcdir)/src/db \
              -I$(top_srcdir)/src/lang \
              -I$(top_srcdir)/src/network \
              -I$(top_srcdir)/src/protocol \
              -I$(top_srcdir)/src/utility

# Removes the automake/autoconf generated files for ultra-clean source tree
MAINTAINERCLEANFILES = Makefile.in
