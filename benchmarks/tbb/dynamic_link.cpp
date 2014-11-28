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

#include "dynamic_link.h"
#include "tbb/tbb_config.h"

/*
    This file is used by both TBB and OpenMP RTL. Do not use __TBB_ASSERT() macro
    and runtime_warning() function because they are not available in OpenMP. Use
    LIBRARY_ASSERT and DYNAMIC_LINK_WARNING instead.
*/

#include <cstdarg>          // va_list etc.
#if _WIN32
    #include <malloc.h>

    // Unify system calls
    #define dlopen( name, flags )   LoadLibraryA( name )
    #define dlsym( handle, name )   GetProcAddress( handle, name )
    #define dlclose( handle )       ( ! FreeLibrary( handle ) )
    #define dlerror()               GetLastError()
#ifndef PATH_MAX
    #define PATH_MAX                MAX_PATH
#endif
#else /* _WIN32 */
    #include <dlfcn.h>
    #include <string.h>
    #include <unistd.h>
    #include <limits.h>
    #include <stdlib.h>
#endif /* _WIN32 */

#if __TBB_WEAK_SYMBOLS_PRESENT
    //TODO: use function attribute for weak symbols instead of the pragma.
    #pragma weak dlopen
    #pragma weak dlsym
    #pragma weak dlclose
    #pragma weak dlerror
    #pragma weak dladdr
#endif /* __TBB_WEAK_SYMBOLS_PRESENT */

#include "tbb/tbb_misc.h"

#define __USE_TBB_ATOMICS       ( !(__linux__&&__ia64__) || __TBB_BUILD )
#define __USE_STATIC_DL_INIT    (!__ANDROID__)

#if !__USE_TBB_ATOMICS
#include <pthread.h>
#endif

/*
dynamic_link is a common interface for searching for required symbols in an
executable and dynamic libraries.

dynamic_link provides certain guarantees:
  1. Either all or none of the requested symbols are resolved. Moreover, if
  symbols are not resolved, the dynamic_link_descriptor table is not modified;
  2. All returned symbols have secured life time: this means that none of them
  can be invalidated until dynamic_unlink is called;
  3. Any loaded library is loaded only via the full path. The full path is that
  from which the runtime itself was loaded. (This is done to avoid security
  issues caused by loading libraries from insecure paths).

dynamic_link searches for the requested symbols in three stages, stopping as
soon as all of the symbols have been resolved.

  1. Search the global scope:
    a. On Windows: dynamic_link tries to obtain the handle of the requested
    library and if it succeeds it resolves the symbols via that handle.
    b. On Linux: dynamic_link tries to search for the symbols in the global
    scope via the main program handle. If the symbols are present in the global
    scope their life time is not guaranteed (since dynamic_link does not know
    anything about the library from which they are exported). Therefore it
    tries to "pin" the symbols by obtaining the library name and reopening it.
    dlopen may fail to reopen the library in two cases:
       i. The symbols are exported from the executable. Currently dynamic _link
      cannot handle this situation, so it will not find these symbols in this
      step.
      ii. The necessary library has been unloaded and cannot be reloaded. It
      seems there is nothing that can be done in this case. No symbols are
      returned.

  2. Dynamic load: an attempt is made to load the requested library via the
  full path.
    The full path used is that from which the runtime itself was loaded. If the
    library can be loaded, then an attempt is made to resolve the requested
    symbols in the newly loaded library.
    If the symbols are not found the library is unloaded.

  3. Weak symbols: if weak symbols are available they are returned.
*/

OPEN_INTERNAL_NAMESPACE

#if __TBB_WEAK_SYMBOLS_PRESENT || __TBB_DYNAMIC_LOAD_ENABLED

#if !defined(DYNAMIC_LINK_WARNING) && !__TBB_WIN8UI_SUPPORT
    // Report runtime errors and continue.
    #define DYNAMIC_LINK_WARNING dynamic_link_warning
    static void dynamic_link_warning( dynamic_link_error_t code, ... ) {
        (void) code;
    } // library_warning
