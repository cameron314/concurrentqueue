/*
    Copyright 2005-2014 Intel Corporation.  All Rights Reserved.

    This file is part of Threading Building Blocks. Threading Building Blocks is free software;
    you can redistribute it and/or modify it under the terms of the GNU General Public License
    version 2  as  published  by  the  Free Software Foundation.  Threading Building Blocks is
    distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
    implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See  the GNU General Public License for more details.   You should have received a copy of
    the  GNU General Public License along with Threading Building Blocks; if not, write to the
    Free Software Foundation, Inc.,  51 Franklin St,  Fifth Floor,  Boston,  MA 02110-1301 USA

    As a special exception,  you may use this file  as part of a free software library without
    restriction.  Specifically,  if other files instantiate templates  or use macros or inline
    functions from this file, or you compile this file and link it with other files to produce
    an executable,  this file does not by itself cause the resulting executable to be covered
    by the GNU General Public License. This exception does not however invalidate any other
    reasons why the executable file might be covered by the GNU General Public License.
*/

#ifndef _INTERNAL_ITTNOTIFY_H_
#define _INTERNAL_ITTNOTIFY_H_

/**
 * @file
 * @brief Internal User API functions and types
 */

/** @cond exclude_from_documentation */
#ifndef ITT_OS_WIN
#  define ITT_OS_WIN   1
#endif /* ITT_OS_WIN */

#ifndef ITT_OS_LINUX
#  define ITT_OS_LINUX 2
#endif /* ITT_OS_LINUX */

#ifndef ITT_OS_MAC
#  define ITT_OS_MAC   3
#endif /* ITT_OS_MAC */

#ifndef ITT_OS
#  if defined WIN32 || defined _WIN32
#    define ITT_OS ITT_OS_WIN
#  elif defined( __APPLE__ ) && defined( __MACH__ )
#    define ITT_OS ITT_OS_MAC
#  else
#    define ITT_OS ITT_OS_LINUX
#  endif
#endif /* ITT_OS */

#ifndef ITT_PLATFORM_WIN
#  define ITT_PLATFORM_WIN 1
#endif /* ITT_PLATFORM_WIN */

#ifndef ITT_PLATFORM_POSIX
#  define ITT_PLATFORM_POSIX 2
#endif /* ITT_PLATFORM_POSIX */

#ifndef ITT_PLATFORM_MAC
#  define ITT_PLATFORM_MAC 3
#endif /* ITT_PLATFORM_MAC */

#ifndef ITT_PLATFORM
#  if ITT_OS==ITT_OS_WIN
#    define ITT_PLATFORM ITT_PLATFORM_WIN
#  elif ITT_OS==ITT_OS_MAC
#    define ITT_PLATFORM ITT_PLATFORM_MAC
#  else
#    define ITT_PLATFORM ITT_PLATFORM_POSIX
#  endif
#endif /* ITT_PLATFORM */

#if defined(_UNICODE) && !defined(UNICODE)
#define UNICODE
#endif

#include <stddef.h>
#if ITT_PLATFORM==ITT_PLATFORM_WIN
#include <tchar.h>
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#include <stdint.h>
#if defined(UNICODE) || defined(_UNICODE)
#include <wchar.h>
#endif /* UNICODE || _UNICODE */
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

#ifndef CDECL
#  if ITT_PLATFORM==ITT_PLATFORM_WIN
#    define CDECL __cdecl
#  else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#    if defined _M_IX86 || defined __i386__ 
#      define CDECL __attribute__ ((cdecl))
#    else  /* _M_IX86 || __i386__ */
#      define CDECL /* actual only on x86 platform */
#    endif /* _M_IX86 || __i386__ */
#  endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* CDECL */

#ifndef STDCALL
#  if ITT_PLATFORM==ITT_PLATFORM_WIN
#    define STDCALL __stdcall
#  else /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#    if defined _M_IX86 || defined __i386__
#      define STDCALL __attribute__ ((stdcall)) 
#    else  /* _M_IX86 || __i386__ */
#      define STDCALL /* supported only on x86 platform */
#    endif /* _M_IX86 || __i386__ */
#  endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* STDCALL */

#define ITTAPI    CDECL
#define LIBITTAPI CDECL

/* TODO: Temporary for compatibility! */
#define ITTAPI_CALL    CDECL
#define LIBITTAPI_CALL CDECL

#if ITT_PLATFORM==ITT_PLATFORM_WIN
/* use __forceinline (VC++ specific) */
#define ITT_INLINE           __forceinline
#define ITT_INLINE_ATTRIBUTE /* nothing */
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
/*
 * Generally, functions are not inlined unless optimization is specified.
 * For functions declared inline, this attribute inlines the function even
 * if no optimization level was specified.
 */
#ifdef __STRICT_ANSI__
#define ITT_INLINE           static
#else  /* __STRICT_ANSI__ */
#define ITT_INLINE           static inline
#endif /* __STRICT_ANSI__ */
#define ITT_INLINE_ATTRIBUTE __attribute__ ((always_inline, unused))
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
/** @endcond */

/** @cond exclude_from_documentation */
/* Helper macro for joining tokens */
#define ITT_JOIN_AUX(p,n) p##n
#define ITT_JOIN(p,n)     ITT_JOIN_AUX(p,n)

#ifdef ITT_MAJOR
#undef ITT_MAJOR
#endif
#ifdef ITT_MINOR
#undef ITT_MINOR
#endif
#define ITT_MAJOR     3
#define ITT_MINOR     0

