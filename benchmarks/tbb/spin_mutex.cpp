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

#include "tbb/tbb_machine.h"
#include "tbb/spin_mutex.h"
#include "itt_notify.h"
#include "tbb_misc.h"

namespace tbb {

void spin_mutex::scoped_lock::internal_acquire( spin_mutex& m ) {
    __TBB_ASSERT( !my_mutex, "already holding a lock on a spin_mutex" );
    ITT_NOTIFY(sync_prepare, &m);
    __TBB_LockByte(m.flag);
    my_mutex = &m;
    ITT_NOTIFY(sync_acquired, &m);
}

void spin_mutex::scoped_lock::internal_release() {
    __TBB_ASSERT( my_mutex, "release on spin_mutex::scoped_lock that is not holding a lock" );

    ITT_NOTIFY(sync_releasing, my_mutex);
    __TBB_UnlockByte(my_mutex->flag);
    my_mutex = NULL;
}

bool spin_mutex::scoped_lock::internal_try_acquire( spin_mutex& m ) {
    __TBB_ASSERT( !my_mutex, "already holding a lock on a spin_mutex" );
    bool result = bool( __TBB_TryLockByte(m.flag) );
    if( result ) {
        my_mutex = &m;
        ITT_NOTIFY(sync_acquired, &m);
    }
    return result;
}

void spin_mutex::internal_construct() {
    ITT_SYNC_CREATE(this, _T("tbb::spin_mutex"), _T(""));
}

} // namespace tbb
