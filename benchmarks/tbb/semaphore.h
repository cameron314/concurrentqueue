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

#ifndef __TBB_tbb_semaphore_H
#define __TBB_tbb_semaphore_H

#include "tbb/tbb_stddef.h"

#if _WIN32||_WIN64
#include "tbb/machine/windows_api.h"

#elif __APPLE__
#include <mach/semaphore.h>
#include <mach/task.h>
#include <mach/mach_init.h>
#include <mach/error.h>

#else
#include <semaphore.h>
#ifdef TBB_USE_DEBUG
#include <errno.h>
#endif
#endif /*_WIN32||_WIN64*/

namespace tbb {
namespace internal {


#if _WIN32||_WIN64
typedef LONG sem_count_t;
//! Edsger Dijkstra's counting semaphore
class semaphore : no_copy {
    static const int max_semaphore_cnt = MAXLONG;
public:
    //! ctor
    semaphore(size_t start_cnt_ = 0) {init_semaphore(start_cnt_);}
    //! dtor
    ~semaphore() {CloseHandle( sem );}
    //! wait/acquire
    void P() {WaitForSingleObjectEx( sem, INFINITE, FALSE );}
    //! post/release 
    void V() {ReleaseSemaphore( sem, 1, NULL );}
private:
    HANDLE sem;
    void init_semaphore(size_t start_cnt_) {
        sem = CreateSemaphoreEx( NULL, LONG(start_cnt_), max_semaphore_cnt, NULL, 0, SEMAPHORE_ALL_ACCESS );
    }
};
#elif __APPLE__
//! Edsger Dijkstra's counting semaphore
class semaphore : no_copy {
public:
    //! ctor
    semaphore(int start_cnt_ = 0) : sem(start_cnt_) { init_semaphore(start_cnt_); }
    //! dtor
    ~semaphore() {
        kern_return_t ret = semaphore_destroy( mach_task_self(), sem );
        __TBB_ASSERT_EX( ret==err_none, NULL );
    }
    //! wait/acquire
    void P() { 
        int ret;
        do {
            ret = semaphore_wait( sem );
        } while( ret==KERN_ABORTED );
        __TBB_ASSERT( ret==KERN_SUCCESS, "semaphore_wait() failed" );
    }
    //! post/release 
    void V() { semaphore_signal( sem ); }
private:
    semaphore_t sem;
    void init_semaphore(int start_cnt_) {
        kern_return_t ret = semaphore_create( mach_task_self(), &sem, SYNC_POLICY_FIFO, start_cnt_ );
        __TBB_ASSERT_EX( ret==err_none, "failed to create a semaphore" );
    }
};
#else /* Linux/Unix */
typedef uint32_t sem_count_t;
//! Edsger Dijkstra's counting semaphore
class semaphore : no_copy {
public:
    //! ctor
    semaphore(int start_cnt_ = 0 ) { init_semaphore( start_cnt_ ); }

    //! dtor
    ~semaphore() {
        int ret = sem_destroy( &sem );
        __TBB_ASSERT_EX( !ret, NULL );
    }
    //! wait/acquire
    void P() {
        while( sem_wait( &sem )!=0 )
            __TBB_ASSERT( errno==EINTR, NULL );
    }
    //! post/release 
    void V() { sem_post( &sem ); }
private:
    sem_t sem;
    void init_semaphore(int start_cnt_) {
        int ret = sem_init( &sem, /*shared among threads*/ 0, start_cnt_ );
        __TBB_ASSERT_EX( !ret, NULL );
    }
};
#endif /* _WIN32||_WIN64 */


//! for performance reasons, we want specialized binary_semaphore
#if _WIN32||_WIN64
#if !__TBB_USE_SRWLOCK
//! binary_semaphore for concurrent_monitor
class binary_semaphore : no_copy {
public:
    //! ctor
    binary_semaphore() { my_sem = CreateEventEx( NULL, NULL, 0, EVENT_ALL_ACCESS );  }
    //! dtor
    ~binary_semaphore() { CloseHandle( my_sem ); }
    //! wait/acquire
    void P() { WaitForSingleObjectEx( my_sem, INFINITE, FALSE ); }
    //! post/release 
    void V() { SetEvent( my_sem ); }
private:
    HANDLE my_sem;
};
#else /* __TBB_USE_SRWLOCK */

union srwl_or_handle {
    SRWLOCK lock;
    HANDLE  h;
};

//! binary_semaphore for concurrent_monitor
class binary_semaphore : no_copy {
public:
    //! ctor
    binary_semaphore();
    //! dtor
    ~binary_semaphore();
    //! wait/acquire
    void P();
    //! post/release 
    void V();
private:
    srwl_or_handle my_sem;
};
#endif /* !__TBB_USE_SRWLOCK */
#elif __APPLE__
//! binary_semaphore for concurrent monitor
class binary_semaphore : no_copy {
public:
    //! ctor
    binary_semaphore() : my_sem(0) {
        kern_return_t ret = semaphore_create( mach_task_self(), &my_sem, SYNC_POLICY_FIFO, 0 );
        __TBB_ASSERT_EX( ret==err_none, "failed to create a semaphore" );
    }
    //! dtor
    ~binary_semaphore() {
        kern_return_t ret = semaphore_destroy( mach_task_self(), my_sem );
        __TBB_ASSERT_EX( ret==err_none, NULL );
    }
    //! wait/acquire
    void P() { 
        int ret;
        do {
            ret = semaphore_wait( my_sem );
        } while( ret==KERN_ABORTED );
        __TBB_ASSERT( ret==KERN_SUCCESS, "semaphore_wait() failed" );
    }
    //! post/release 
    void V() { semaphore_signal( my_sem ); }
private:
    semaphore_t my_sem;
};
#else /* Linux/Unix */

#if __TBB_USE_FUTEX
class binary_semaphore : no_copy {
public:
    //! ctor
    binary_semaphore() { my_sem = 1; }
    //! dtor
    ~binary_semaphore() {}
    //! wait/acquire
    void P() {
        int s;
        if( (s = my_sem.compare_and_swap( 1, 0 ))!=0 ) {
            if( s!=2 )
                s = my_sem.fetch_and_store( 2 );
            while( s!=0 ) {
                futex_wait( &my_sem, 2 );
                s = my_sem.fetch_and_store( 2 );
            }
        }
    }
    //! post/release 
    void V() { 
        __TBB_ASSERT( my_sem>=1, "multiple V()'s in a row?" );
        if( my_sem--!=1 ) {
            //if old value was 2
            my_sem = 0;
            futex_wakeup_one( &my_sem );
        }
    }
private:
    atomic<int> my_sem;
};
#else
typedef uint32_t sem_count_t;
//! binary_semaphore for concurrent monitor
class binary_semaphore : no_copy {
public:
    //! ctor
    binary_semaphore() {
        int ret = sem_init( &my_sem, /*shared among threads*/ 0, 0 );
        __TBB_ASSERT_EX( !ret, NULL );
    }
    //! dtor
    ~binary_semaphore() {
        int ret = sem_destroy( &my_sem );
        __TBB_ASSERT_EX( !ret, NULL );
    }
    //! wait/acquire
    void P() {
        while( sem_wait( &my_sem )!=0 )
            __TBB_ASSERT( errno==EINTR, NULL );
    }
    //! post/release 
    void V() { sem_post( &my_sem ); }
private:
    sem_t my_sem;
};
#endif /* __TBB_USE_FUTEX */
#endif /* _WIN32||_WIN64 */

} // namespace internal
} // namespace tbb

#endif /* __TBB_tbb_semaphore_H */
