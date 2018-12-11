/*
 * Core standard inclusions for all C-based toolkit codebase.
 *
 * Copyright (C) 1999-2018 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_STDCONFIG_H
#define WX_STDCONFIG_H 1

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Past experience has shown that pthreads needs to be loaded first (AIX...) */
#include <pthread.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

/* Like to use these everywhere */
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#endif