/* Standard versioning of a token with major and minor version numbers */
#define ITT_VERSIONIZE(x)    \
    ITT_JOIN(x,              \
    ITT_JOIN(_,              \
    ITT_JOIN(ITT_MAJOR,      \
    ITT_JOIN(_, ITT_MINOR))))

#ifndef INTEL_ITTNOTIFY_PREFIX
#  define INTEL_ITTNOTIFY_PREFIX __itt_
#endif /* INTEL_ITTNOTIFY_PREFIX */
#ifndef INTEL_ITTNOTIFY_POSTFIX
#  define INTEL_ITTNOTIFY_POSTFIX _ptr_
#endif /* INTEL_ITTNOTIFY_POSTFIX */

#define ITTNOTIFY_NAME_AUX(n) ITT_JOIN(INTEL_ITTNOTIFY_PREFIX,n)
#define ITTNOTIFY_NAME(n)     ITT_VERSIONIZE(ITTNOTIFY_NAME_AUX(ITT_JOIN(n,INTEL_ITTNOTIFY_POSTFIX)))

#define ITTNOTIFY_VOID(n) (!ITTNOTIFY_NAME(n)) ? (void)0 : ITTNOTIFY_NAME(n)
#define ITTNOTIFY_DATA(n) (!ITTNOTIFY_NAME(n)) ?       0 : ITTNOTIFY_NAME(n)

#define ITTNOTIFY_VOID_D0(n,d)       (!(d)->flags) ? (void)0 : (!ITTNOTIFY_NAME(n)) ? (void)0 : ITTNOTIFY_NAME(n)(d)
#define ITTNOTIFY_VOID_D1(n,d,x)     (!(d)->flags) ? (void)0 : (!ITTNOTIFY_NAME(n)) ? (void)0 : ITTNOTIFY_NAME(n)(d,x)
#define ITTNOTIFY_VOID_D2(n,d,x,y)   (!(d)->flags) ? (void)0 : (!ITTNOTIFY_NAME(n)) ? (void)0 : ITTNOTIFY_NAME(n)(d,x,y)
#define ITTNOTIFY_VOID_D3(n,d,x,y,z) (!(d)->flags) ? (void)0 : (!ITTNOTIFY_NAME(n)) ? (void)0 : ITTNOTIFY_NAME(n)(d,x,y,z)
#define ITTNOTIFY_VOID_D4(n,d,x,y,z,a)     (!(d)->flags) ? (void)0 : (!ITTNOTIFY_NAME(n)) ? (void)0 : ITTNOTIFY_NAME(n)(d,x,y,z,a)
#define ITTNOTIFY_VOID_D5(n,d,x,y,z,a,b)   (!(d)->flags) ? (void)0 : (!ITTNOTIFY_NAME(n)) ? (void)0 : ITTNOTIFY_NAME(n)(d,x,y,z,a,b)
#define ITTNOTIFY_VOID_D6(n,d,x,y,z,a,b,c) (!(d)->flags) ? (void)0 : (!ITTNOTIFY_NAME(n)) ? (void)0 : ITTNOTIFY_NAME(n)(d,x,y,z,a,b,c)
#define ITTNOTIFY_DATA_D0(n,d)       (!(d)->flags) ?       0 : (!ITTNOTIFY_NAME(n)) ?       0 : ITTNOTIFY_NAME(n)(d)
#define ITTNOTIFY_DATA_D1(n,d,x)     (!(d)->flags) ?       0 : (!ITTNOTIFY_NAME(n)) ?       0 : ITTNOTIFY_NAME(n)(d,x)
#define ITTNOTIFY_DATA_D2(n,d,x,y)   (!(d)->flags) ?       0 : (!ITTNOTIFY_NAME(n)) ?       0 : ITTNOTIFY_NAME(n)(d,x,y)
#define ITTNOTIFY_DATA_D3(n,d,x,y,z) (!(d)->flags) ?       0 : (!ITTNOTIFY_NAME(n)) ?       0 : ITTNOTIFY_NAME(n)(d,x,y,z)
#define ITTNOTIFY_DATA_D4(n,d,x,y,z,a)     (!(d)->flags) ? 0 : (!ITTNOTIFY_NAME(n)) ?       0 : ITTNOTIFY_NAME(n)(d,x,y,z,a)
#define ITTNOTIFY_DATA_D5(n,d,x,y,z,a,b)   (!(d)->flags) ? 0 : (!ITTNOTIFY_NAME(n)) ?       0 : ITTNOTIFY_NAME(n)(d,x,y,z,a,b)
#define ITTNOTIFY_DATA_D6(n,d,x,y,z,a,b,c) (!(d)->flags) ? 0 : (!ITTNOTIFY_NAME(n)) ?       0 : ITTNOTIFY_NAME(n)(d,x,y,z,a,b,c)

#ifdef ITT_STUB
#undef ITT_STUB
#endif
#ifdef ITT_STUBV
#undef ITT_STUBV
#endif
#define ITT_STUBV(api,type,name,args)                             \
    typedef type (api* ITT_JOIN(ITTNOTIFY_NAME(name),_t)) args;   \
    extern ITT_JOIN(ITTNOTIFY_NAME(name),_t) ITTNOTIFY_NAME(name);
#define ITT_STUB ITT_STUBV
/** @endcond */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define INTEL_ITTNOTIFY_API_PRIVATE
#include "../ittnotify.h"

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _INTERNAL_ITTNOTIFY_H_ */
