/*  Relacy Race Detector
 *  Copyright (c) 2008-2010, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE.TXT in this distribution.
 */

#ifndef RL_WINDOWS_HPP
#define RL_WINDOWS_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "mutex.hpp"
#include "condition_variable.hpp"
#include "semaphore.hpp"
#include "event.hpp"


namespace rl
{

typedef win_object* rl_HANDLE;
unsigned long const rl_INFINITE = (unsigned long)-1;

unsigned long const rl_WAIT_FAILED                = (unsigned long)-1;
unsigned long const rl_WAIT_OBJECT_0              = 100;
unsigned long const rl_WAIT_TIMEOUT               = 1;
unsigned long const rl_WAIT_IO_COMPLETION         = 2;
unsigned long const rl_MAXIMUM_WAIT_OBJECTS       = wfmo_max_objects;


inline int rl_SwitchToThread(debug_info_param info)
{
    yield(1, info);
    return 1;
}

inline void rl_Sleep(unsigned long milliseconds, debug_info_param info)
{
    yield(milliseconds ? milliseconds : 1, info);
}



inline unsigned long rl_WaitForSingleObjectEx(rl_HANDLE obj, unsigned long timeout, int alertable, debug_info_param info)
{
    (void)alertable; //!!! not yet supported – support it!
    //!!! support WAIT_IO_COMPLETION
    RL_VERIFY(false == alertable && "Alertable wait is not supported in WaitForSingleObject() yet");

    bool try_wait = (timeout == 0);
    bool is_timed = (timeout != rl_INFINITE);
    sema_wakeup_reason reason = static_cast<win_waitable_object*>(obj)->wait(try_wait, is_timed, info);
    if (reason == sema_wakeup_reason_success)
        return rl_WAIT_OBJECT_0;
    else if (reason == sema_wakeup_reason_timeout)
        return rl_WAIT_TIMEOUT;
    else if (reason == sema_wakeup_reason_failed)
        return rl_WAIT_TIMEOUT;
    RL_VERIFY(false);
    return rl_WAIT_FAILED;
}

inline unsigned long rl_WaitForSingleObject(rl_HANDLE obj, unsigned long timeout, debug_info_param info)
{
    return rl_WaitForSingleObjectEx(obj, timeout, 0, info);
}

inline unsigned long rl_WaitForMultipleObjectsEx(unsigned long count, rl_HANDLE* objects, int wait_all, unsigned long timeout, int alertable, debug_info_param info)
{
    (void)alertable; //!!!
    //!!! support WAIT_IO_COMPLETION
    RL_VERIFY(false == alertable && "Alertable wait is not supported in WaitForMultipleObjects() yet");

    bool try_wait = (timeout == 0);
    bool is_timed = (timeout != rl_INFINITE);
    win_waitable_object** obj = reinterpret_cast<win_waitable_object**>(objects);
    size_t signaled = 0;
    sema_wakeup_reason reason = wait_for_multiple_objects(signaled, count, obj, !!wait_all, try_wait, is_timed, info);
    if (reason == sema_wakeup_reason_success)
        return rl_WAIT_OBJECT_0 + (int)signaled;
    else if (reason == sema_wakeup_reason_timeout)
        return rl_WAIT_TIMEOUT;
    RL_VERIFY(false);
    return rl_WAIT_FAILED;
}

inline unsigned long rl_WaitForMultipleObjects(unsigned long count, rl_HANDLE* objects, int wait_all, unsigned long timeout, debug_info_param info)
{
    return rl_WaitForMultipleObjectsEx(count, objects, wait_all, timeout, 0, info);
}

inline unsigned long rl_SignalObjectAndWait(rl_HANDLE obj_to_signal,
                                            rl_HANDLE obj_to_wait,
                                            unsigned long timeout,
                                            int alertable,
                                            debug_info_param info)
{
    bool result = static_cast<win_waitable_object*>(obj_to_signal)->signal(info);
    if (false == result)
        return result ? 1 : 0;
    preemption_disabler pd (ctx());
    return rl_WaitForSingleObjectEx(obj_to_wait, timeout, alertable, info);
}



struct sem_tag_win;

inline rl_HANDLE rl_CreateSemaphore(void* /*security*/, long initial_count, long max_count, void const* /*name*/, debug_info_param info)
{
    void* mem = ctx().alloc(sizeof(semaphore<sem_tag_win>), false, info);
    semaphore<sem_tag_win>* sema = new (mem) semaphore<sem_tag_win>;
    sema->init(false, initial_count, max_count, info);
    return sema;
}

inline int rl_CloseHandle(rl_HANDLE h, debug_info_param info)
{
    h->deinit(info);
    h->~win_object();
    (ctx().free)(h, false, info); //!!! rename free because of the define
    return 1;
}

inline int rl_ReleaseSemaphore(rl_HANDLE sema, long count, long* prev_count, debug_info_param info)
{
    unsigned prev = 0;
    bool result = static_cast<semaphore<sem_tag_win>*>(sema)->post(count, prev, info);
    if (prev_count)
        prev_count[0] = prev;
    return result ? 1 : 0;
}




inline rl_HANDLE rl_CreateEvent(void* /*security*/, int manual_reset, int initial_state, void const* /*name*/, debug_info_param info)
{
    void* mem = ctx().alloc(sizeof(generic_event), false, info);
    generic_event* ev = new (mem) generic_event;
    ev->init(!!manual_reset, !!initial_state, info);
    return ev;
}

inline int rl_SetEvent(rl_HANDLE ev, debug_info_param info)
{
    static_cast<generic_event*>(ev)->set(info);
    return 1;
}

inline int rl_ResetEvent(rl_HANDLE ev, debug_info_param info)
{
    static_cast<generic_event*>(ev)->reset(info);
    return 1;
}

inline int rl_PulseEvent(rl_HANDLE ev, debug_info_param info)
{
    static_cast<generic_event*>(ev)->pulse(info);
    return 1;
}



struct mutex_tag_win_cs;
typedef generic_mutex<mutex_tag_win_cs> rl_CRITICAL_SECTION;

inline void rl_InitializeCriticalSection(rl_CRITICAL_SECTION* m, debug_info_param info)
{
    m->init(false, true, false, false, info);
}

inline int rl_InitializeCriticalSectionAndSpinCount(rl_CRITICAL_SECTION* m, unsigned long spin_count, debug_info_param info)
{
    (void)spin_count;
    m->init(false, true, false, false, info);
    return 1;
}

inline int rl_InitializeCriticalSectionEx(rl_CRITICAL_SECTION* m, unsigned long spin_count, unsigned long flags, debug_info_param info)
{
    (void)spin_count;
    (void)flags;
    m->init(false, true, false, false, info);
    return 1;
}

inline void rl_DeleteCriticalSection(rl_CRITICAL_SECTION* m, debug_info_param info)
{
    m->deinit(info);
}

inline void rl_EnterCriticalSection(rl_CRITICAL_SECTION* m, debug_info_param info)
{
    m->lock_exclusive(info);
}

inline int rl_TryEnterCriticalSection(rl_CRITICAL_SECTION* m, debug_info_param info)
{
    return m->try_lock_exclusive(info) ? 1 : 0;
}

inline void rl_LeaveCriticalSection(rl_CRITICAL_SECTION* m, debug_info_param info)
{
    m->unlock_exclusive(info);
}

struct mutex_tag_win_srwl;
typedef generic_mutex<mutex_tag_win_srwl> rl_SRWLOCK;

inline void rl_InitializeSRWLock(rl_SRWLOCK* lock, debug_info_param info)
{
    lock->init(true, false, false, false, info);
}

inline void rl_AcquireSRWLockExclusive(rl_SRWLOCK* lock, debug_info_param info)
{
    lock->lock_exclusive(info);
}

inline void rl_AcquireSRWLockShared(rl_SRWLOCK* lock, debug_info_param info)
{
    lock->lock_shared(info);
}

inline void rl_ReleaseSRWLockExclusive(rl_SRWLOCK* lock, debug_info_param info)
{
    lock->unlock_exclusive(info);
}

inline void rl_ReleaseSRWLockShared(rl_SRWLOCK* lock, debug_info_param info)
{
    lock->unlock_shared(info);
}

//!!!
inline void rl_DeleteSRWLock(rl_SRWLOCK* lock, debug_info_param info)
{
    lock->deinit(info);
}


struct mutex_tag_win_mutex;
typedef generic_mutex<mutex_tag_win_mutex> rl_win_mutex;


inline rl_HANDLE rl_CreateMutex(void* /*security*/, int initial_owner, void const* /*name*/, debug_info_param info)
{
    void* mem = ctx().alloc(sizeof(rl_win_mutex), false, info);
    rl_win_mutex* mtx = new (mem) rl_win_mutex ();
    mtx->init(false, true, false, false, info);
    if (initial_owner)
        mtx->lock_exclusive(info);
    return mtx;
}

inline int rl_ReleaseMutex(rl_HANDLE mtx, debug_info_param info)
{
    static_cast<rl_win_mutex*>(mtx)->unlock_exclusive(info);
    return 1;

}



struct condvar_tag_win;
typedef condvar<condvar_tag_win> rl_CONDITION_VARIABLE;
unsigned long const rl_CONDITION_VARIABLE_LOCKMODE_SHARED = 1;

inline void rl_InitializeConditionVariable(rl_CONDITION_VARIABLE* cv, debug_info_param info)
{
    cv->init(false, info);
}

inline int rl_SleepConditionVariableCS(rl_CONDITION_VARIABLE* cv, rl_CRITICAL_SECTION* cs, unsigned long ms, debug_info_param info)
{
    cv->wait(*cs, ms != rl_INFINITE, info);
    return 0;
}

inline int rl_SleepConditionVariableSRW(rl_CONDITION_VARIABLE* cv, rl_SRWLOCK* lock, unsigned long ms, unsigned long flags, debug_info_param info)
{
    //!!! CONDITION_VARIABLE_LOCKMODE_SHARED
    (void)flags;
    cv->wait(*lock, ms != rl_INFINITE, info);
    return 0;
}

inline void rl_WakeAllConditionVariable(rl_CONDITION_VARIABLE* cv, debug_info_param info)
{
    cv->notify_all(info);
}

inline void rl_WakeConditionVariable(rl_CONDITION_VARIABLE* cv, debug_info_param info)
{
    cv->notify_one(info);
}

inline void rl_DeleteConditionVariable(rl_CONDITION_VARIABLE* cv, debug_info_param info)
{
    cv->deinit(info);
}






typedef unsigned long (RL_STDCALL *rl_WIN_START_ROUTINE)(void* param);
typedef unsigned (RL_STDCALL *rl_MSVCR_THREAD_ROUTINE)(void* param);

template<typename thread_fn_t>
struct win32_thread_helper
{
    thread_fn_t fn;
    void* param;