#endif /* DYNAMIC_LINK_WARNING */
    static bool resolve_symbols( dynamic_link_handle module, const dynamic_link_descriptor descriptors[], size_t required )
    {
        LIBRARY_ASSERT( module != NULL, "Module handle is NULL" );
        if ( module == NULL )
            return false;

        #if __TBB_WEAK_SYMBOLS_PRESENT
            if ( !dlsym ) return false;
        #endif /* __TBB_WEAK_SYMBOLS_PRESENT */

        const size_t n_desc=20; // Usually we don't have more than 20 descriptors per library
        LIBRARY_ASSERT( required <= n_desc, "Too many descriptors is required" );
        if ( required > n_desc ) return false;
        pointer_to_handler h[n_desc];

        for ( size_t k = 0; k < required; ++k ) {
            dynamic_link_descriptor const & desc = descriptors[k];
            pointer_to_handler addr = (pointer_to_handler)dlsym( module, desc.name );
            if ( !addr ) {
                return false;
            }
            h[k] = addr;
        }

        // Commit the entry points.
        // Cannot use memset here, because the writes must be atomic.
        for( size_t k = 0; k < required; ++k )
            *descriptors[k].handler = h[k];
        return true;
    }

#if __TBB_WIN8UI_SUPPORT
    bool dynamic_link( const char*  library, const dynamic_link_descriptor descriptors[], size_t required, dynamic_link_handle*, int flags ) {
        dynamic_link_handle tmp_handle = NULL;
        TCHAR wlibrary[256];
        if ( MultiByteToWideChar(CP_UTF8, 0, library, -1, wlibrary, 255) == 0 ) return false;
        if ( flags & DYNAMIC_LINK_LOAD )
            tmp_handle = LoadPackagedLibrary( wlibrary, 0 );
        if (tmp_handle != NULL){
            return resolve_symbols(tmp_handle, descriptors, required);
        }else{
            return false;
        }
    }
    void dynamic_unlink( dynamic_link_handle ) {
    }
    void dynamic_unlink_all() {
    }
#else
/*
    There is a security issue on Windows: LoadLibrary() may load and execute malicious code.
    See http://www.microsoft.com/technet/security/advisory/2269637.mspx for details.
    To avoid the issue, we have to pass full path (not just library name) to LoadLibrary. This
    function constructs full path to the specified library (it is assumed the library located
    side-by-side with the tbb.dll.

    The function constructs absolute path for given relative path. Important: Base directory is not
    current one, it is the directory tbb.dll loaded from.

    Example:
        Let us assume "tbb.dll" is located in "c:\program files\common\intel\" directory, e. g.
        absolute path of tbb library is "c:\program files\common\intel\tbb.dll". Absolute path for
        "tbbmalloc.dll" would be "c:\program files\common\intel\tbbmalloc.dll". Absolute path for
        "malloc\tbbmalloc.dll" would be "c:\program files\common\intel\malloc\tbbmalloc.dll".
*/

    // Struct handle_storage is used by dynamic_link routine to store handles of
    // all loaded or pinned dynamic libraries. When TBB is shut down, it calls
    // dynamic_unlink_all() that unloads modules referenced by handle_storage.
    // This struct should not have any constructors since it may be used before
    // the constructor is called.
    #define MAX_LOADED_MODULES 8 // The number of maximum possible modules which can be loaded

    struct handle_storage {
    #if __USE_TBB_ATOMICS
        ::tbb::atomic<size_t> my_size;
    #else
        size_t my_size;
        pthread_spinlock_t my_lock;
    #endif
        dynamic_link_handle my_handles[MAX_LOADED_MODULES];

        void add_handle(const dynamic_link_handle &handle) {
        #if !__USE_TBB_ATOMICS
            int res = pthread_spin_lock( &my_lock );
            LIBRARY_ASSERT( res==0, "pthread_spin_lock failed" );
        #endif
            const size_t ind = my_size++;
        #if !__USE_TBB_ATOMICS
            res = pthread_spin_unlock( &my_lock );
            LIBRARY_ASSERT( res==0, "pthread_spin_unlock failed" );
        #endif
            LIBRARY_ASSERT( ind < MAX_LOADED_MODULES, "Too many modules are loaded" );
            my_handles[ind] = handle;
        }

        void free_handles() {
            const size_t size = my_size;
            for (size_t i=0; i<size; ++i)
                dynamic_unlink( my_handles[i] );
        }
    };

    handle_storage handles;

#if __USE_TBB_ATOMICS
    static void atomic_once ( void (*func) (void), tbb::atomic< tbb::internal::do_once_state > &once_state ) {
        tbb::internal::atomic_do_once( func, once_state );
    }
#define ATOMIC_ONCE_DECL( var ) tbb::atomic< tbb::internal::do_once_state > var
#else
    static void atomic_once ( void (*func) (), pthread_once_t &once_state ) {
        pthread_once( &once_state, func );
    }
