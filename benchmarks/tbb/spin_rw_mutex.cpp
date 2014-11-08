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

#include "tbb/spin_rw_mutex.h"
#include "tbb/tbb_machine.h"
#include "tbb/atomic.h"
#include "itt_notify.h"

#if defined(_MSC_VER) && defined(_Wp64)
    // Workaround for overzealous compiler warnings in /Wp64 mode
    #pragma warning (disable: 4244)
#endif

namespace tbb {

template<typename T> // a template can work with private spin_rw_mutex::state_t
static inline T CAS(volatile T &addr, T newv, T oldv) {
    // ICC (9.1 and 10.1 tried) unable to do implicit conversion
    // from "volatile T*" to "volatile void*", so explicit cast added.
    return tbb::internal::as_atomic(addr).compare_and_swap( newv, oldv );
}

//! Acquire write lock on the given mutex.
bool spin_rw_mutex_v3::internal_acquire_writer()
{
    ITT_NOTIFY(sync_prepare, this);
    for( internal::atomic_backoff backoff;;backoff.pause() ){
        state_t s = const_cast<volatile state_t&>(state); // ensure reloading
        if( !(s & BUSY) ) { // no readers, no writers
            if( CAS(state, WRITER, s)==s )
                break; // successfully stored writer flag
            backoff.reset(); // we could be very close to complete op.
        } else if( !(s & WRITER_PENDING) ) { // no pending writers
            __TBB_AtomicOR(&state, WRITER_PENDING);
        }
    }
    ITT_NOTIFY(sync_acquired, this);
    return false;
}

//! Release writer lock on the given mutex
void spin_rw_mutex_v3::internal_release_writer()
{
    ITT_NOTIFY(sync_releasing, this);
    __TBB_AtomicAND( &state, READERS );
}

//! Acquire read lock on given mutex.
void spin_rw_mutex_v3::internal_acquire_reader()
{
    ITT_NOTIFY(sync_prepare, this);
    for( internal::atomic_backoff b;;b.pause() ){
        state_t s = const_cast<volatile state_t&>(state); // ensure reloading
        if( !(s & (WRITER|WRITER_PENDING)) ) { // no writer or write requests
            state_t t = (state_t)__TBB_FetchAndAddW( &state, (intptr_t) ONE_READER );
            if( !( t&WRITER )) 
                break; // successfully stored increased number of readers
            // writer got there first, undo the increment
            __TBB_FetchAndAddW( &state, -(intptr_t)ONE_READER );
        }
    }

    ITT_NOTIFY(sync_acquired, this);
    __TBB_ASSERT( state & READERS, "invalid state of a read lock: no readers" );
}

//! Upgrade reader to become a writer.
/** Returns whether the upgrade happened without releasing and re-acquiring the lock */
bool spin_rw_mutex_v3::internal_upgrade()
{
    state_t s = state;
    __TBB_ASSERT( s & READERS, "invalid state before upgrade: no readers " );
    // check and set writer-pending flag
    // required conditions: either no pending writers, or we are the only reader
    // (with multiple readers and pending writer, another upgrade could have been requested)
    while( (s & READERS)==ONE_READER || !(s & WRITER_PENDING) ) {
        state_t old_s = s;
        if( (s=CAS(state, s | WRITER | WRITER_PENDING, s))==old_s ) {
            ITT_NOTIFY(sync_prepare, this);
            internal::atomic_backoff backoff;
            while( (state & READERS) != ONE_READER ) backoff.pause();
            __TBB_ASSERT((state&(WRITER_PENDING|WRITER))==(WRITER_PENDING|WRITER),"invalid state when upgrading to writer");
            // both new readers and writers are blocked at this time
            __TBB_FetchAndAddW( &state,  - (intptr_t)(ONE_READER+WRITER_PENDING));
            ITT_NOTIFY(sync_acquired, this);
            return true; // successfully upgraded
        }
    }
    // slow reacquire
    internal_release_reader();
    return internal_acquire_writer(); // always returns false
}

//! Downgrade writer to a reader
void spin_rw_mutex_v3::internal_downgrade() {
    ITT_NOTIFY(sync_releasing, this);
    __TBB_FetchAndAddW( &state, (intptr_t)(ONE_READER-WRITER));
    __TBB_ASSERT( state & READERS, "invalid state after downgrade: no readers" );
}

//! Release read lock on the given mutex
void spin_rw_mutex_v3::internal_release_reader()
{
    __TBB_ASSERT( state & READERS, "invalid state of a read lock: no readers" );
    ITT_NOTIFY(sync_releasing, this); // release reader
    __TBB_FetchAndAddWrelease( &state,-(intptr_t)ONE_READER);
}

//! Try to acquire write lock on the given mutex
bool spin_rw_mutex_v3::internal_try_acquire_writer()
{
    // for a writer: only possible to acquire if no active readers or writers
    state_t s = state;
    if( !(s & BUSY) ) // no readers, no writers; mask is 1..1101
        if( CAS(state, WRITER, s)==s ) {
            ITT_NOTIFY(sync_acquired, this);
            return true; // successfully stored writer flag
        }
    return false;
}

//! Try to acquire read lock on the given mutex
bool spin_rw_mutex_v3::internal_try_acquire_reader()
{
    // for a reader: acquire if no active or waiting writers
    state_t s = state;
    if( !(s & (WRITER|WRITER_PENDING)) ) { // no writers
        state_t t = (state_t)__TBB_FetchAndAddW( &state, (intptr_t) ONE_READER );
        if( !( t&WRITER )) {  // got the lock
            ITT_NOTIFY(sync_acquired, this);
            return true; // successfully stored increased number of readers
        }
        // writer got there first, undo the increment
        __TBB_FetchAndAddW( &state, -(intptr_t)ONE_READER );
    }
    return false;
}

void spin_rw_mutex_v3::internal_construct() {
    ITT_SYNC_CREATE(this, _T("tbb::spin_rw_mutex"), _T(""));
}
} // namespace tbb