    static void* thread(void* p)
    {
        win32_thread_helper* self = (win32_thread_helper*)p;
        void* result = (void*)(uintptr_t)(self->fn(self->param));
        delete_impl(self, $);
        return result;
    }
};

inline rl_HANDLE rl_CreateThread(void* security, unsigned stack_size, rl_WIN_START_ROUTINE fn, void* param, unsigned long creation_flags, unsigned long* thread_id, debug_info_param info)
{
    (void)security;
    (void)stack_size;
    (void)creation_flags;
    (void)thread_id;

    void* mem =
        ctx().alloc(sizeof(win32_thread_helper<rl_WIN_START_ROUTINE>), false, info);
    win32_thread_helper<rl_WIN_START_ROUTINE>* arg =
        new (mem) win32_thread_helper<rl_WIN_START_ROUTINE>;
    arg->fn = fn;
    arg->param = param;
    win_waitable_object* handle = ctx().create_thread(&win32_thread_helper<rl_WIN_START_ROUTINE>::thread, arg);
    return handle;
}


inline uintptr_t rl_beginthreadex(void *security, unsigned stack_size, rl_MSVCR_THREAD_ROUTINE start_address, void *arglist, unsigned initflag, unsigned* thrdaddr, debug_info_param info)
{
    (void)security;
    (void)stack_size;
    (void)initflag;
    (void)thrdaddr;

    void* mem = ctx().alloc(sizeof(win32_thread_helper<rl_MSVCR_THREAD_ROUTINE>), false, info);
    win32_thread_helper<rl_MSVCR_THREAD_ROUTINE>* arg =
        new (mem) win32_thread_helper<rl_MSVCR_THREAD_ROUTINE>;
    arg->fn = start_address;
    arg->param = arglist;
    win_waitable_object* handle = ctx().create_thread(&win32_thread_helper<rl_MSVCR_THREAD_ROUTINE>::thread, arg);
    return (uintptr_t)handle;
}

inline unsigned long rl_SetThreadAffinityMask(rl_HANDLE th, unsigned long affinity_mask, debug_info_param info)
{
    (void)(th);
    (void)(affinity_mask);
    (void)info;
    return 0;
}

inline int rl_SuspendThread(rl_HANDLE th, debug_info_param info)
{
    (void)th;
    (void)info;
    return 1;
}

inline int rl_ResumeThread(rl_HANDLE th, debug_info_param info)
{
    (void)th;
    (void)info;
    return 1;
}

inline unsigned long GetLastError()
{
    return (unsigned long)get_errno();
}

inline void SetLastError(unsigned long value)
{
    set_errno((int)value);
}

inline void rl_FlushProcessWriteBuffers(debug_info_param info)
{
    systemwide_fence(info);
}

}


