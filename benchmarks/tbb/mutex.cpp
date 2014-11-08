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

#if _WIN32||_WIN64
#include <errno.h> // EDEADLK
#endif
#include "tbb/mutex.h"
#include "itt_notify.h"

namespace tbb {
    void mutex::scoped_lock::internal_acquire( mutex& m ) {

#if _WIN32||_WIN64
        switch( m.state ) {
        case INITIALIZED: 
        case HELD:
            EnterCriticalSection( &m.impl );
            // If a thread comes here, and another thread holds the lock, it will block
            // in EnterCriticalSection.  When it returns from EnterCriticalSection,
            // m.state must be set to INITIALIZED.  If the same thread tries to acquire a lock it
            // aleady holds, the lock is in HELD state, thus will cause throwing the exception.
            if (m.state==HELD)
                tbb::internal::handle_perror(EDEADLK,"mutex::scoped_lock: deadlock caused by attempt to reacquire held mutex");
            m.state = HELD;
            break;
        case DESTROYED:
            __TBB_ASSERT(false,"mutex::scoped_lock: mutex already destroyed");
            break;
        default:
            __TBB_ASSERT(false,"mutex::scoped_lock: illegal mutex state");
            break;
        }
#else
        int error_code = pthread_mutex_lock(&m.impl);
        if( error_code )
            tbb::internal::handle_perror(error_code,"mutex::scoped_lock: pthread_mutex_lock failed");
#endif /* _WIN32||_WIN64 */
        my_mutex = &m;
    }

void mutex::scoped_lock::internal_release() {
    __TBB_ASSERT( my_mutex, "mutex::scoped_lock: not holding a mutex" );
#if _WIN32||_WIN64    
     switch( my_mutex->state ) {
        case INITIALIZED: 
            __TBB_ASSERT(false,"mutex::scoped_lock: try to release the lock without acquisition");
            break;
        case HELD:
            my_mutex->state = INITIALIZED;
            LeaveCriticalSection(&my_mutex->impl);
            break;
        case DESTROYED: 
            __TBB_ASSERT(false,"mutex::scoped_lock: mutex already destroyed"); 
            break;
        default: 
            __TBB_ASSERT(false,"mutex::scoped_lock: illegal mutex state");
            break;
    }
#else
     int error_code = pthread_mutex_unlock(&my_mutex->impl);
     __TBB_ASSERT_EX(!error_code, "mutex::scoped_lock: pthread_mutex_unlock failed");
#endif /* _WIN32||_WIN64 */
     my_mutex = NULL;
}

bool mutex::scoped_lock::internal_try_acquire( mutex& m ) {
#if _WIN32||_WIN64
    switch( m.state ) {
        case INITIALIZED: 
        case HELD:
            break;
        case DESTROYED: 
            __TBB_ASSERT(false,"mutex::scoped_lock: mutex already destroyed"); 
            break;
        default: 
            __TBB_ASSERT(false,"mutex::scoped_lock: illegal mutex state");
            break;
    }
#endif /* _WIN32||_WIN64 */

    bool result;
#if _WIN32||_WIN64
    result = TryEnterCriticalSection(&m.impl)!=0;
    if( result ) {
        __TBB_ASSERT(m.state!=HELD, "mutex::scoped_lock: deadlock caused by attempt to reacquire held mutex");
        m.state = HELD;
    }
#else
    result = pthread_mutex_trylock(&m.impl)==0;
#endif /* _WIN32||_WIN64 */
    if( result ) 
        my_mutex = &m;
    return result;
}

void mutex::internal_construct() {
#if _WIN32||_WIN64
    InitializeCriticalSectionEx(&impl, 4000, 0);
    state = INITIALIZED;  
#else
    int error_code = pthread_mutex_init(&impl,NULL);
    if( error_code )
        tbb::internal::handle_perror(error_code,"mutex: pthread_mutex_init failed");
#endif /* _WIN32||_WIN64*/    
    ITT_SYNC_CREATE(&impl, _T("tbb::mutex"), _T(""));
}

void mutex::internal_destroy() {
#if _WIN32||_WIN64
    switch( state ) {
      case INITIALIZED:
        DeleteCriticalSection(&impl);
       break;
      case DESTROYED: 
        __TBB_ASSERT(false,"mutex: already destroyed");
        break;
      default: 
        __TBB_ASSERT(false,"mutex: illegal state for destruction");
        break;
    }
    state = DESTROYED;
#else
    int error_code = pthread_mutex_destroy(&impl); 
    __TBB_ASSERT_EX(!error_code,"mutex: pthread_mutex_destroy failed");
#endif /* _WIN32||_WIN64 */
}

} // namespace tbb
