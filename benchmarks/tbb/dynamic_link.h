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

#ifndef __TBB_dynamic_link
#define __TBB_dynamic_link

// Support for dynamic loading entry points from other shared libraries.

#include "tbb/tbb_stddef.h"

#ifdef LIBRARY_ASSERT
    #undef __TBB_ASSERT
    #define __TBB_ASSERT(x,y) LIBRARY_ASSERT(x,y)
#else
    #define LIBRARY_ASSERT(x,y) __TBB_ASSERT_EX(x,y)
#endif /* !LIBRARY_ASSERT */

/** By default, symbols declared and defined here go into namespace tbb::internal.
    To put them in other namespace, define macros OPEN_INTERNAL_NAMESPACE
    and CLOSE_INTERNAL_NAMESPACE to override the following default definitions. **/
#ifndef OPEN_INTERNAL_NAMESPACE
#define OPEN_INTERNAL_NAMESPACE namespace tbb { namespace internal {
#define CLOSE_INTERNAL_NAMESPACE }}
#endif /* OPEN_INTERNAL_NAMESPACE */

#include <stddef.h>
#if _WIN32
#include "tbb/machine/windows_api.h"
#endif /* _WIN32 */

OPEN_INTERNAL_NAMESPACE

//! Type definition for a pointer to a void somefunc(void)
typedef void (*pointer_to_handler)();

//! The helper to construct dynamic_link_descriptor structure
// Double cast through the void* in DLD macro is necessary to
// prevent warnings from some compilers (g++ 4.1)
#if __TBB_WEAK_SYMBOLS_PRESENT
#define DLD(s,h) {#s, (pointer_to_handler*)(void*)(&h), (pointer_to_handler)&s}
#else 
#define DLD(s,h) {#s, (pointer_to_handler*)(void*)(&h)}
#endif /* __TBB_WEAK_SYMBOLS_PRESENT */
//! Association between a handler name and location of pointer to it.
struct dynamic_link_descriptor {
    //! Name of the handler
    const char* name;
    //! Pointer to the handler
    pointer_to_handler* handler;
#if __TBB_WEAK_SYMBOLS_PRESENT
    //! Weak symbol
    pointer_to_handler ptr;
#endif
};

#if _WIN32
typedef HMODULE dynamic_link_handle;
#else
typedef void* dynamic_link_handle;
#endif /* _WIN32 */

const int DYNAMIC_LINK_GLOBAL = 0x01;
const int DYNAMIC_LINK_LOAD   = 0x02;
const int DYNAMIC_LINK_WEAK   = 0x04;
const int DYNAMIC_LINK_ALL    = DYNAMIC_LINK_GLOBAL | DYNAMIC_LINK_LOAD | DYNAMIC_LINK_WEAK;

//! Fill in dynamically linked handlers.
/** 'library' is the name of the requested library. It should not contain a full
    path since dynamic_link adds the full path (from which the runtime itself
    was loaded) to the library name.
    'required' is the number of the initial entries in the array descriptors[]
    that have to be found in order for the call to succeed. If the library and
    all the required handlers are found, then the corresponding handler
    pointers are set, and the return value is true.  Otherwise the original
    array of descriptors is left untouched and the return value is false.
    'required' is limited by 20 (exceeding of this value will result in failure
    to load the symbols and the return value will be false).
    'handle' is the handle of the library if it is loaded. Otherwise it is left
    untouched.
    'flags' is the set of DYNAMIC_LINK_* flags. Each of the DYNAMIC_LINK_* flags
    allows its corresponding linking stage.
**/
bool dynamic_link( const char* library,
                   const dynamic_link_descriptor descriptors[],
                   size_t required,
                   dynamic_link_handle* handle = 0,
                   int flags = DYNAMIC_LINK_ALL );

void dynamic_unlink( dynamic_link_handle handle );

void dynamic_unlink_all();

enum dynamic_link_error_t {
    dl_success = 0,
    dl_lib_not_found,     // char const * lib, dlerr_t err
    dl_sym_not_found,     // char const * sym, dlerr_t err
                          // Note: dlerr_t depends on OS: it is char const * on Linux* and OS X*, int on Windows*.
    dl_sys_fail,          // char const * func, int err
    dl_buff_too_small     // none
}; // dynamic_link_error_t

CLOSE_INTERNAL_NAMESPACE

#endif /* __TBB_dynamic_link */