#ifdef HANDLE
#   undef HANDLE
#endif
#define HANDLE rl::rl_HANDLE

#ifdef INFINITE
#   undef INFINITE
#endif
#define INFINITE rl::rl_INFINITE


#ifdef WAIT_FAILED
#   undef WAIT_FAILED
#endif
#define WAIT_FAILED rl::rl_WAIT_FAILED

#ifdef WAIT_OBJECT_0
#   undef WAIT_OBJECT_0
#endif
#define WAIT_OBJECT_0 rl::rl_WAIT_OBJECT_0

#ifdef WAIT_TIMEOUT
#   undef WAIT_TIMEOUT
#endif
#define WAIT_TIMEOUT rl::rl_WAIT_TIMEOUT

#ifdef WAIT_IO_COMPLETION
#   undef WAIT_IO_COMPLETION
#endif
#define WAIT_IO_COMPLETION rl::rl_WAIT_IO_COMPLETION

#ifdef MAXIMUM_WAIT_OBJECTS
#   undef MAXIMUM_WAIT_OBJECTS
#endif
#define MAXIMUM_WAIT_OBJECTS rl::rl_MAXIMUM_WAIT_OBJECTS



#define SwitchToThread() \
 rl::rl_SwitchToThread($)

#define Sleep(milliseconds) \
 rl::rl_Sleep(milliseconds, $)



