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
#include "tbb_main.h"
#include "governor.h"
#include "market.h"
#include "tbb_misc.h"
#include "itt_notify.h"

namespace tbb {
namespace internal {

//------------------------------------------------------------------------
// Begin shared data layout.
// The following global data items are mostly read-only after initialization.
//------------------------------------------------------------------------

//! Padding in order to prevent false sharing.
static const char _pad[NFS_MaxLineSize - sizeof(int)] = {};

//------------------------------------------------------------------------
// governor data
basic_tls<generic_scheduler*> governor::theTLS;
unsigned governor::DefaultNumberOfThreads;
rml::tbb_factory governor::theRMLServerFactory;
bool governor::UsePrivateRML;
const task_scheduler_init *governor::BlockingTSI;
#if TBB_USE_ASSERT
bool governor::IsBlockingTerminationInProgress;
#endif
bool governor::is_speculation_enabled;

//------------------------------------------------------------------------
// market data
market* market::theMarket;
market::global_market_mutex_type market::theMarketMutex;

//------------------------------------------------------------------------
// One time initialization data

//! Counter of references to global shared resources such as TLS.
atomic<int> __TBB_InitOnce::count;

__TBB_atomic_flag __TBB_InitOnce::InitializationLock;

//! Flag that is set to true after one-time initializations are done.
bool __TBB_InitOnce::InitializationDone;

#if DO_ITT_NOTIFY
    static bool ITT_Present;
    static bool ITT_InitializationDone;
#endif

#if !(_WIN32||_WIN64) || __TBB_SOURCE_DIRECTLY_INCLUDED
    static __TBB_InitOnce __TBB_InitOnceHiddenInstance;
#endif

//------------------------------------------------------------------------
// generic_scheduler data

//! Pointer to the scheduler factory function
generic_scheduler* (*AllocateSchedulerPtr)( arena*, size_t index );

#if __TBB_OLD_PRIMES_RNG
//! Table of primes used by fast random-number generator (FastRandom).
/** Also serves to keep anything else from being placed in the same
    cache line as the global data items preceding it. */
static const unsigned Primes[] = {
    0x9e3779b1, 0xffe6cc59, 0x2109f6dd, 0x43977ab5,
    0xba5703f5, 0xb495a877, 0xe1626741, 0x79695e6b,
    0xbc98c09f, 0xd5bee2b3, 0x287488f9, 0x3af18231,
    0x9677cd4d, 0xbe3a6929, 0xadc6a877, 0xdcf0674b,
    0xbe4d6fe9, 0x5f15e201, 0x99afc3fd, 0xf3f16801,
    0xe222cfff, 0x24ba5fdb, 0x0620452d, 0x79f149e3,
    0xc8b93f49, 0x972702cd, 0xb07dd827, 0x6c97d5ed,
    0x085a3d61, 0x46eb5ea7, 0x3d9910ed, 0x2e687b5b,
    0x29609227, 0x6eb081f1, 0x0954c4e1, 0x9d114db9,
    0x542acfa9, 0xb3e6bd7b, 0x0742d917, 0xe9f3ffa7,
    0x54581edb, 0xf2480f45, 0x0bb9288f, 0xef1affc7,
    0x85fa0ca7, 0x3ccc14db, 0xe6baf34b, 0x343377f7,
    0x5ca19031, 0xe6d9293b, 0xf0a9f391, 0x5d2e980b,
    0xfc411073, 0xc3749363, 0xb892d829, 0x3549366b,
    0x629750ad, 0xb98294e5, 0x892d9483, 0xc235baf3,
    0x3d2402a3, 0x6bdef3c9, 0xbec333cd, 0x40c9520f
};

//------------------------------------------------------------------------
// End of shared data layout
//------------------------------------------------------------------------

//------------------------------------------------------------------------
// Shared data accessors
//------------------------------------------------------------------------

unsigned GetPrime ( unsigned seed ) {
    return Primes[seed%(sizeof(Primes)/sizeof(Primes[0]))];
}
#endif //__TBB_OLD_PRIMES_RNG

//------------------------------------------------------------------------
// __TBB_InitOnce
//------------------------------------------------------------------------

void __TBB_InitOnce::add_ref() {
    if( ++count==1 )
        governor::acquire_resources();
}

void __TBB_InitOnce::remove_ref() {
    int k = --count;
    __TBB_ASSERT(k>=0,"removed __TBB_InitOnce ref that was not added?"); 
    if( k==0 ) {
        governor::release_resources();
        ITT_FINI_ITTLIB();
    }
}

//------------------------------------------------------------------------
// One-time Initializations
//------------------------------------------------------------------------

//! Defined in cache_aligned_allocator.cpp
void initialize_cache_aligned_allocator();

//! Defined in scheduler.cpp
void Scheduler_OneTimeInitialization ( bool itt_present );

#if DO_ITT_NOTIFY

#if __TBB_ITT_STRUCTURE_API

static __itt_domain *fgt_domain = NULL;

struct resource_string {
    const char *str;
    __itt_string_handle *itt_str_handle;
};

//
// populate resource strings
//
#define TBB_STRING_RESOURCE( index_name, str ) { str, NULL },
static resource_string strings_for_itt[] = {
    #include "tbb/internal/_tbb_strings.h"
    { "num_resource_strings", NULL } 
};
#undef TBB_STRING_RESOURCE

static __itt_string_handle *ITT_get_string_handle(int idx) {
    __TBB_ASSERT(idx >= 0, NULL);
    return idx < NUM_STRINGS ? strings_for_itt[idx].itt_str_handle : NULL;
}

static void ITT_init_domains() {
    fgt_domain = __itt_domain_create( _T("tbb.flow") );
    fgt_domain->flags = 1;
}

static void ITT_init_strings() {
    for ( int i = 0; i < NUM_STRINGS; ++i ) {
#if _WIN32||_WIN64
        strings_for_itt[i].itt_str_handle = __itt_string_handle_createA( strings_for_itt[i].str );
#else
        strings_for_itt[i].itt_str_handle = __itt_string_handle_create( strings_for_itt[i].str );
#endif
    }
}

static void ITT_init() {
    ITT_init_domains();
    ITT_init_strings();
}

#endif // __TBB_ITT_STRUCTURE_API

/** Thread-unsafe lazy one-time initialization of tools interop.
    Used by both dummy handlers and general TBB one-time initialization routine. **/
void ITT_DoUnsafeOneTimeInitialization () {
    if ( !ITT_InitializationDone ) {
        ITT_Present = (__TBB_load_ittnotify()!=0);
#if __TBB_ITT_STRUCTURE_API
        if (ITT_Present) ITT_init();
#endif
        ITT_InitializationDone = true;
        ITT_SYNC_CREATE(&market::theMarketMutex, SyncType_GlobalLock, SyncObj_SchedulerInitialization);
    }
}

/** Thread-safe lazy one-time initialization of tools interop.
    Used by dummy handlers only. **/
extern "C"
void ITT_DoOneTimeInitialization() {
    __TBB_InitOnce::lock();
    ITT_DoUnsafeOneTimeInitialization();
    __TBB_InitOnce::unlock();
}
#endif /* DO_ITT_NOTIFY */

//! Performs thread-safe lazy one-time general TBB initialization.
void DoOneTimeInitializations() {
    suppress_unused_warning(_pad);
    __TBB_InitOnce::lock();
    // No fence required for load of InitializationDone, because we are inside a critical section.
    if( !__TBB_InitOnce::InitializationDone ) {
        __TBB_InitOnce::add_ref();
        if( GetBoolEnvironmentVariable("TBB_VERSION") )
            PrintVersion();
        bool itt_present = false;
#if DO_ITT_NOTIFY
        ITT_DoUnsafeOneTimeInitialization();
        itt_present = ITT_Present;
#endif /* DO_ITT_NOTIFY */
        initialize_cache_aligned_allocator();
        governor::initialize_rml_factory();
        Scheduler_OneTimeInitialization( itt_present );
        // Force processor groups support detection
        governor::default_num_threads();
        // Dump version data
        governor::print_version_info();
        PrintExtraVersionInfo( "Tools support", itt_present ? "enabled" : "disabled" );
        __TBB_InitOnce::InitializationDone = true;
    }
    __TBB_InitOnce::unlock();
}

#if (_WIN32||_WIN64) && !__TBB_SOURCE_DIRECTLY_INCLUDED
//! Windows "DllMain" that handles startup and shutdown of dynamic library.
extern "C" bool WINAPI DllMain( HANDLE /*hinstDLL*/, DWORD reason, LPVOID /*lpvReserved*/ ) {
    switch( reason ) {
        case DLL_PROCESS_ATTACH:
            __TBB_InitOnce::add_ref();
            break;
        case DLL_PROCESS_DETACH:
            __TBB_InitOnce::remove_ref();
            // It is assumed that InitializationDone is not set after DLL_PROCESS_DETACH,
            // and thus no race on InitializationDone is possible.
            if( __TBB_InitOnce::initialization_done() ) {
                // Remove reference that we added in DoOneTimeInitializations.
                __TBB_InitOnce::remove_ref();
            }
            break;
        case DLL_THREAD_DETACH:
            governor::terminate_auto_initialized_scheduler();
            break;
    }
    return true;
}
#endif /* (_WIN32||_WIN64) && !__TBB_SOURCE_DIRECTLY_INCLUDED */

void itt_store_pointer_with_release_v3( void* dst, void* src ) {
    ITT_NOTIFY(sync_releasing, dst);
    __TBB_store_with_release(*static_cast<void**>(dst),src);
}

void* itt_load_pointer_with_acquire_v3( const void* src ) {
    void* result = __TBB_load_with_acquire(*static_cast<void*const*>(src));
    ITT_NOTIFY(sync_acquired, const_cast<void*>(src));
    return result;
}
    
#if DO_ITT_NOTIFY
void call_itt_notify_v5(int t, void *ptr) {
    switch (t) {
    case 0: ITT_NOTIFY(sync_prepare, ptr); break;
    case 1: ITT_NOTIFY(sync_cancel, ptr); break;
    case 2: ITT_NOTIFY(sync_acquired, ptr); break;
    case 3: ITT_NOTIFY(sync_releasing, ptr); break;
    }
}
#else
void call_itt_notify_v5(int /*t*/, void* /*ptr*/) {}
#endif

#if __TBB_ITT_STRUCTURE_API

#if DO_ITT_NOTIFY

const __itt_id itt_null_id = {0, 0, 0};

static inline __itt_domain* get_itt_domain( itt_domain_enum idx ) {
    return ( idx == ITT_DOMAIN_FLOW ) ? fgt_domain : NULL;
}

static inline void itt_id_make(__itt_id *id, void* addr, unsigned long long extra) {
    *id = __itt_id_make(addr, extra);
}

static inline void itt_id_create(const __itt_domain *domain, __itt_id id) {
    ITTNOTIFY_VOID_D1(id_create, domain, id);
}

void itt_make_task_group_v7( itt_domain_enum domain, void *group, unsigned long long group_extra, 
                             void *parent, unsigned long long parent_extra, string_index name_index ) {
    if ( __itt_domain *d = get_itt_domain( domain ) ) {
        __itt_id group_id = itt_null_id;
        __itt_id parent_id = itt_null_id;
        itt_id_make( &group_id, group, group_extra );
        itt_id_create( d, group_id );
        if ( parent ) {
            itt_id_make( &parent_id, parent, parent_extra );
        }
        __itt_string_handle *n = ITT_get_string_handle(name_index);
        ITTNOTIFY_VOID_D3(task_group, d, group_id, parent_id, n);
    }
}

void itt_metadata_str_add_v7( itt_domain_enum domain, void *addr, unsigned long long addr_extra, 
                              string_index key, const char *value ) {
    if ( __itt_domain *d = get_itt_domain( domain ) ) {
        __itt_id id = itt_null_id;
        itt_id_make( &id, addr, addr_extra );
        __itt_string_handle *k = ITT_get_string_handle(key);
       size_t value_length = strlen( value );
#if _WIN32||_WIN64
        ITTNOTIFY_VOID_D4(metadata_str_addA, d, id, k, value, value_length);
#else
        ITTNOTIFY_VOID_D4(metadata_str_add, d, id, k, value, value_length);
#endif
    }
}

void itt_relation_add_v7( itt_domain_enum domain, void *addr0, unsigned long long addr0_extra, 
                          itt_relation relation, void *addr1, unsigned long long addr1_extra ) {
    if ( __itt_domain *d = get_itt_domain( domain ) ) {
        __itt_id id0 = itt_null_id; 
        __itt_id id1 = itt_null_id;
        itt_id_make( &id0, addr0, addr0_extra );
        itt_id_make( &id1, addr1, addr1_extra );
        ITTNOTIFY_VOID_D3(relation_add, d, id0, (__itt_relation)relation, id1); 
    }
}

void itt_task_begin_v7( itt_domain_enum domain, void *task, unsigned long long task_extra, 
                        void *parent, unsigned long long parent_extra, string_index /* name_index */ ) {
    if ( __itt_domain *d = get_itt_domain( domain ) ) {
        __itt_id task_id = itt_null_id;
        __itt_id parent_id = itt_null_id;
        itt_id_make( &task_id, task, task_extra );
        if ( parent ) {
            itt_id_make( &parent_id, parent, parent_extra );
        }
        ITTNOTIFY_VOID_D3(task_begin, d, task_id, parent_id, NULL );
    }
}

void itt_task_end_v7( itt_domain_enum domain ) {
    if ( __itt_domain *d = get_itt_domain( domain ) ) {
        ITTNOTIFY_VOID_D0(task_end, d);
    }
}

#else // DO_ITT_NOTIFY

void itt_make_task_group_v7( itt_domain_enum domain, void *group, unsigned long long group_extra, 
                             void *parent, unsigned long long parent_extra, string_index name_index ) { }

void itt_metadata_str_add_v7( itt_domain_enum domain, void *addr, unsigned long long addr_extra, 
                              string_index key, const char *value ) { }

void itt_relation_add_v7( itt_domain_enum domain, void *addr0, unsigned long long addr0_extra, 
                          itt_relation relation, void *addr1, unsigned long long addr1_extra ) { }

void itt_task_begin_v7( itt_domain_enum domain, void *task, unsigned long long task_extra, 
                        void * /*parent*/, unsigned long long /* parent_extra */, string_index /* name_index */ ) { }

void itt_task_end_v7( itt_domain_enum domain ) { }

#endif // DO_ITT_NOTIFY

#endif // __TBB_ITT_STRUCTURE_API

void* itt_load_pointer_v3( const void* src ) {
    //TODO: replace this with __TBB_load_relaxed
    void* result = *static_cast<void*const*>(src);
    return result;
}

void itt_set_sync_name_v3( void* obj, const tchar* name) {
    ITT_SYNC_RENAME(obj, name);
    suppress_unused_warning(obj && name);
}


} // namespace internal
} // namespace tbb