#define ATOMIC_ONCE_DECL( var ) pthread_once_t var = PTHREAD_ONCE_INIT
#endif

    ATOMIC_ONCE_DECL( init_dl_data_state );

    static struct _ap_data {
        char _path[PATH_MAX+1];
        size_t _len;
    } ap_data;

    static void init_ap_data() {
    #if _WIN32
        // Get handle of our DLL first.
        HMODULE handle;
        BOOL brc = GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPSTR)( & dynamic_link ), // any function inside the library can be used for the address
            & handle
            );
        if ( !brc ) { // Error occurred.
            int err = GetLastError();
            DYNAMIC_LINK_WARNING( dl_sys_fail, "GetModuleHandleEx", err );
            return;
        }
        // Now get path to our DLL.
        DWORD drc = GetModuleFileNameA( handle, ap_data._path, static_cast< DWORD >( PATH_MAX ) );
        if ( drc == 0 ) { // Error occurred.
            int err = GetLastError();
            DYNAMIC_LINK_WARNING( dl_sys_fail, "GetModuleFileName", err );
            return;
        }
        if ( drc >= PATH_MAX ) { // Buffer too short.
            DYNAMIC_LINK_WARNING( dl_buff_too_small );
            return;
        }
        // Find the position of the last backslash.
        char *backslash = strrchr( ap_data._path, '\\' );

        if ( !backslash ) {    // Backslash not found.
            LIBRARY_ASSERT( backslash!=NULL, "Unbelievable.");
            return;
        }
        LIBRARY_ASSERT( backslash >= ap_data._path, "Unbelievable.");
        ap_data._len = (size_t)(backslash - ap_data._path) + 1;
        *(backslash+1) = 0;
    #else
        // Get the library path
        #if __TBB_WEAK_SYMBOLS_PRESENT
            if ( !dladdr || !dlerror ) return;
        #endif /* __TBB_WEAK_SYMBOLS_PRESENT */
        Dl_info dlinfo;
        int res = dladdr( (void*)&dynamic_link, &dlinfo ); // any function inside the library can be used for the address
        if ( !res ) {
            char const * err = dlerror();
            DYNAMIC_LINK_WARNING( dl_sys_fail, "dladdr", err );
            return;
        } else {
            LIBRARY_ASSERT( dlinfo.dli_fname!=NULL, "Unbelievable." );
        }

        char const *slash = strrchr( dlinfo.dli_fname, '/' );
        size_t fname_len=0;
        if ( slash ) {
            LIBRARY_ASSERT( slash >= dlinfo.dli_fname, "Unbelievable.");
            fname_len = (size_t)(slash - dlinfo.dli_fname) + 1;
        }

        size_t rc;
        if ( dlinfo.dli_fname[0]=='/' ) {
            // The library path is absolute
            rc = 0;
            ap_data._len = 0;
        } else {
            // The library path is relative so get the current working directory
            if ( !getcwd( ap_data._path, sizeof(ap_data._path)/sizeof(ap_data._path[0]) ) ) {
                DYNAMIC_LINK_WARNING( dl_buff_too_small );
                return;
            }
            ap_data._len = strlen( ap_data._path );
            ap_data._path[ap_data._len++]='/';
            rc = ap_data._len;
        }

        if ( fname_len>0 ) {
            if ( ap_data._len>PATH_MAX ) {
                DYNAMIC_LINK_WARNING( dl_buff_too_small );
                ap_data._len=0;
                return;
            }
            strncpy( ap_data._path+rc, dlinfo.dli_fname, fname_len );
            ap_data._len += fname_len;
            ap_data._path[ap_data._len]=0;
        }
    #endif /* _WIN32 */
    }

    static void init_dl_data() {
        init_ap_data();
    #if !__USE_TBB_ATOMICS
        int res;
        res = pthread_spin_init( &handles.my_lock, PTHREAD_PROCESS_SHARED );
        LIBRARY_ASSERT( res==0, "pthread_spin_init failed" );
    #endif
    }

    // ap_data structure is initialized with current directory on Linux.
    // So it should be initialized as soon as possible since the current directory may be changed.
    // static_init_ap_data object provides this initialization during library loading.
    static class _static_init_dl_data {
    public:
        _static_init_dl_data() {
    #if __USE_STATIC_DL_INIT
            atomic_once( &init_dl_data, init_dl_data_state );
    #endif
        }
    #if !__USE_TBB_ATOMICS
        ~_static_init_dl_data() {
            int res;
            res = pthread_spin_destroy( &handles.my_lock );
            LIBRARY_ASSERT( res==0, "pthread_spin_destroy failed" );
        }
    #endif
    } static_init_dl_data;

    /*
        The function constructs absolute path for given relative path. Important: Base directory is not
        current one, it is the directory libtbb.so loaded from.

        Arguments:
        in  name -- Name of a file (may be with relative path; it must not be an absolute one).
        out path -- Buffer to save result (absolute path) to.
        in  len  -- Size of buffer.
        ret      -- 0         -- Error occurred.
                    > len     -- Buffer too short, required size returned.
                    otherwise -- Ok, number of characters (not counting terminating null) written to
                    buffer.
    */
    #if __TBB_DYNAMIC_LOAD_ENABLED
    static size_t abs_path( char const * name, char * path, size_t len ) {
        atomic_once( &init_dl_data, init_dl_data_state );

        if ( !ap_data._len )
            return 0;

        size_t name_len = strlen( name );
        size_t full_len = name_len+ap_data._len;
        if ( full_len < len ) {
            strncpy( path, ap_data._path, ap_data._len );
            strncpy( path+ap_data._len, name, name_len );
            path[full_len] = 0;
        }
        return full_len;
    }
    #endif  // __TBB_DYNAMIC_LOAD_ENABLED

    #if __TBB_WEAK_SYMBOLS_PRESENT
    static bool weak_symbol_link( const dynamic_link_descriptor descriptors[], size_t required )
    {
        // Check if the required entries are present in what was loaded into our process.
        for ( size_t k = 0; k < required; ++k )
            if ( !descriptors[k].ptr )
                return false;
        // Commit the entry points.
        for ( size_t k = 0; k < required; ++k )
            *descriptors[k].handler = (pointer_to_handler) descriptors[k].ptr;
        return true;
    }
    #else
    static bool weak_symbol_link( const dynamic_link_descriptor[], size_t ) {
        return false;
    }
    #endif /* __TBB_WEAK_SYMBOLS_PRESENT */

    void dynamic_unlink( dynamic_link_handle handle ) {
        if ( handle ) {
    #if __TBB_WEAK_SYMBOLS_PRESENT
        LIBRARY_ASSERT( dlclose != NULL, "dlopen is present but dlclose is NOT present!?" );
    #endif /* __TBB_WEAK_SYMBOLS_PRESENT */
    #if __TBB_DYNAMIC_LOAD_ENABLED
            dlclose( handle );
    #endif /* __TBB_DYNAMIC_LOAD_ENABLED */
        }
    }

    void dynamic_unlink_all() {
        handles.free_handles();
    }

    #if _WIN32
    static dynamic_link_handle global_symbols_link( const char* library, const dynamic_link_descriptor descriptors[], size_t required ) {
        dynamic_link_handle library_handle;
        if ( GetModuleHandleExA( 0, library, &library_handle ) ) {
            if ( resolve_symbols( library_handle, descriptors, required ) )
                return library_handle;
            else
                FreeLibrary( library_handle );
        }
        return 0;
    }
    #else /* _WIN32 */
    // It is supposed that all symbols are from the only one library
    static dynamic_link_handle pin_symbols( dynamic_link_descriptor desc, const dynamic_link_descriptor descriptors[], size_t required ) {
        // The library has been loaded by another module and contains at least one requested symbol.
        // But after we obtained the symbol the library can be unloaded by another thread
        // invalidating our symbol. Therefore we need to pin the library in memory.
        dynamic_link_handle library_handle;
        Dl_info info;
        // Get library's name from earlier found symbol
        if ( dladdr( (void*)*desc.handler, &info ) ) {
            // Pin the library
            library_handle = dlopen( info.dli_fname, RTLD_LAZY );
            if ( library_handle ) {
                // If original library was unloaded before we pinned it
                // and then another module loaded in its place, the earlier
                // found symbol would become invalid. So revalidate them.
                if ( !resolve_symbols( library_handle, descriptors, required ) ) {
                    // Wrong library.
                    dynamic_unlink(library_handle);
                    library_handle = 0;
                }
            } else {
                char const * err = dlerror();
                DYNAMIC_LINK_WARNING( dl_lib_not_found, info.dli_fname, err );
            }
        }
        else {
            // The library have been unloaded by another thread
            library_handle = 0;
        }
        return library_handle;
    }

    static dynamic_link_handle global_symbols_link( const char*, const dynamic_link_descriptor descriptors[], size_t required ) {
    #if __TBB_WEAK_SYMBOLS_PRESENT
        if ( !dlopen ) return 0;
    #endif /* __TBB_WEAK_SYMBOLS_PRESENT */
        dynamic_link_handle library_handle = dlopen( NULL, RTLD_LAZY );
    #if __ANDROID__
        // On Android dlopen( NULL ) returns NULL if it is called during dynamic module initialization.
        if ( !library_handle )
            return 0;
    #endif
        // Check existence of only the first symbol, then use it to find the library and load all necessary symbols
        pointer_to_handler handler;
        dynamic_link_descriptor desc = { descriptors[0].name, &handler };
        if ( resolve_symbols( library_handle, &desc, 1 ) )
                return pin_symbols( desc, descriptors, required );
        return 0;
    }
    #endif /* _WIN32 */

    static void save_library_handle( dynamic_link_handle src, dynamic_link_handle *dst ) {
        if ( dst )
            *dst = src;
        else
            handles.add_handle( src );
    }

    dynamic_link_handle dynamic_load( const char* library, const dynamic_link_descriptor descriptors[], size_t required ) {
    #if __TBB_DYNAMIC_LOAD_ENABLED
    #if _XBOX
        return LoadLibrary (library);
    #else /* _XBOX */
        size_t const len = PATH_MAX + 1;
        char path[ len ];
        size_t rc = abs_path( library, path, len );
        if ( 0 < rc && rc < len ) {
    #if _WIN32
            // Prevent Windows from displaying silly message boxes if it fails to load library
            // (e.g. because of MS runtime problems - one of those crazy manifest related ones)
            UINT prev_mode = SetErrorMode (SEM_FAILCRITICALERRORS);
    #endif /* _WIN32 */
    #if __TBB_WEAK_SYMBOLS_PRESENT
        if ( !dlopen ) return 0;
    #endif /* __TBB_WEAK_SYMBOLS_PRESENT */
            dynamic_link_handle library_handle = dlopen( path, RTLD_LAZY );
    #if _WIN32
            SetErrorMode (prev_mode);
    #endif /* _WIN32 */
            if( library_handle ) {
                if( !resolve_symbols( library_handle, descriptors, required ) ) {
                    // The loaded library does not contain all the expected entry points
                    dynamic_unlink( library_handle );
                    library_handle = NULL;
                }
            } else
                DYNAMIC_LINK_WARNING( dl_lib_not_found, path, dlerror() );
            return library_handle;
        } else if ( rc>=len )
                DYNAMIC_LINK_WARNING( dl_buff_too_small );
                // rc == 0 means failing of init_ap_data so the warning has already been issued.
    #endif /* _XBOX */
    #endif /* __TBB_DYNAMIC_LOAD_ENABLED */
        return 0;
    }

    bool dynamic_link( const char* library, const dynamic_link_descriptor descriptors[], size_t required, dynamic_link_handle *handle, int flags ) {
        // TODO: May global_symbols_link find weak symbols?
        dynamic_link_handle library_handle = ( flags & DYNAMIC_LINK_GLOBAL ) ? global_symbols_link( library, descriptors, required ) : 0;

        if ( !library_handle && ( flags & DYNAMIC_LINK_LOAD ) )
            library_handle = dynamic_load( library, descriptors, required );

        if ( !library_handle && ( flags & DYNAMIC_LINK_WEAK ) )
            return weak_symbol_link( descriptors, required );

        save_library_handle( library_handle, handle );
        return true;
    }

#endif /*__TBB_WIN8UI_SUPPORT*/
#else /* __TBB_WEAK_SYMBOLS_PRESENT || __TBB_DYNAMIC_LOAD_ENABLED */
    bool dynamic_link( const char*, const dynamic_link_descriptor*, size_t, dynamic_link_handle *handle, int ) {
        if ( handle )
            *handle=0;
        return false;
    }

    void dynamic_unlink( dynamic_link_handle ) {
    }

    void dynamic_unlink_all() {
    }
#endif /* __TBB_WEAK_SYMBOLS_PRESENT || __TBB_DYNAMIC_LOAD_ENABLED */

CLOSE_INTERNAL_NAMESPACE