#define CloseHandle(obj) \
 rl::rl_CloseHandle(obj, $)

#define WaitForSingleObject(obj, timeout) \
 rl::rl_WaitForSingleObject(obj, timeout, $)

#define WaitForMultipleObjects(count, objects, wait_all, timeout) \
 rl::rl_WaitForMultipleObjects(count, objects, wait_all, timeout, $)

#define WaitForMultipleObjectsEx(count, objects, wait_all, timeout, alertable)] \
 rl::rl_WaitForMultipleObjectsEx(count, objects, wait_all, timeout, alertable, $)

#define SignalObjectAndWait(obj_to_signal, obj_to_wait, timeout, alertable) \
 rl::rl_SignalObjectAndWait(obj_to_signal, obj_to_wait, timeout, alertable, $)

#ifdef CreateSemaphore
#   undef CreateSemaphore
#endif

#ifdef CreateSemaphore
#   undef ReleaseSemaphore
#endif

#define CreateSemaphoreA rl_CreateSemaphore
#define CreateSemaphoreW rl_CreateSemaphore
#define CreateSemaphore rl_CreateSemaphore
#define rl_CreateSemaphore(security, initial_count, max_count, name) \
    rl::rl_CreateSemaphore(security, initial_count, max_count, name, $)\

#define ReleaseSemaphore(sema, count, prev_count) \
 rl::rl_ReleaseSemaphore(sema, count, prev_count, $)



#ifdef CreateEvent
#   undef CreateEvent
#endif
#define CreateEventA rl_CreateEvent
#define CreateEventW rl_CreateEvent
#define CreateEvent rl_CreateEvent
#define rl_CreateEvent(security, manual_reset, initial_state, name)\
    rl::rl_CreateEvent(security, manual_reset, initial_state, name, $)

#define SetEvent(ev)\
 rl::rl_SetEvent(ev, $)

