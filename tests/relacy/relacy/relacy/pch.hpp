/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_PCH_HPP
#define RL_PCH_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
#   define _CRT_SECURE_NO_WARNINGS 1
#endif

#ifdef _FORTIFY_SOURCE
#    undef _FORTIFY_SOURCE
#endif

#ifndef _XOPEN_SOURCE
#    define _XOPEN_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>
#include <memory.h>
#include <string.h>

#include <typeinfo>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <iterator>
#include <memory>
#include <vector>
#include <queue>
#include <string>
#include <stack>
#include <set>
#include <map>
#include <new>

#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
#   define RL_WIN
#endif

#if defined(RL_WIN) || defined(_CYGWIN)
#   ifndef _WIN32_WINNT
#       define _WIN32_WINNT 0x0500
#   endif
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <process.h>
#   ifdef RL_WIN
#       include <intrin.h>
#   else
#       include <stdint.h>
#       include <sys/times.h>
#   endif
#else
#   include <stdint.h>
#   include <sys/times.h>
#   include <unistd.h>
#   include <ucontext.h>
#   include <setjmp.h>
#endif

#endif
