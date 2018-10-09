# Toolkit
A collection of utility tooling, libraries and other elements used in various
projects (some of which appear in other associated repositories, just in case
you see commonalities).  Basically a lifetime of bits and pieces of code that
have been released or should have been released into the public domain.  YMMV!

## Code Compilation
In general, the toolkit is just a bunch of random bits of code.  It doesn't
really build into a complete product, so to speak.  But for maintenance/updates
the various parts that can compile, will.

The bulk of the 'compiling' code is written in C and uses the autoconf tooling
on Unix-derived systems.  To build from raw source, run `autoreconf -vfi` to
rebuild the `configure` script and then follow the standard processes for
autoconf-based code compilation.

For the windows side of the world, it is still `make` driven, but in this
instance the environment is a bit more restricted, so autoconf is not needed.
Makefile.win (and its counterparts) are used in this case to compile the code.
Oh - did I mention that it is really intended for cross-compiler environments?

## Intents and Contexts

The remainder of the README (the following sections) attempt to give some
context and descriptions to the origins and intents of the various bits found
in this toolkit.  That may provide clues to an intended or possible use for
the underlying code (or even just an understanding of it...)

### network

Over twenty+ years, I seem to continuously repeat a cycle of application
development patterns.  One stage in the cycle involves creating code related to
network/wire protocol processing (I can write wireshark tracing plugins in my
sleep now).  This directory contains a library of functional enclosures of
common network processing elements (sockets and the like) designed to be
used in cross-platform development environments.  If I've built an application
or a component that utilized low-level networking (and wasn't written in Perl)
then it used some form or components of this library.  Note that this library
isn't really intended to be used verbatim (as a generic library), rather, use
it as a starting point and customize it to your application requirements.

This directory also includes a basic multi-threaded server/engine that can be
used as the basis for a daemon/server application that listens for and processes
requests from clients.  This originated from the HTTP request server that was
built to service operational requests for a distributed agent platform but has
been used for other direct protocol servers (including client emulation).

Note: This library requires elements from the utility library (particularly the
buffer) and (in windows cross-compile) the pthreads-compat library

Note 2: The original forms of this library incorporated a custom built security
layer.  Best practice (now, well, even then) would dictate using audited code
like OpenSSL, HeartBleed bug notwithstanding.  Some remnants might remain...

### utility

This directory contains various 'standard' data structures and algorithms used
throughout the toolkit (or as part of other assemblies).  Probably a lot of
CS### homework assignments might be found in here...
