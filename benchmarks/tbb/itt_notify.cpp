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

#if DO_ITT_NOTIFY

#if _WIN32||_WIN64
    #ifndef UNICODE
        #define UNICODE
    #endif
#else
    #pragma weak dlopen
    #pragma weak dlsym
    #pragma weak dlerror
#endif /* WIN */

#if __TBB_BUILD

extern "C" void ITT_DoOneTimeInitialization();
#define __itt_init_ittlib_name(x,y) (ITT_DoOneTimeInitialization(), true)

#elif __TBBMALLOC_BUILD

extern "C" void MallocInitializeITT();
#define __itt_init_ittlib_name(x,y) (MallocInitializeITT(), true)

#else
#error This file is expected to be used for either TBB or TBB allocator build.
#endif // __TBB_BUILD

#include "tools_api/ittnotify_static.c"

namespace tbb {
namespace internal {
int __TBB_load_ittnotify() {
    return __itt_init_ittlib(NULL,          // groups for:
      (__itt_group_id)(__itt_group_sync     // prepare/cancel/acquired/releasing
                       | __itt_group_thread // name threads
                       | __itt_group_stitch // stack stitching
#if __TBB_CPF_BUILD
                       | __itt_group_structure
#endif
                           ));
}

}} // namespaces

#endif /* DO_ITT_NOTIFY */

#define __TBB_NO_IMPLICIT_LINKAGE 1
#include "itt_notify.h"

namespace tbb {

#if DO_ITT_NOTIFY
    const tchar 
            *SyncType_GlobalLock = _T("TbbGlobalLock"),
            *SyncType_Scheduler = _T("%Constant")
            ;
    const tchar 
            *SyncObj_SchedulerInitialization = _T("TbbSchedulerInitialization"),
            *SyncObj_SchedulersList = _T("TbbSchedulersList"),
            *SyncObj_WorkerLifeCycleMgmt = _T("TBB Scheduler"),
            *SyncObj_TaskStealingLoop = _T("TBB Scheduler"),
            *SyncObj_WorkerTaskPool = _T("TBB Scheduler"),
            *SyncObj_MasterTaskPool = _T("TBB Scheduler"),
            *SyncObj_TaskPoolSpinning = _T("TBB Scheduler"),
            *SyncObj_Mailbox = _T("TBB Scheduler"),
            *SyncObj_TaskReturnList = _T("TBB Scheduler"),
            *SyncObj_TaskStream = _T("TBB Scheduler"),
            *SyncObj_ContextsList = _T("TBB Scheduler")
            ;
#endif /* DO_ITT_NOTIFY */

} // namespace tbb