#define ResetEvent(ev)\
 rl::rl_ResetEvent(ev, $)

#define PulseEvent(ev)\
 rl::rl_PulseEvent(ev, $)


#ifdef CreateMutex
#   undef CreateMutex
#endif
#define CreateMutexA rl_CreateMutex
#define CreateMutexW rl_CreateMutex
#define CreateMutex rl_CreateMutex
#define rl_CreateMutex(security, initial_owner, name)\
    rl::rl_CreateMutex(security, initial_owner, name, $)

#define ReleaseMutex(mtx)\
 rl::rl_ReleaseMutex(mtx, $)



#define CRITICAL_SECTION rl::rl_CRITICAL_SECTION

#define InitializeCriticalSection(cs) \
 rl::rl_InitializeCriticalSection(cs, $)

#define InitializeCriticalSectionAndSpinCount(cs, spin) \
 rl::rl_InitializeCriticalSectionAndSpinCount(cs, spin, $)

#define InitializeCriticalSectionEx(cs, spin, flags) \
 rl::rl_InitializeCriticalSectionEx(cs, spin, flags, $)

#define DeleteCriticalSection(cs) \
 rl::rl_DeleteCriticalSection(cs, $)

#define EnterCriticalSection(cs) \
 rl::rl_EnterCriticalSection(cs, $)

#define TryEnterCriticalSection(cs) \
 rl::rl_TryEnterCriticalSection(cs, $)

#define LeaveCriticalSection(cs) \
 rl::rl_LeaveCriticalSection(cs, $)




#define SRWLOCK rl::rl_SRWLOCK

#define InitializeSRWLock(lock) \
 rl::rl_InitializeSRWLock(lock, $)

#define AcquireSRWLockExclusive(lock) \
 rl::rl_AcquireSRWLockExclusive(lock, $)

#define AcquireSRWLockShared(lock) \
 rl::rl_AcquireSRWLockShared(lock, $)

#define ReleaseSRWLockExclusive(lock) \
 rl::rl_ReleaseSRWLockExclusive(lock, $)

#define ReleaseSRWLockShared(lock) \
 rl::rl_ReleaseSRWLockShared(lock, $)

//!!! no such function in WIN API
#define DeleteSRWLock(lock) \
 rl::rl_DeleteSRWLock(lock, $)






#define CONDITION_VARIABLE rl::rl_CONDITION_VARIABLE

#ifdef CONDITION_VARIABLE_LOCKMODE_SHARED
#   undef CONDITION_VARIABLE_LOCKMODE_SHARED
#endif
#define CONDITION_VARIABLE_LOCKMODE_SHARED rl::rl_CONDITION_VARIABLE_LOCKMODE_SHARED

#define InitializeConditionVariable(cv) \
 rl::rl_InitializeConditionVariable(cv, $)

#define SleepConditionVariableCS(cv, cs, ms) \
 rl::rl_SleepConditionVariableCS(cv, cs, ms, $)

#define SleepConditionVariableSRW(cv, lock, ms, flags) \
 rl::rl_SleepConditionVariableSRW(cv, lock, ms, flags, $)

#define WakeAllConditionVariable(cv) \
 rl::rl_WakeAllConditionVariable(cv, $)

#define WakeConditionVariable(cv) \
 rl::rl_WakeConditionVariable(cv, $)

//!!! no such function in WIN API
#define DeleteConditionVariable(cv) \
 rl::rl_DeleteConditionVariable(cv, $)



#define CreateThread(security, stack_size, fn, param, creation_flags, thread_id) \
 rl::rl_CreateThread(security, stack_size, fn, param, creation_flags, thread_id, $)

#define _beginthreadex(security, stack_size, start_address, arglist, initflag, thrdaddr) \
  rl::rl_beginthreadex(security, stack_size, start_address, arglist, initflag, thrdaddr, $)

#define SetThreadAffinityMask(th, affinity_mask) \
 rl::rl_SetThreadAffinityMask(th, affinity_mask, $)

#define SuspendThread(th) \
 rl::rl_SuspendThread(th, $)

#define ResumeThread(th) \
 rl::rl_ResumeThread(th, $)

#define FlushProcessWriteBuffers() \
 rl::rl_FlushProcessWriteBuffers($)


#endif
