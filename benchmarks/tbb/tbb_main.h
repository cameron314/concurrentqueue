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

#ifndef _TBB_tbb_main_H
#define _TBB_tbb_main_H

#include "tbb/atomic.h"

namespace tbb {

namespace internal {

void DoOneTimeInitializations ();

//------------------------------------------------------------------------
// __TBB_InitOnce
//------------------------------------------------------------------------

//! Class that supports TBB initialization. 
/** It handles acquisition and release of global resources (e.g. TLS) during startup and shutdown,
    as well as synchronization for DoOneTimeInitializations. */
class __TBB_InitOnce {
    friend void DoOneTimeInitializations();
    friend void ITT_DoUnsafeOneTimeInitialization ();

    static atomic<int> count;

    //! Platform specific code to acquire resources.
    static void acquire_resources();

    //! Platform specific code to release resources.
    static void release_resources();

    //! Specifies if the one-time initializations has been done.
    static bool InitializationDone;

    //! Global initialization lock
    /** Scenarios are possible when tools interop has to be initialized before the
        TBB itself. This imposes a requirement that the global initialization lock 
        has to support valid static initialization, and does not issue any tool
        notifications in any build mode. **/
    static __TBB_atomic_flag InitializationLock;

public:
    static void lock()   { __TBB_LockByte( InitializationLock ); }

    static void unlock() { __TBB_UnlockByte( InitializationLock ); }

    static bool initialization_done() { return __TBB_load_with_acquire(InitializationDone); }

    //! Add initial reference to resources. 
    /** We assume that dynamic loading of the library prevents any other threads 
        from entering the library until this constructor has finished running. **/
    __TBB_InitOnce() { add_ref(); }

    //! Remove the initial reference to resources.
    /** This is not necessarily the last reference if other threads are still running. **/
    ~__TBB_InitOnce() {
        remove_ref();
        // We assume that InitializationDone is not set after file-scope destructors
        // start running, and thus no race on InitializationDone is possible.
        if( initialization_done() ) {
            // Remove an extra reference that was added in DoOneTimeInitializations.
            remove_ref();  
        }
    } 
    //! Add reference to resources.  If first reference added, acquire the resources.
    static void add_ref();

    //! Remove reference to resources.  If last reference removed, release the resources.
    static void remove_ref();
}; // class __TBB_InitOnce


} // namespace internal

} // namespace tbb

#endif /* _TBB_tbb_main_H */
