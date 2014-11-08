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

#include "tbb/tbb_config.h"
#include "tbb/cache_aligned_allocator.h"
#include "tbb/tbb_allocator.h"
#include "tbb/tbb_exception.h"
#include "tbb_misc.h"
#include "dynamic_link.h"
#include <cstdlib>

#if _WIN32||_WIN64
#include "tbb/machine/windows_api.h"
#else
#include <dlfcn.h>
#endif /* _WIN32||_WIN64 */

using namespace std;

#if __TBB_WEAK_SYMBOLS_PRESENT

#pragma weak scalable_malloc
#pragma weak scalable_free
#pragma weak scalable_aligned_malloc
#pragma weak scalable_aligned_free

extern "C" {
    void* scalable_malloc( size_t );
    void  scalable_free( void* );
    void* scalable_aligned_malloc( size_t, size_t );
    void  scalable_aligned_free( void* );
}

#endif /* __TBB_WEAK_SYMBOLS_PRESENT */

namespace tbb {

namespace internal {

//! Dummy routine used for first indirect call via MallocHandler.
static void* DummyMalloc( size_t size );

//! Dummy routine used for first indirect call via FreeHandler.
static void DummyFree( void * ptr );

//! Handler for memory allocation
static void* (*MallocHandler)( size_t size ) = &DummyMalloc;

//! Handler for memory deallocation
static void (*FreeHandler)( void* pointer ) = &DummyFree;

//! Dummy routine used for first indirect call via padded_allocate_handler.
static void* dummy_padded_allocate( size_t bytes, size_t alignment );

//! Dummy routine used for first indirect call via padded_free_handler.
static void dummy_padded_free( void * ptr );

// ! Allocates memory using standard malloc. It is used when scalable_allocator is not available
static void* padded_allocate( size_t bytes, size_t alignment );

// ! Allocates memory using standard free. It is used when scalable_allocator is not available
static void padded_free( void* p );

//! Handler for padded memory allocation
static void* (*padded_allocate_handler)( size_t bytes, size_t alignment ) = &dummy_padded_allocate;

//! Handler for padded memory deallocation
static void (*padded_free_handler)( void* p ) = &dummy_padded_free;

//! Table describing how to link the handlers.
static const dynamic_link_descriptor MallocLinkTable[] = {
    DLD(scalable_malloc, MallocHandler),
    DLD(scalable_free, FreeHandler),
    DLD(scalable_aligned_malloc, padded_allocate_handler),
    DLD(scalable_aligned_free, padded_free_handler),
};


#if TBB_USE_DEBUG
#define DEBUG_SUFFIX "_debug"
#else
#define DEBUG_SUFFIX
#endif /* TBB_USE_DEBUG */

// MALLOCLIB_NAME is the name of the TBB memory allocator library.
#if _WIN32||_WIN64
#define MALLOCLIB_NAME "tbbmalloc" DEBUG_SUFFIX ".dll"
#elif __APPLE__
#define MALLOCLIB_NAME "libtbbmalloc" DEBUG_SUFFIX ".dylib"
#elif __FreeBSD__ || __NetBSD__ || __sun || _AIX || __ANDROID__
#define MALLOCLIB_NAME "libtbbmalloc" DEBUG_SUFFIX ".so"
#elif __linux__  // Note that order of these #elif's is important!
#define MALLOCLIB_NAME "libtbbmalloc" DEBUG_SUFFIX  __TBB_STRING(.so.TBB_COMPATIBLE_INTERFACE_VERSION)
#else
#error Unknown OS
#endif

//! Initialize the allocation/free handler pointers.
/** Caller is responsible for ensuring this routine is called exactly once.
    The routine attempts to dynamically link with the TBB memory allocator.
    If that allocator is not found, it links to malloc and free. */
void initialize_handler_pointers() {
    __TBB_ASSERT( MallocHandler==&DummyMalloc, NULL );
    bool success = dynamic_link( MALLOCLIB_NAME, MallocLinkTable, 4 );
    if( !success ) {
        // If unsuccessful, set the handlers to the default routines.
        // This must be done now, and not before FillDynamicLinks runs, because if other
        // threads call the handlers, we want them to go through the DoOneTimeInitializations logic,
        // which forces them to wait.
        FreeHandler = &free;
        MallocHandler = &malloc;
        padded_allocate_handler = &padded_allocate;
        padded_free_handler = &padded_free;
    }
#if !__TBB_RML_STATIC
    PrintExtraVersionInfo( "ALLOCATOR", success?"scalable_malloc":"malloc" );
#endif
}

static tbb::atomic<do_once_state> initialization_state;
void initialize_cache_aligned_allocator() {
    atomic_do_once( &initialize_handler_pointers, initialization_state );
}

//! Executed on very first call through MallocHandler
static void* DummyMalloc( size_t size ) {
    initialize_cache_aligned_allocator();
    __TBB_ASSERT( MallocHandler!=&DummyMalloc, NULL );
    return (*MallocHandler)( size );
}

//! Executed on very first call through FreeHandler
static void DummyFree( void * ptr ) {
    initialize_cache_aligned_allocator();
    __TBB_ASSERT( FreeHandler!=&DummyFree, NULL );
    (*FreeHandler)( ptr );
}

//! Executed on very first call through padded_allocate_handler
static void* dummy_padded_allocate( size_t bytes, size_t alignment ) {
    initialize_cache_aligned_allocator();
    __TBB_ASSERT( padded_allocate_handler!=&dummy_padded_allocate, NULL );
    return (*padded_allocate_handler)(bytes, alignment);
}

//! Executed on very first call through padded_free_handler
static void dummy_padded_free( void * ptr ) {
    initialize_cache_aligned_allocator();
    __TBB_ASSERT( padded_free_handler!=&dummy_padded_free, NULL );
    (*padded_free_handler)( ptr );
}    

static size_t NFS_LineSize = 128;

size_t NFS_GetLineSize() {
    return NFS_LineSize;
}

#if _MSC_VER && !defined(__INTEL_COMPILER)
    // unary minus operator applied to unsigned type, result still unsigned
    #pragma warning( disable: 4146 4706 )
#endif

void* NFS_Allocate( size_t n, size_t element_size, void* /*hint*/ ) {
    size_t m = NFS_LineSize;
    __TBB_ASSERT( m<=NFS_MaxLineSize, "illegal value for NFS_LineSize" );
    __TBB_ASSERT( (m & (m-1))==0, "must be power of two" );
    size_t bytes = n*element_size;

    if (bytes<n || bytes+m<bytes) {
        // Overflow
        throw_exception(eid_bad_alloc);
    }
    // scalable_aligned_malloc considers zero size request an error, and returns NULL
    if (bytes==0) bytes = 1;
    
    void* result = (*padded_allocate_handler)( bytes, m );
    if (!result)
        throw_exception(eid_bad_alloc);

    __TBB_ASSERT( ((size_t)result&(m-1)) == 0, "The address returned isn't aligned to cache line size" );
    return result;
}

void NFS_Free( void* p ) {
    (*padded_free_handler)( p );
}

static void* padded_allocate( size_t bytes, size_t alignment ) {    
    unsigned char* result = NULL;
    unsigned char* base = (unsigned char*)malloc(alignment+bytes);
    if( base ) {        
        // Round up to the next line
        result = (unsigned char*)((uintptr_t)(base+alignment)&-alignment);
        // Record where block actually starts.
        ((uintptr_t*)result)[-1] = uintptr_t(base);
    }
    return result;    
}

static void padded_free( void* p ) {
    if( p ) {
        __TBB_ASSERT( (uintptr_t)p>=0x4096, "attempt to free block not obtained from cache_aligned_allocator" );
        // Recover where block actually starts
        unsigned char* base = ((unsigned char**)p)[-1];
        __TBB_ASSERT( (void*)((uintptr_t)(base+NFS_LineSize)&-NFS_LineSize)==p, "not allocated by NFS_Allocate?" );
        free(base);
    }
}

void* __TBB_EXPORTED_FUNC allocate_via_handler_v3( size_t n ) {    
    void* result = (*MallocHandler) (n);
    if (!result) {
        throw_exception(eid_bad_alloc);
    }
    return result;
}

void __TBB_EXPORTED_FUNC deallocate_via_handler_v3( void *p ) {
    if( p ) {        
        (*FreeHandler)( p );
    }
}

bool __TBB_EXPORTED_FUNC is_malloc_used_v3() {
    if (MallocHandler == &DummyMalloc) {
        void* void_ptr = (*MallocHandler)(1);
        (*FreeHandler)(void_ptr);
    }
    __TBB_ASSERT( MallocHandler!=&DummyMalloc && FreeHandler!=&DummyFree, NULL );
    // Cast to void avoids type mismatch errors on some compilers (e.g. __IBMCPP__)
    __TBB_ASSERT( !(((void*)MallocHandler==(void*)&malloc) ^ ((void*)FreeHandler==(void*)&free)),
                  "Both shim pointers must refer to routines from the same package (either TBB or CRT)" );
    return (void*)MallocHandler == (void*)&malloc;
}

} // namespace internal

} // namespace tbb
