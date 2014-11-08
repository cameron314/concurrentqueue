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

#ifndef _TBB_governor_H
#define _TBB_governor_H

#include "tbb/task_scheduler_init.h"
#include "../rml/include/rml_tbb.h"

#include "tbb_misc.h" // for AvailableHwConcurrency and ThreadStackSize
#include "tls.h"

#if __TBB_SURVIVE_THREAD_SWITCH
#include "cilk-tbb-interop.h"
#endif /* __TBB_SURVIVE_THREAD_SWITCH */

namespace tbb {
namespace internal {

class market;
class generic_scheduler;
class __TBB_InitOnce;

//------------------------------------------------------------------------
// Class governor
//------------------------------------------------------------------------

//! The class handles access to the single instance of market, and to TLS to keep scheduler instances.
/** It also supports automatic on-demand initialization of the TBB scheduler.
    The class contains only static data members and methods.*/
class governor {
    friend class __TBB_InitOnce;
    friend class market;

    //! TLS for scheduler instances associated with individual threads
    static basic_tls<generic_scheduler*> theTLS;

    //! Caches the maximal level of parallelism supported by the hardware
    static unsigned DefaultNumberOfThreads;

    static rml::tbb_factory theRMLServerFactory;

    static bool UsePrivateRML;

    //! Instance of task_scheduler_init that requested blocking termination.
    static const task_scheduler_init *BlockingTSI;

#if TBB_USE_ASSERT
    static bool IsBlockingTerminationInProgress;
#endif

    static bool is_speculation_enabled;

    //! Create key for thread-local storage and initialize RML.
    static void acquire_resources ();

    //! Destroy the thread-local storage key and deinitialize RML.
    static void release_resources ();

    static rml::tbb_server* create_rml_server ( rml::tbb_client& );

    //! The internal routine to undo automatic initialization.
    /** The signature is written with void* so that the routine
        can be the destructor argument to pthread_key_create. */
    static void auto_terminate(void* scheduler);

public:
    static unsigned default_num_threads () {
        // No memory fence required, because at worst each invoking thread calls AvailableHwConcurrency once.
        return DefaultNumberOfThreads ? DefaultNumberOfThreads :
                                        DefaultNumberOfThreads = AvailableHwConcurrency();
    }
    //! Processes scheduler initialization request (possibly nested) in a master thread
    /** If necessary creates new instance of arena and/or local scheduler.
        The auto_init argument specifies if the call is due to automatic initialization. **/
    static generic_scheduler* init_scheduler( unsigned num_threads, stack_size_type stack_size, bool auto_init = false );

    //! Processes scheduler termination request (possibly nested) in a master thread
    static void terminate_scheduler( generic_scheduler* s, const task_scheduler_init *tsi_ptr );

    //! Register TBB scheduler instance in thread-local storage.
    static void sign_on(generic_scheduler* s);

    //! Unregister TBB scheduler instance from thread-local storage.
    static void sign_off(generic_scheduler* s);

    //! Used to check validity of the local scheduler TLS contents.
    static bool is_set ( generic_scheduler* s ) { return theTLS.get() == s; }

    //! Temporarily set TLS slot to the given scheduler
    static void assume_scheduler( generic_scheduler* s ) { theTLS.set( s ); }

    //! Obtain the thread-local instance of the TBB scheduler.
    /** If the scheduler has not been initialized yet, initialization is done automatically.
        Note that auto-initialized scheduler instance is destroyed only when its thread terminates. **/
    static generic_scheduler* local_scheduler () {
        generic_scheduler* s = theTLS.get();
        return s ? s : init_scheduler( (unsigned)task_scheduler_init::automatic, 0, true );
    }

    static generic_scheduler* local_scheduler_if_initialized () {
        return theTLS.get();
    }

    //! Undo automatic initialization if necessary; call when a thread exits.
    static void terminate_auto_initialized_scheduler() {
        auto_terminate( theTLS.get() );
    }

    static void print_version_info ();

    static void initialize_rml_factory ();

    static bool needsWaitWorkers () { return BlockingTSI!=NULL; }

    //! Must be called before init_scheduler
    static void setBlockingTerminate(const task_scheduler_init *tsi);

#if __TBB_SURVIVE_THREAD_SWITCH
    static __cilk_tbb_retcode stack_op_handler( __cilk_tbb_stack_op op, void* );
#endif /* __TBB_SURVIVE_THREAD_SWITCH */
    static bool speculation_enabled() { return is_speculation_enabled; }

}; // class governor

} // namespace internal
} // namespace tbb

#endif /* _TBB_governor_H */
