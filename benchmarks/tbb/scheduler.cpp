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

#include "custom_scheduler.h"
#include "scheduler_utility.h"
#include "governor.h"
#include "market.h"
#include "arena.h"
#include "mailbox.h"
#include "observer_proxy.h"
#include "tbb/tbb_machine.h"
#include "tbb/atomic.h"

namespace tbb {
namespace internal {

//------------------------------------------------------------------------
// Library initialization
//------------------------------------------------------------------------

/** Defined in tbb_main.cpp **/
extern generic_scheduler* (*AllocateSchedulerPtr)( arena*, size_t index );

inline generic_scheduler* allocate_scheduler ( arena* a, size_t index ) {
    return AllocateSchedulerPtr(a, index);
}

#if __TBB_TASK_GROUP_CONTEXT
context_state_propagation_mutex_type the_context_state_propagation_mutex;

uintptr_t the_context_state_propagation_epoch = 0;

//! Context to be associated with dummy tasks of worker threads schedulers.
/** It is never used for its direct purpose, and is introduced solely for the sake
    of avoiding one extra conditional branch in the end of wait_for_all method. **/
static task_group_context the_dummy_context(task_group_context::isolated);
#endif /* __TBB_TASK_GROUP_CONTEXT */

void Scheduler_OneTimeInitialization ( bool itt_present ) {
    AllocateSchedulerPtr = itt_present ? &custom_scheduler<DefaultSchedulerTraits>::allocate_scheduler :
                                      &custom_scheduler<IntelSchedulerTraits>::allocate_scheduler;
#if __TBB_TASK_GROUP_CONTEXT
    // There must be no tasks belonging to this fake task group. Mark invalid for the assert
    __TBB_ASSERT(!(task_group_context::low_unused_state_bit & (task_group_context::low_unused_state_bit-1)), NULL);
    the_dummy_context.my_state = task_group_context::low_unused_state_bit;
#if __TBB_TASK_PRIORITY
    // It should never prevent tasks from being passed to execution.
    the_dummy_context.my_priority = num_priority_levels - 1;
#endif /* __TBB_TASK_PRIORITY */
#endif /* __TBB_TASK_GROUP_CONTEXT */
}

//------------------------------------------------------------------------
// scheduler interface
//------------------------------------------------------------------------

//  A pure virtual destructor should still have a body
//  so the one for tbb::internal::scheduler::~scheduler() is provided here
scheduler::~scheduler( ) {}

//------------------------------------------------------------------------
// generic_scheduler
//------------------------------------------------------------------------

#if _MSC_VER && !defined(__INTEL_COMPILER)
    // Suppress overzealous compiler warning about using 'this' in base initializer list.
    #pragma warning(push)
    #pragma warning(disable:4355)
#endif

generic_scheduler::generic_scheduler( arena* a, size_t index )
    : my_stealing_threshold(0)
    , my_market(NULL)
    , my_random( this )
    , my_free_list(NULL)
#if __TBB_HOARD_NONLOCAL_TASKS
    , my_nonlocal_free_list(NULL)
#endif
    , my_dummy_task(NULL)
    , my_ref_count(1)
    , my_auto_initialized(false)
#if __TBB_COUNT_TASK_NODES
    , my_task_node_count(0)
#endif /* __TBB_COUNT_TASK_NODES */
    , my_small_task_count(1)   // Extra 1 is a guard reference
    , my_return_list(NULL)
#if __TBB_TASK_GROUP_CONTEXT
    , my_local_ctx_list_update(make_atomic(uintptr_t(0)))
#endif /* __TBB_TASK_GROUP_CONTEXT */
#if __TBB_TASK_PRIORITY
    , my_offloaded_tasks(NULL)
    , my_offloaded_task_list_tail_link(NULL)
    , my_local_reload_epoch(0)
    , my_pool_reshuffling_pending(false)
#endif /* __TBB_TASK_PRIORITY */
#if __TBB_TASK_GROUP_CONTEXT
    , my_nonlocal_ctx_list_update(make_atomic(uintptr_t(0)))
#endif /* __TBB_TASK_GROUP_CONTEXT */
#if __TBB_SURVIVE_THREAD_SWITCH && TBB_USE_ASSERT
    , my_cilk_state(cs_none)
#endif /* __TBB_SURVIVE_THREAD_SWITCH && TBB_USE_ASSERT */
{
    my_arena_index = index;
    my_arena_slot = 0;
    my_arena = a;
    my_innermost_running_task = NULL;
    my_dispatching_task = NULL;
    my_affinity_id = 0;
#if __TBB_SCHEDULER_OBSERVER
    my_last_global_observer = NULL;
    my_last_local_observer = NULL;
#endif /* __TBB_SCHEDULER_OBSERVER */
#if __TBB_TASK_PRIORITY
    my_ref_top_priority = NULL;
    my_ref_reload_epoch = NULL;
#endif /* __TBB_TASK_PRIORITY */

    my_dummy_task = &allocate_task( sizeof(task), __TBB_CONTEXT_ARG(NULL, NULL) );
#if __TBB_TASK_GROUP_CONTEXT
    my_context_list_head.my_prev = &my_context_list_head;
    my_context_list_head.my_next = &my_context_list_head;
    ITT_SYNC_CREATE(&my_context_list_mutex, SyncType_Scheduler, SyncObj_ContextsList);
#endif /* __TBB_TASK_GROUP_CONTEXT */
    my_dummy_task->prefix().ref_count = 2;
    ITT_SYNC_CREATE(&my_dummy_task->prefix().ref_count, SyncType_Scheduler, SyncObj_WorkerLifeCycleMgmt);
    ITT_SYNC_CREATE(&my_return_list, SyncType_Scheduler, SyncObj_TaskReturnList);
    assert_task_pool_valid();
#if __TBB_SURVIVE_THREAD_SWITCH
    my_cilk_unwatch_thunk.routine = NULL;
#endif /* __TBB_SURVIVE_THREAD_SWITCH */
}

#if _MSC_VER && !defined(__INTEL_COMPILER)
    #pragma warning(pop)
#endif // warning 4355 is back

#if TBB_USE_ASSERT > 1
void generic_scheduler::assert_task_pool_valid() const {
    acquire_task_pool();
    task** tp = my_arena_slot->task_pool_ptr;
    __TBB_ASSERT( my_arena_slot->my_task_pool_size >= min_task_pool_size, NULL );
    const size_t H = __TBB_load_relaxed(my_arena_slot->head); // mirror
    const size_t T = __TBB_load_relaxed(my_arena_slot->tail); // mirror
    __TBB_ASSERT( H <= T, NULL );
    for ( size_t i = 0; i < H; ++i )
        __TBB_ASSERT( tp[i] == poisoned_ptr, "Task pool corrupted" );
    for ( size_t i = H; i < T; ++i ) {
        __TBB_ASSERT( (uintptr_t)tp[i] + 1 > 1u, "nil or invalid task pointer in the deque" );
        __TBB_ASSERT( tp[i]->prefix().state == task::ready ||
                      tp[i]->prefix().extra_state == es_task_proxy, "task in the deque has invalid state" );
    }
    for ( size_t i = T; i < my_arena_slot->my_task_pool_size; ++i )
        __TBB_ASSERT( tp[i] == poisoned_ptr, "Task pool corrupted" );
    release_task_pool();
}
#endif /* TBB_USE_ASSERT > 1 */

void generic_scheduler::init_stack_info () {
    // Stacks are growing top-down. Highest address is called "stack base",
    // and the lowest is "stack limit".
    __TBB_ASSERT( !my_stealing_threshold, "Stealing threshold has already been calculated" );
    size_t  stack_size = my_market->worker_stack_size();
#if USE_WINTHREAD
#if defined(_MSC_VER)&&_MSC_VER<1400 && !_WIN64
    NT_TIB  *pteb = (NT_TIB*)__TBB_machine_get_current_teb();
#else
    NT_TIB  *pteb = (NT_TIB*)NtCurrentTeb();
#endif
    __TBB_ASSERT( &pteb < pteb->StackBase && &pteb > pteb->StackLimit, "invalid stack info in TEB" );
    __TBB_ASSERT( stack_size >0, "stack_size not initialized?" );
    // When a thread is created with the attribute STACK_SIZE_PARAM_IS_A_RESERVATION, stack limit
    // in the TIB points to the committed part of the stack only. This renders the expression
    // "(uintptr_t)pteb->StackBase / 2 + (uintptr_t)pteb->StackLimit / 2" virtually useless.
    // Thus for worker threads we use the explicit stack size we used while creating them.
    // And for master threads we rely on the following fact and assumption:
    // - the default stack size of a master thread on Windows is 1M;
    // - if it was explicitly set by the application it is at least as large as the size of a worker stack.
    if ( is_worker() || stack_size < MByte )
        my_stealing_threshold = (uintptr_t)pteb->StackBase - stack_size / 2;
    else
        my_stealing_threshold = (uintptr_t)pteb->StackBase - MByte / 2;
#else /* USE_PTHREAD */
    // There is no portable way to get stack base address in Posix, so we use
    // non-portable method (on all modern Linux) or the simplified approach
    // based on the common sense assumptions. The most important assumption
    // is that the main thread's stack size is not less than that of other threads.
    // See also comment 3 at the end of this file
    void    *stack_base = &stack_size;
#if __linux__ && !__bg__
#if __TBB_ipf
    void    *rsb_base = __TBB_get_bsp();
#endif
    size_t  np_stack_size = 0;
    void    *stack_limit = NULL;
    pthread_attr_t  np_attr_stack;
    if( 0 == pthread_getattr_np(pthread_self(), &np_attr_stack) ) {
        if ( 0 == pthread_attr_getstack(&np_attr_stack, &stack_limit, &np_stack_size) ) {
#if __TBB_ipf
            pthread_attr_t  attr_stack;
            if ( 0 == pthread_attr_init(&attr_stack) ) {
                if ( 0 == pthread_attr_getstacksize(&attr_stack, &stack_size) ) {
                    if ( np_stack_size < stack_size ) {
                        // We are in a secondary thread. Use reliable data.
                        // IA-64 architecture stack is split into RSE backup and memory parts
                        rsb_base = stack_limit;
                        stack_size = np_stack_size/2;
                        // Limit of the memory part of the stack
                        stack_limit = (char*)stack_limit + stack_size;
                    }
                    // We are either in the main thread or this thread stack
                    // is bigger that that of the main one. As we cannot discern
                    // these cases we fall back to the default (heuristic) values.
                }
                pthread_attr_destroy(&attr_stack);
            }
            // IA-64 architecture stack is split into RSE backup and memory parts
            my_rsb_stealing_threshold = (uintptr_t)((char*)rsb_base + stack_size/2);
#endif /* __TBB_ipf */
            // Size of the stack free part 
            stack_size = size_t((char*)stack_base - (char*)stack_limit);
        }
        pthread_attr_destroy(&np_attr_stack);
    }
#endif /* __linux__ */
    __TBB_ASSERT( stack_size>0, "stack size must be positive" );
    my_stealing_threshold = (uintptr_t)((char*)stack_base - stack_size/2);
#endif /* USE_PTHREAD */
}

#if __TBB_TASK_GROUP_CONTEXT
/** The function uses synchronization scheme similar to the one in the destructor
    of task_group_context augmented with interlocked state change of each context
    object. The purpose of this algo is to prevent threads doing nonlocal context
    destruction from accessing destroyed owner-scheduler instance still pointed to
    by the context object. **/
void generic_scheduler::cleanup_local_context_list () {
    // Detach contexts remaining in the local list
    bool wait_for_concurrent_destroyers_to_leave = false;
    uintptr_t local_count_snapshot = my_context_state_propagation_epoch;
    my_local_ctx_list_update.store<relaxed>(1);
    {
        // This is just a definition. Actual lock is acquired only in case of conflict.
        spin_mutex::scoped_lock lock;
        // Full fence prevents reordering of store to my_local_ctx_list_update with
        // load from my_nonlocal_ctx_list_update.
        atomic_fence();
        // Check for the conflict with concurrent destroyer or cancellation propagator
        if ( my_nonlocal_ctx_list_update.load<relaxed>() || local_count_snapshot != the_context_state_propagation_epoch )
            lock.acquire(my_context_list_mutex);
        // No acquire fence is necessary for loading my_context_list_head.my_next,
        // as the list can be updated by this thread only.
        context_list_node_t *node = my_context_list_head.my_next;
        while ( node != &my_context_list_head ) {
            task_group_context &ctx = __TBB_get_object_ref(task_group_context, my_node, node);
            __TBB_ASSERT( __TBB_load_relaxed(ctx.my_kind) != task_group_context::binding_required, "Only a context bound to a root task can be detached" );
            node = node->my_next;
            __TBB_ASSERT( is_alive(ctx.my_version_and_traits), "Walked into a destroyed context while detaching contexts from the local list" );
            // Synchronizes with ~task_group_context(). TODO: evaluate and perhaps relax
            if ( internal::as_atomic(ctx.my_kind).fetch_and_store(task_group_context::detached) == task_group_context::dying )
                wait_for_concurrent_destroyers_to_leave = true;
        }
    }
    my_local_ctx_list_update.store<release>(0);
    // Wait until other threads referencing this scheduler object finish with it
    if ( wait_for_concurrent_destroyers_to_leave )
        spin_wait_until_eq( my_nonlocal_ctx_list_update, 0u );
}
#endif /* __TBB_TASK_GROUP_CONTEXT */

void generic_scheduler::free_scheduler() {
    __TBB_ASSERT( !my_arena_slot, NULL );
#if __TBB_TASK_GROUP_CONTEXT
    cleanup_local_context_list();
#endif /* __TBB_TASK_GROUP_CONTEXT */
    free_task<small_local_task>( *my_dummy_task );

#if __TBB_HOARD_NONLOCAL_TASKS
    while( task* t = my_nonlocal_free_list ) {
        task_prefix& p = t->prefix();
        my_nonlocal_free_list = p.next;
        __TBB_ASSERT( p.origin && p.origin!=this, NULL );
        free_nonlocal_small_task(*t);
    }
#endif
    // k accounts for a guard reference and each task that we deallocate.
    intptr_t k = 1;
    for(;;) {
        while( task* t = my_free_list ) {
            my_free_list = t->prefix().next;
            deallocate_task(*t);
            ++k;
        }
        if( my_return_list==plugged_return_list() )
            break;
        my_free_list = (task*)__TBB_FetchAndStoreW( &my_return_list, (intptr_t)plugged_return_list() );
    }
#if __TBB_COUNT_TASK_NODES
    my_market->update_task_node_count( my_task_node_count );
#endif /* __TBB_COUNT_TASK_NODES */
    // Update my_small_task_count last.  Doing so sooner might cause another thread to free *this.
    __TBB_ASSERT( my_small_task_count>=k, "my_small_task_count corrupted" );
    governor::sign_off(this);
    if( __TBB_FetchAndAddW( &my_small_task_count, -k )==k )
        NFS_Free( this );
}

task& generic_scheduler::allocate_task( size_t number_of_bytes,
                                            __TBB_CONTEXT_ARG(task* parent, task_group_context* context) ) {
    GATHER_STATISTIC(++my_counters.active_tasks);
    task *t;
    if( number_of_bytes<=quick_task_size ) {
#if __TBB_HOARD_NONLOCAL_TASKS
        if( (t = my_nonlocal_free_list) ) {
            GATHER_STATISTIC(--my_counters.free_list_length);
            __TBB_ASSERT( t->state()==task::freed, "free list of tasks is corrupted" );
            my_nonlocal_free_list = t->prefix().next;
        } else
#endif
        if( (t = my_free_list) ) {
            GATHER_STATISTIC(--my_counters.free_list_length);
            __TBB_ASSERT( t->state()==task::freed, "free list of tasks is corrupted" );
            my_free_list = t->prefix().next;
        } else if( my_return_list ) {
            // No fence required for read of my_return_list above, because __TBB_FetchAndStoreW has a fence.
            t = (task*)__TBB_FetchAndStoreW( &my_return_list, 0 ); // with acquire
            __TBB_ASSERT( t, "another thread emptied the my_return_list" );
            __TBB_ASSERT( t->prefix().origin==this, "task returned to wrong my_return_list" );
            ITT_NOTIFY( sync_acquired, &my_return_list );
            my_free_list = t->prefix().next;
        } else {
            t = (task*)((char*)NFS_Allocate( 1, task_prefix_reservation_size+quick_task_size, NULL ) + task_prefix_reservation_size );
#if __TBB_COUNT_TASK_NODES
            ++my_task_node_count;
#endif /* __TBB_COUNT_TASK_NODES */
            t->prefix().origin = this;
            t->prefix().next = 0;
            ++my_small_task_count;
        }
#if __TBB_PREFETCHING
        task *t_next = t->prefix().next;
        if( !t_next ) { // the task was last in the list
#if __TBB_HOARD_NONLOCAL_TASKS
            if( my_free_list )
                t_next = my_free_list;
            else
#endif
            if( my_return_list ) // enable prefetching, gives speedup
                t_next = my_free_list = (task*)__TBB_FetchAndStoreW( &my_return_list, 0 );
        }
        if( t_next ) { // gives speedup for both cache lines
            __TBB_cl_prefetch(t_next);
            __TBB_cl_prefetch(&t_next->prefix());
        }
#endif /* __TBB_PREFETCHING */
    } else {
        GATHER_STATISTIC(++my_counters.big_tasks);
        t = (task*)((char*)NFS_Allocate( 1, task_prefix_reservation_size+number_of_bytes, NULL ) + task_prefix_reservation_size );
#if __TBB_COUNT_TASK_NODES
        ++my_task_node_count;
#endif /* __TBB_COUNT_TASK_NODES */
        t->prefix().origin = NULL;
    }
    task_prefix& p = t->prefix();
#if __TBB_TASK_GROUP_CONTEXT
    p.context = context;
#endif /* __TBB_TASK_GROUP_CONTEXT */
    // Obsolete. But still in use, so has to be assigned correct value here.
    p.owner = this;
    p.ref_count = 0;
    // Obsolete. Assign some not outrageously out-of-place value for a while.
    p.depth = 0;
    p.parent = parent;
    // In TBB 2.1 and later, the constructor for task sets extra_state to indicate the version of the tbb/task.h header.
    // In TBB 2.0 and earlier, the constructor leaves extra_state as zero.
    p.extra_state = 0;
    p.affinity = 0;
    p.state = task::allocated;
    return *t;
}

void generic_scheduler::free_nonlocal_small_task( task& t ) {
    __TBB_ASSERT( t.state()==task::freed, NULL );
    generic_scheduler& s = *static_cast<generic_scheduler*>(t.prefix().origin);
    __TBB_ASSERT( &s!=this, NULL );
    for(;;) {
        task* old = s.my_return_list;
        if( old==plugged_return_list() )
            break;
        // Atomically insert t at head of s.my_return_list
        t.prefix().next = old;
        ITT_NOTIFY( sync_releasing, &s.my_return_list );
        if( as_atomic(s.my_return_list).compare_and_swap(&t, old )==old ) {
#if __TBB_PREFETCHING
            __TBB_cl_evict(&t.prefix());
            __TBB_cl_evict(&t);
#endif
            return;
        }
    }
    deallocate_task(t);
    if( __TBB_FetchAndDecrementWrelease( &s.my_small_task_count )==1 ) {
        // We freed the last task allocated by scheduler s, so it's our responsibility
        // to free the scheduler.
        NFS_Free( &s );
    }
}

size_t generic_scheduler::prepare_task_pool ( size_t num_tasks ) {
    size_t T = __TBB_load_relaxed(my_arena_slot->tail); // mirror
    if ( T + num_tasks <= my_arena_slot->my_task_pool_size )
        return T;
    acquire_task_pool();
    size_t H = __TBB_load_relaxed(my_arena_slot->head); // mirror
    T -= H;
    size_t new_size = T + num_tasks;
    __TBB_ASSERT(!my_arena_slot->my_task_pool_size || my_arena_slot->my_task_pool_size >= min_task_pool_size, NULL);
    if( !my_arena_slot->my_task_pool_size ) {
        __TBB_ASSERT( !in_arena() && !my_arena_slot->task_pool_ptr, NULL );
        if( new_size < min_task_pool_size ) new_size = min_task_pool_size;
        my_arena_slot->allocate_task_pool( new_size );
    }
    // If the free space at the beginning of the task pool is too short, we
    // are likely facing a pathological single-producer-multiple-consumers
    // scenario, and thus it's better to expand the task pool
    else if ( new_size <= my_arena_slot->my_task_pool_size - min_task_pool_size/4 ) {
        // Relocate the busy part to the beginning of the deque
        memmove( my_arena_slot->task_pool_ptr, my_arena_slot->task_pool_ptr + H, T * sizeof(task*) );
        my_arena_slot->fill_with_canary_pattern( T, my_arena_slot->tail );
        commit_relocated_tasks(T);
    }
    else {
        // Grow task pool. As this operation is rare, and its cost is asymptotically
        // amortizable, we can tolerate new task pool allocation done under the lock.
        if ( new_size < 2 * my_arena_slot->my_task_pool_size )
            new_size = 2 * my_arena_slot->my_task_pool_size;
        task** old_pool = my_arena_slot->task_pool_ptr;
        my_arena_slot->allocate_task_pool( new_size ); // updates my_task_pool_size
        __TBB_ASSERT( T <= my_arena_slot->my_task_pool_size, "new task pool is too short" );
        memcpy( my_arena_slot->task_pool_ptr, old_pool + H, T * sizeof(task*) );
        commit_relocated_tasks(T);
        __TBB_ASSERT( old_pool, "attempt to free NULL TaskPool" );
        NFS_Free( old_pool );
    }
    assert_task_pool_valid();
    return T;
}

/** ATTENTION:
    This method is mostly the same as generic_scheduler::lock_task_pool(), with
    a little different logic of slot state checks (slot is either locked or points
    to our task pool).
    Thus if either of them is changed, consider changing the counterpart as well. **/
inline void generic_scheduler::acquire_task_pool() const {
    if ( !in_arena() )
        return; // we are not in arena - nothing to lock
    bool sync_prepare_done = false;
    for( atomic_backoff b;;b.pause() ) {
#if TBB_USE_ASSERT
        __TBB_ASSERT( my_arena_slot == my_arena->my_slots + my_arena_index, "invalid arena slot index" );
        // Local copy of the arena slot task pool pointer is necessary for the next
        // assertion to work correctly to exclude asynchronous state transition effect.
        task** tp = my_arena_slot->task_pool;
        __TBB_ASSERT( tp == LockedTaskPool || tp == my_arena_slot->task_pool_ptr, "slot ownership corrupt?" );
#endif
        if( my_arena_slot->task_pool != LockedTaskPool &&
            as_atomic(my_arena_slot->task_pool).compare_and_swap(LockedTaskPool, my_arena_slot->task_pool_ptr ) == my_arena_slot->task_pool_ptr )
        {
            // We acquired our own slot
            ITT_NOTIFY(sync_acquired, my_arena_slot);
            break;
        }
        else if( !sync_prepare_done ) {
            // Start waiting
            ITT_NOTIFY(sync_prepare, my_arena_slot);
            sync_prepare_done = true;
        }
        // Someone else acquired a lock, so pause and do exponential backoff.
    }
    __TBB_ASSERT( my_arena_slot->task_pool == LockedTaskPool, "not really acquired task pool" );
} // generic_scheduler::acquire_task_pool

inline void generic_scheduler::release_task_pool() const {
    if ( !in_arena() )
        return; // we are not in arena - nothing to unlock
    __TBB_ASSERT( my_arena_slot, "we are not in arena" );
    __TBB_ASSERT( my_arena_slot->task_pool == LockedTaskPool, "arena slot is not locked" );
    ITT_NOTIFY(sync_releasing, my_arena_slot);
    __TBB_store_with_release( my_arena_slot->task_pool, my_arena_slot->task_pool_ptr );
}

/** ATTENTION:
    This method is mostly the same as generic_scheduler::acquire_task_pool(),
    with a little different logic of slot state checks (slot can be empty, locked
    or point to any task pool other than ours, and asynchronous transitions between
    all these states are possible).
    Thus if any of them is changed, consider changing the counterpart as well **/
inline task** generic_scheduler::lock_task_pool( arena_slot* victim_arena_slot ) const {
    task** victim_task_pool;
    bool sync_prepare_done = false;
    for( atomic_backoff backoff;; /*backoff pause embedded in the loop*/) {
        victim_task_pool = victim_arena_slot->task_pool;
        // NOTE: Do not use comparison of head and tail indices to check for
        // the presence of work in the victim's task pool, as they may give
        // incorrect indication because of task pool relocations and resizes.
        if ( victim_task_pool == EmptyTaskPool ) {
            // The victim thread emptied its task pool - nothing to lock
            if( sync_prepare_done )
                ITT_NOTIFY(sync_cancel, victim_arena_slot);
            break;
        }
        if( victim_task_pool != LockedTaskPool &&
            as_atomic(victim_arena_slot->task_pool).compare_and_swap(LockedTaskPool, victim_task_pool ) == victim_task_pool )
        {
            // We've locked victim's task pool
            ITT_NOTIFY(sync_acquired, victim_arena_slot);
            break;
        }
        else if( !sync_prepare_done ) {
            // Start waiting
            ITT_NOTIFY(sync_prepare, victim_arena_slot);
            sync_prepare_done = true;
        }
        GATHER_STATISTIC( ++my_counters.thieves_conflicts );
        // Someone else acquired a lock, so pause and do exponential backoff.
#if __TBB_STEALING_ABORT_ON_CONTENTION
        if(!backoff.bounded_pause()) {
            // the 16 was acquired empirically and a theory behind it supposes
            // that number of threads becomes much bigger than number of
            // tasks which can be spawned by one thread causing excessive contention.
            // TODO: However even small arenas can benefit from the abort on contention
            //       if preemption of a thief is a problem
            if(my_arena->my_limit >= 16)
                return EmptyTaskPool;
            __TBB_Yield();
        }
#else
        backoff.pause();
#endif
    }
    __TBB_ASSERT( victim_task_pool == EmptyTaskPool ||
                  (victim_arena_slot->task_pool == LockedTaskPool && victim_task_pool != LockedTaskPool),
                  "not really locked victim's task pool?" );
    return victim_task_pool;
} // generic_scheduler::lock_task_pool

inline void generic_scheduler::unlock_task_pool( arena_slot* victim_arena_slot,
                                                task** victim_task_pool ) const {
    __TBB_ASSERT( victim_arena_slot, "empty victim arena slot pointer" );
    __TBB_ASSERT( victim_arena_slot->task_pool == LockedTaskPool, "victim arena slot is not locked" );
    ITT_NOTIFY(sync_releasing, victim_arena_slot);
    __TBB_store_with_release( victim_arena_slot->task_pool, victim_task_pool );
}


inline task* generic_scheduler::prepare_for_spawning( task* t ) {
    __TBB_ASSERT( t->state()==task::allocated, "attempt to spawn task that is not in 'allocated' state" );
    t->prefix().state = task::ready;
#if TBB_USE_ASSERT
    if( task* parent = t->parent() ) {
        internal::reference_count ref_count = parent->prefix().ref_count;
        __TBB_ASSERT( ref_count>=0, "attempt to spawn task whose parent has a ref_count<0" );
        __TBB_ASSERT( ref_count!=0, "attempt to spawn task whose parent has a ref_count==0 (forgot to set_ref_count?)" );
        parent->prefix().extra_state |= es_ref_count_active;
    }
#endif /* TBB_USE_ASSERT */
    affinity_id dst_thread = t->prefix().affinity;
    __TBB_ASSERT( dst_thread == 0 || is_version_3_task(*t),
                  "backwards compatibility to TBB 2.0 tasks is broken" );
    if( dst_thread != 0 && dst_thread != my_affinity_id ) {
        task_proxy& proxy = (task_proxy&)allocate_task( sizeof(task_proxy),
                                                      __TBB_CONTEXT_ARG(NULL, NULL) );
        // Mark as a proxy
        proxy.prefix().extra_state = es_task_proxy;
        proxy.outbox = &my_arena->mailbox(dst_thread);
        // Mark proxy as present in both locations (sender's task pool and destination mailbox)
        proxy.task_and_tag = intptr_t(t) | task_proxy::location_mask;
#if __TBB_TASK_PRIORITY
        proxy.prefix().context = t->prefix().context;
#endif /* __TBB_TASK_PRIORITY */
        ITT_NOTIFY( sync_releasing, proxy.outbox );
        // Mail the proxy - after this point t may be destroyed by another thread at any moment.
        proxy.outbox->push(proxy);
        return &proxy;
    }
    return t;
}

/** Conceptually, this method should be a member of class scheduler.
    But doing so would force us to publish class scheduler in the headers. */
void generic_scheduler::local_spawn( task& first, task*& next ) {
    __TBB_ASSERT( governor::is_set(this), NULL );
    if ( &first.prefix().next == &next ) {
        // Single task is being spawned
        size_t T = prepare_task_pool( 1 );
        my_arena_slot->task_pool_ptr[T] = prepare_for_spawning( &first );
        commit_spawned_tasks( T + 1 );
    }
    else {
        // Task list is being spawned
        task *arr[min_task_pool_size];
        fast_reverse_vector<task*> tasks(arr, min_task_pool_size);
        task *t_next = NULL;
        for( task* t = &first; ; t = t_next ) {
            // If t is affinitized to another thread, it may already be executed
            // and destroyed by the time prepare_for_spawning returns.
            // So milk it while it is alive.
            bool end = &t->prefix().next == &next;
            t_next = t->prefix().next;
            tasks.push_back( prepare_for_spawning(t) );
            if( end )
                break;
        }
        size_t num_tasks = tasks.size();
        size_t T = prepare_task_pool( num_tasks );
        tasks.copy_memory( my_arena_slot->task_pool_ptr + T );
        commit_spawned_tasks( T + num_tasks );
    }
    if ( !in_arena() )
        enter_arena();
    my_arena->advertise_new_work</*Spawned=*/true>();
    assert_task_pool_valid();
}

void generic_scheduler::local_spawn_root_and_wait( task& first, task*& next ) {
    __TBB_ASSERT( governor::is_set(this), NULL );
    __TBB_ASSERT( &first, NULL );
    auto_empty_task dummy( __TBB_CONTEXT_ARG(this, first.prefix().context) );
    internal::reference_count n = 0;
    for( task* t=&first; ; t=t->prefix().next ) {
        ++n;
        __TBB_ASSERT( !t->prefix().parent, "not a root task, or already running" );
        t->prefix().parent = &dummy;
        if( &t->prefix().next==&next ) break;
#if __TBB_TASK_GROUP_CONTEXT
        __TBB_ASSERT( t->prefix().context == t->prefix().next->prefix().context,
                    "all the root tasks in list must share the same context");
#endif /* __TBB_TASK_GROUP_CONTEXT */
    }
    dummy.prefix().ref_count = n+1;
    if( n>1 )
        local_spawn( *first.prefix().next, next );
    local_wait_for_all( dummy, &first );
}

void tbb::internal::generic_scheduler::spawn( task& first, task*& next ) {
    governor::local_scheduler()->local_spawn( first, next );
}

void tbb::internal::generic_scheduler::spawn_root_and_wait( task& first, task*& next ) {
    governor::local_scheduler()->local_spawn_root_and_wait( first, next );
}

void tbb::internal::generic_scheduler::enqueue( task& t, void* prio ) {
    generic_scheduler *s = governor::local_scheduler();
    // these redirections are due to bw-compatibility, consider reworking some day
    __TBB_ASSERT( s->my_arena, "thread is not in any arena" );
    s->my_arena->enqueue_task(t, (intptr_t)prio, s->my_random );
}

#if __TBB_TASK_PRIORITY
class auto_indicator : no_copy {
    volatile bool& my_indicator;
public:
    auto_indicator ( volatile bool& indicator ) : my_indicator(indicator) { my_indicator = true ;}
    ~auto_indicator () { my_indicator = false; }
};

task* generic_scheduler::winnow_task_pool () {
    GATHER_STATISTIC( ++my_counters.prio_winnowings );
    __TBB_ASSERT( in_arena(), NULL );
    __TBB_ASSERT( my_offloaded_tasks, "At least one task is expected to be already offloaded" );
    // To eliminate possible sinking of the store to the indicator below the subsequent
    // store to my_arena_slot->tail, the stores should have either been separated
    // by full fence or both use release fences. And resetting indicator should have
    // been done with release fence. But since this is just an optimization, and
    // the corresponding checking sequence in arena::is_out_of_work() is not atomic
    // anyway, fences aren't used, so that not to penalize warmer path.
    auto_indicator indicator(my_pool_reshuffling_pending);
    // The purpose of the synchronization algorithm here is for the owner thread
    // to avoid locking task pool most of the time.
    size_t T0 = __TBB_load_relaxed(my_arena_slot->tail);
    __TBB_store_relaxed( my_arena_slot->tail, __TBB_load_relaxed(my_arena_slot->head) - 1 );
    atomic_fence();
    size_t H = __TBB_load_relaxed(my_arena_slot->head);
    size_t T = __TBB_load_relaxed(my_arena_slot->tail);
    __TBB_ASSERT( (intptr_t)T <= (intptr_t)T0, NULL);
    __TBB_ASSERT( (intptr_t)H >= (intptr_t)T || (H == T0 && T == T0), NULL );
    bool acquired = false;
    if ( H == T ) {
        // Either no contention with thieves during arbitration protocol execution or ...
        if ( H >= T0 ) {
            // ... the task pool got empty
            reset_deque_and_leave_arena( /*locked=*/false );
            return NULL;
        }
    }
    else {
        // Contention with thieves detected. Now without taking lock it is impossible
        // to define the current head value because of its jitter caused by continuing
        // stealing attempts (the pool is not locked so far).
        acquired = true;
        acquire_task_pool();
        H = __TBB_load_relaxed(my_arena_slot->head);
        if ( H >= T0 ) {
            reset_deque_and_leave_arena( /*locked=*/true );
            return NULL;
        }
    }
    size_t src,
           dst = T0;
    // Find the first task to offload.
    for ( src = H; src < T0; ++src ) {
        task &t = *my_arena_slot->task_pool_ptr[src];
        intptr_t p = priority(t);
        if ( p < *my_ref_top_priority ) {
            // Position of the first offloaded task will be the starting point
            // for relocation of subsequent tasks that survive winnowing.
            dst = src;
            offload_task( t, p );
            break;
        }
    }
    for ( ++src; src < T0; ++src ) {
        task &t = *my_arena_slot->task_pool_ptr[src];
        intptr_t p = priority(t);
        if ( p < *my_ref_top_priority )
            offload_task( t, p );
        else
            my_arena_slot->task_pool_ptr[dst++] = &t;
    }
    __TBB_ASSERT( T0 >= dst, NULL );
    task *t = H < dst ? my_arena_slot->task_pool_ptr[--dst] : NULL;
    if ( H == dst ) {
        // No tasks remain the primary pool
        reset_deque_and_leave_arena( acquired );
    }
    else if ( acquired ) {
        __TBB_ASSERT( !is_poisoned(my_arena_slot->task_pool_ptr[H]), NULL );
        __TBB_store_relaxed( my_arena_slot->tail, dst );
        release_task_pool();
    }
    else {
        __TBB_ASSERT( !is_poisoned(my_arena_slot->task_pool_ptr[H]), NULL );
        // Release fence is necessary to make sure possibly relocated task pointers
        // become visible to potential thieves
        __TBB_store_with_release( my_arena_slot->tail, dst );
    }
    my_arena_slot->fill_with_canary_pattern( dst, T0 );
    assert_task_pool_valid();
    return t;
}

task* generic_scheduler::reload_tasks ( task*& offloaded_tasks, task**& offloaded_task_list_link, intptr_t top_priority ) {
    GATHER_STATISTIC( ++my_counters.prio_reloads );
    __TBB_ASSERT( !in_arena(), NULL );
    task *arr[min_task_pool_size];
    fast_reverse_vector<task*> tasks(arr, min_task_pool_size);
    task **link = &offloaded_tasks;
    task *t;
    while ( (t = *link) ) {
        task** next_ptr = &t->prefix().next_offloaded;
        if ( priority(*t) >= top_priority ) {
            tasks.push_back( t );
            // Note that owner is an alias of next_offloaded. Thus the following
            // assignment overwrites *next_ptr
            task* next = *next_ptr;
            t->prefix().owner = this;
            __TBB_ASSERT( t->prefix().state == task::ready || t->prefix().extra_state == es_task_proxy, NULL );
            *link = next;
        }
        else {
            link = next_ptr;
        }
    }
    if ( link == &offloaded_tasks ) {
        offloaded_tasks = NULL;
#if TBB_USE_ASSERT
        offloaded_task_list_link = NULL;
#endif /* TBB_USE_ASSERT */
    }
    else {
        __TBB_ASSERT( link, NULL );
        // Mark end of list
        *link = NULL;
        offloaded_task_list_link = link;
    }
    __TBB_ASSERT( link, NULL );
    size_t num_tasks = tasks.size();
    if ( num_tasks ) {
        GATHER_STATISTIC( ++my_counters.prio_tasks_reloaded );
        size_t T = prepare_task_pool( num_tasks );
        tasks.copy_memory( my_arena_slot->task_pool_ptr + T );
        if ( --num_tasks ) {
            commit_spawned_tasks( T += num_tasks );
            enter_arena();
            my_arena->advertise_new_work</*Spawned=*/true>();
        }
        __TBB_ASSERT( T == __TBB_load_relaxed(my_arena_slot->tail), NULL );
        __TBB_ASSERT( T < my_arena_slot->my_task_pool_size, NULL );
        t = my_arena_slot->task_pool_ptr[T];
        poison_pointer(my_arena_slot->task_pool_ptr[T]);
        assert_task_pool_valid();
    }
    return t;
}

task* generic_scheduler::reload_tasks () {
    uintptr_t reload_epoch = *my_ref_reload_epoch;
    __TBB_ASSERT( my_offloaded_tasks, NULL );
    __TBB_ASSERT( my_local_reload_epoch <= reload_epoch
                  || my_local_reload_epoch - reload_epoch > uintptr_t(-1)/2,
                  "Reload epoch counter overflow?" );
    if ( my_local_reload_epoch == reload_epoch )
        return NULL;
    __TBB_ASSERT( my_offloaded_tasks, NULL );
    intptr_t top_priority = effective_reference_priority();
    __TBB_ASSERT( (uintptr_t)top_priority < (uintptr_t)num_priority_levels, NULL );
    task *t = reload_tasks( my_offloaded_tasks, my_offloaded_task_list_tail_link, top_priority );
    if ( my_offloaded_tasks && (my_arena->my_bottom_priority >= top_priority || !my_arena->my_num_workers_requested) ) {
        // Safeguard against deliberately relaxed synchronization while checking
        // for the presence of work in arena (so that not to impact hot paths).
        // Arena may be reset to empty state when offloaded low priority tasks
        // are still present. This results in both bottom and top priority bounds
        // becoming 'normal', which makes offloaded low priority tasks unreachable.
        // Update arena's bottom priority to accommodate them.

        // First indicate the presence of lower-priority tasks
        my_market->update_arena_priority( *my_arena, priority(*my_offloaded_tasks) );
        // Then mark arena as full to unlock arena priority level adjustment
        // by arena::is_out_of_work(), and ensure worker's presence
        my_arena->advertise_new_work</*Spawned=*/false>();
    }
    my_local_reload_epoch = reload_epoch;
    return t;
}
#endif /* __TBB_TASK_PRIORITY */

inline task* generic_scheduler::get_task() {
    __TBB_ASSERT( in_arena(), NULL );
    task* result = NULL;
    size_t T = __TBB_load_relaxed(my_arena_slot->tail); // mirror
retry:
    __TBB_store_relaxed(my_arena_slot->tail, --T);
    atomic_fence();
    if ( (intptr_t)__TBB_load_relaxed(my_arena_slot->head) > (intptr_t)T ) {
        acquire_task_pool();
        size_t H = __TBB_load_relaxed(my_arena_slot->head); // mirror
        if ( (intptr_t)H <= (intptr_t)T ) {
            // The thief backed off - grab the task
            result = my_arena_slot->task_pool_ptr[T];
            __TBB_ASSERT( !is_poisoned(result), NULL );
            poison_pointer( my_arena_slot->task_pool_ptr[T] );
        }
        else {
            __TBB_ASSERT ( H == __TBB_load_relaxed(my_arena_slot->head)
                        && T == __TBB_load_relaxed(my_arena_slot->tail)
                        && H == T + 1, "victim/thief arbitration algorithm failure" );
        }
        if ( (intptr_t)H < (intptr_t)T )
            release_task_pool();
        else
            reset_deque_and_leave_arena( /*locked=*/true );
    }
    else {
        __TBB_control_consistency_helper(); // on my_arena_slot->head
        result = my_arena_slot->task_pool_ptr[T];
        __TBB_ASSERT( !is_poisoned(result), NULL );
        poison_pointer( my_arena_slot->task_pool_ptr[T] );
    }
    if( result && is_proxy(*result) ) {
        task_proxy &tp = *(task_proxy*)result;
        result = tp.extract_task<task_proxy::pool_bit>();
        if( !result ) {
            // Proxy was empty, so it's our responsibility to free it
            free_task<small_task>(tp);
            if ( in_arena() )
                goto retry;
            __TBB_ASSERT( is_quiescent_local_task_pool_reset(), NULL );
            return NULL;
        }
        GATHER_STATISTIC( ++my_counters.proxies_executed );
        // Following assertion should be true because TBB 2.0 tasks never specify affinity, and hence are not proxied.
        __TBB_ASSERT( is_version_3_task(*result), "backwards compatibility with TBB 2.0 broken" );
        // Task affinity has changed.
        my_innermost_running_task = result;
        result->note_affinity(my_affinity_id);
    }
    __TBB_ASSERT( result || is_quiescent_local_task_pool_reset(), NULL );
    return result;
} // generic_scheduler::get_task

task* generic_scheduler::steal_task( arena_slot& victim_slot ) {
    task** victim_pool = lock_task_pool( &victim_slot );
    if ( !victim_pool )
        return NULL;
    task* result = NULL;
    size_t H = __TBB_load_relaxed(victim_slot.head); // mirror
    const size_t H0 = H;
    int skip_and_bump = 0; // +1 for skipped task and +1 for bumped head&tail
retry:
    __TBB_store_relaxed( victim_slot.head, ++H );
    atomic_fence();
    if ( (intptr_t)H > (intptr_t)__TBB_load_relaxed(victim_slot.tail) ) {
        // Stealing attempt failed, deque contents has not been changed by us
        GATHER_STATISTIC( ++my_counters.thief_backoffs );
        __TBB_store_relaxed( victim_slot.head, /*dead: H = */ H0 );
        skip_and_bump++; // trigger that we bumped head and tail
        __TBB_ASSERT ( !result, NULL );
    }
    else {
        __TBB_control_consistency_helper(); // on victim_slot.tail
        result = victim_pool[H-1];
        __TBB_ASSERT( !is_poisoned(result), NULL );
        if( is_proxy(*result) ) {
            task_proxy& tp = *static_cast<task_proxy*>(result);
            // If mailed task is likely to be grabbed by its destination thread, skip it.
            if ( task_proxy::is_shared(tp.task_and_tag) && tp.outbox->recipient_is_idle() )
            {
                GATHER_STATISTIC( ++my_counters.proxies_bypassed );
                result = NULL;
                __TBB_ASSERT( skip_and_bump < 2, NULL );
                skip_and_bump = 1; // note we skipped a task
                goto retry;
            }
        }
        __TBB_ASSERT( result, NULL );
        // emit "task was consumed" signal
        ITT_NOTIFY(sync_acquired, (void*)((uintptr_t)&victim_slot+sizeof(uintptr_t)));
        const size_t H1 = H0 + 1;
        if ( H1 < H ) {
            // Some proxies in the task pool have been bypassed. Need to close
            // the hole left by the stolen task. The following variant:
            //     victim_pool[H-1] = victim_pool[H0];
            // is of constant time, but creates a potential for degrading stealing
            // mechanism efficiency and growing owner's stack size too much because
            // of moving earlier split off (and thus larger) chunks closer to owner's
            // end of the deque (tail).
            // So we use linear time variant that is likely to be amortized to be
            // near-constant time, though, and preserves stealing efficiency premises.
            // These changes in the deque must be released to the owner.
            memmove( victim_pool + H1, victim_pool + H0, (H - H1) * sizeof(task*) );
            __TBB_store_with_release( victim_slot.head, /*dead: H = */ H1 );
            if ( (intptr_t)H >= (intptr_t)__TBB_load_relaxed(victim_slot.tail) )
                skip_and_bump++; // trigger that we bumped head and tail
        }
        poison_pointer( victim_pool[H0] );
    }

    unlock_task_pool( &victim_slot, victim_pool );
    __TBB_ASSERT( skip_and_bump <= 2, NULL );
#if __TBB_PREFETCHING
    __TBB_cl_evict(&victim_slot.head);
    __TBB_cl_evict(&victim_slot.tail);
#endif
    if( --skip_and_bump > 0 ) { // if both: task skipped and head&tail bumped
        // Synchronize with snapshot as we bumped head and tail which can falsely trigger EMPTY state
        atomic_fence();
        my_arena->advertise_new_work</*Spawned=*/true>();
    }
    return result;
}

task* generic_scheduler::get_mailbox_task() {
    __TBB_ASSERT( my_affinity_id>0, "not in arena" );
    while ( task_proxy* const tp = my_inbox.pop() ) {
        if ( task* result = tp->extract_task<task_proxy::mailbox_bit>() ) {
            ITT_NOTIFY( sync_acquired, my_inbox.outbox() );
            result->prefix().extra_state |= es_task_is_stolen;
            return result;
        }
        // We have exclusive access to the proxy, and can destroy it.
        free_task<no_cache_small_task>(*tp);
    }
    return NULL;
}

// TODO: Rename to publish_task_pool
void generic_scheduler::enter_arena() {
    __TBB_ASSERT ( my_arena, "no arena: initialization not completed?" );
    __TBB_ASSERT ( my_arena_index < my_arena->my_num_slots, "arena slot index is out-of-bound" );
    __TBB_ASSERT ( my_arena_slot == &my_arena->my_slots[my_arena_index], NULL);
    __TBB_ASSERT ( my_arena_slot->task_pool == EmptyTaskPool, "someone else grabbed my arena slot?" );
    __TBB_ASSERT ( __TBB_load_relaxed(my_arena_slot->head) < __TBB_load_relaxed(my_arena_slot->tail),
                   "entering arena without tasks to share" );
    // Release signal on behalf of previously spawned tasks (when this thread was not in arena yet)
    ITT_NOTIFY(sync_releasing, my_arena_slot);
    __TBB_store_with_release( my_arena_slot->task_pool, my_arena_slot->task_pool_ptr );
}

void generic_scheduler::leave_arena() {
    __TBB_ASSERT( in_arena(), "Not in arena" );
    // Do not reset my_arena_index. It will be used to (attempt to) re-acquire the slot next time
    __TBB_ASSERT( &my_arena->my_slots[my_arena_index] == my_arena_slot, "arena slot and slot index mismatch" );
    __TBB_ASSERT ( my_arena_slot->task_pool == LockedTaskPool, "Task pool must be locked when leaving arena" );
    __TBB_ASSERT ( is_quiescent_local_task_pool_empty(), "Cannot leave arena when the task pool is not empty" );
    ITT_NOTIFY(sync_releasing, &my_arena->my_slots[my_arena_index]);
    // No release fence is necessary here as this assignment precludes external
    // accesses to the local task pool when becomes visible. Thus it is harmless
    // if it gets hoisted above preceding local bookkeeping manipulations.
    __TBB_store_relaxed( my_arena_slot->task_pool, EmptyTaskPool );
}

generic_scheduler* generic_scheduler::create_worker( market& m, size_t index ) {
    generic_scheduler* s = allocate_scheduler( NULL, index ); // index is not a real slot in arena
#if __TBB_TASK_GROUP_CONTEXT
    s->my_dummy_task->prefix().context = &the_dummy_context;
    // Sync up the local cancellation state with the global one. No need for fence here.
    s->my_context_state_propagation_epoch = the_context_state_propagation_epoch;
#endif /* __TBB_TASK_GROUP_CONTEXT */
    s->my_market = &m;
    s->init_stack_info();
#if __TBB_TASK_PRIORITY
    s->my_ref_top_priority = &s->my_market->my_global_top_priority;
    s->my_ref_reload_epoch = &s->my_market->my_global_reload_epoch;
#endif /* __TBB_TASK_PRIORITY */
    return s;
}

// TODO: make it a member method
generic_scheduler* generic_scheduler::create_master( arena& a ) {
    generic_scheduler* s = allocate_scheduler( &a, 0 /*Master thread always occupies the first slot*/ );
    task& t = *s->my_dummy_task;
    s->my_innermost_running_task = &t;
    s->my_dispatching_task = &t;
    t.prefix().ref_count = 1;
    governor::sign_on(s);
    __TBB_ASSERT( &task::self()==&t, "governor::sign_on failed?" );
#if __TBB_TASK_GROUP_CONTEXT
    // Context to be used by root tasks by default (if the user has not specified one).
    // Allocation is done by NFS allocator because we cannot reuse memory allocated
    // for task objects since the free list is empty at the moment.
    t.prefix().context = a.my_default_ctx;
#endif /* __TBB_TASK_GROUP_CONTEXT */
    s->my_market = a.my_market;
    __TBB_ASSERT( s->my_arena_index == 0, "Master thread must occupy the first slot in its arena" );
    s->attach_mailbox(1);
    s->my_arena_slot = a.my_slots + 0;
    s->my_arena_slot->my_scheduler = s;
#if _WIN32||_WIN64
    __TBB_ASSERT( s->my_market, NULL );
    s->my_market->register_master( s->master_exec_resource );
#endif /* _WIN32||_WIN64 */
    s->init_stack_info();
#if __TBB_TASK_GROUP_CONTEXT
    // Sync up the local cancellation state with the global one. No need for fence here.
    s->my_context_state_propagation_epoch = the_context_state_propagation_epoch;
#endif
#if __TBB_TASK_PRIORITY
    // In the current implementation master threads continue processing even when
    // there are other masters with higher priority. Only TBB worker threads are
    // redistributed between arenas based on the latters' priority. Thus master
    // threads use arena's top priority as a reference point (in contrast to workers
    // that use my_market->my_global_top_priority).
    s->my_ref_top_priority = &s->my_arena->my_top_priority;
    s->my_ref_reload_epoch = &s->my_arena->my_reload_epoch;
#endif /* __TBB_TASK_PRIORITY */
#if __TBB_SCHEDULER_OBSERVER
    // Process any existing observers.
    __TBB_ASSERT( a.my_observers.empty(), "Just created arena cannot have any observers associated with it" );
    the_global_observer_list.notify_entry_observers( s->my_last_global_observer, /*worker=*/false );
#endif /* __TBB_SCHEDULER_OBSERVER */
    return s;
}

void generic_scheduler::cleanup_worker( void* arg, bool worker ) {
    generic_scheduler& s = *(generic_scheduler*)arg;
    __TBB_ASSERT( !s.my_arena_slot, "cleaning up attached worker" );
#if __TBB_SCHEDULER_OBSERVER
    if ( worker ) // can be called by master for worker, do not notify master twice
        the_global_observer_list.notify_exit_observers( s.my_last_global_observer, /*worker=*/true );
#endif /* __TBB_SCHEDULER_OBSERVER */
    s.free_scheduler();
}

void generic_scheduler::cleanup_master() {
    generic_scheduler& s = *this; // for similarity with cleanup_worker
    __TBB_ASSERT( s.my_arena_slot, NULL);
#if __TBB_SCHEDULER_OBSERVER
    s.my_arena->my_observers.notify_exit_observers( s.my_last_local_observer, /*worker=*/false );
    the_global_observer_list.notify_exit_observers( s.my_last_global_observer, /*worker=*/false );
#endif /* __TBB_SCHEDULER_OBSERVER */
    if( in_arena() ) {
        acquire_task_pool();
        if ( my_arena_slot->task_pool == EmptyTaskPool ||
             __TBB_load_relaxed(my_arena_slot->head) >= __TBB_load_relaxed(my_arena_slot->tail) )
        {
            // Local task pool is empty
            leave_arena();
        }
        else {
            // Master's local task pool may e.g. contain proxies of affinitized tasks.
            release_task_pool();
            __TBB_ASSERT ( governor::is_set(this), "TLS slot is cleared before the task pool cleanup" );
            s.local_wait_for_all( *s.my_dummy_task, NULL );
            __TBB_ASSERT( !in_arena(), NULL );
            __TBB_ASSERT ( governor::is_set(this), "Other thread reused our TLS key during the task pool cleanup" );
        }
    }
    __TBB_ASSERT( s.my_market, NULL );
    market *my_market = s.my_market;
#if _WIN32||_WIN64
    s.my_market->unregister_master( s.master_exec_resource );
#endif /* _WIN32||_WIN64 */
    arena* a = s.my_arena;
    __TBB_ASSERT(a->my_slots+0 == my_arena_slot, NULL);
#if __TBB_STATISTICS
    *my_arena_slot->my_counters += s.my_counters;
#endif /* __TBB_STATISTICS */
#if __TBB_TASK_PRIORITY
    __TBB_ASSERT( my_arena_slot->my_scheduler, NULL );
    // Master's scheduler may be locked by a worker taking arena snapshot or by
    // a thread propagating task group state change across the context tree.
    while ( as_atomic(my_arena_slot->my_scheduler).compare_and_swap(NULL, this) != this )
        __TBB_Yield();
    __TBB_ASSERT( !my_arena_slot->my_scheduler, NULL );
#else /* !__TBB_TASK_PRIORITY */
    __TBB_store_with_release(my_arena_slot->my_scheduler, (generic_scheduler*)NULL);
#endif /* __TBB_TASK_PRIORITY */
    my_arena_slot = NULL; // detached from slot
    s.free_scheduler();
    // Resetting arena to EMPTY state (as earlier TBB versions did) should not be
    // done here (or anywhere else in the master thread to that matter) because
    // after introducing arena-per-master logic and fire-and-forget tasks doing
    // so can result either in arena's premature destruction (at least without
    // additional costly checks in workers) or in unnecessary arena state changes
    // (and ensuing workers migration).
#if __TBB_STATISTICS_EARLY_DUMP
    GATHER_STATISTIC( a->dump_arena_statistics() );
#endif
    if (governor::needsWaitWorkers())
        my_market->prepare_wait_workers();
    a->on_thread_leaving</*is_master*/true>();
    if (governor::needsWaitWorkers())
        my_market->wait_workers();
}

} // namespace internal
} // namespace tbb

/*
    Comments:

1.  The premise of the cancellation support implementation is that cancellations are
    not part of the hot path of the program execution. Therefore all changes in its
    implementation in order to reduce the overhead of the cancellation control flow
    should be done only in ways that do not increase overhead of the normal execution.

    In general contexts are used by all threads and their descendants are created in
    different threads as well. In order to minimize impact of the cross-thread tree
    maintenance (first of all because of the synchronization), the tree of contexts
    is split into pieces, each of which is handled by the only thread. Such pieces
    are represented as lists of contexts, members of which are contexts that were
    bound to their parents in the given thread.

    The context tree maintenance and cancellation propagation algorithms is designed
    in such a manner that cross-thread access to a context list will take place only
    when cancellation signal is sent (by user or when an exception happens), and
    synchronization is necessary only then. Thus the normal execution flow (without
    exceptions and cancellation) remains free from any synchronization done on
    behalf of exception handling and cancellation support.

2.  Consider parallel cancellations at the different levels of the context tree:

        Ctx1 <- Cancelled by Thread1            |- Thread2 started processing
         |                                      |
        Ctx2                                    |- Thread1 started processing
         |                                   T1 |- Thread2 finishes and syncs up local counters
        Ctx3 <- Cancelled by Thread2            |
         |                                      |- Ctx5 is bound to Ctx2
        Ctx4                                    |
                                             T2 |- Thread1 reaches Ctx2

    Thread-propagator of each cancellation increments global counter. However the thread
    propagating the cancellation from the outermost context (Thread1) may be the last
    to finish. Which means that the local counters may be synchronized earlier (by Thread2,
    at Time1) than it propagated cancellation into Ctx2 (at time Time2). If a new context
    (Ctx5) is created and bound to Ctx2 between Time1 and Time2, checking its parent only
    (Ctx2) may result in cancellation request being lost.

    This issue is solved by doing the whole propagation under the lock.

    If we need more concurrency while processing parallel cancellations, we could try
    the following modification of the propagation algorithm:

    advance global counter and remember it
    for each thread:
        scan thread's list of contexts
    for each thread:
        sync up its local counter only if the global counter has not been changed

    However this version of the algorithm requires more analysis and verification.

3.  There is no portable way to get stack base address in Posix, however the modern
    Linux versions provide pthread_attr_np API that can be used  to obtain thread's
    stack size and base address. Unfortunately even this function does not provide
    enough information for the main thread on IA-64 architecture (RSE spill area
    and memory stack are allocated as two separate discontinuous chunks of memory),
    and there is no portable way to discern the main and the secondary threads.
    Thus for OS X* and IA-64 Linux architecture we use the TBB worker stack size for 
    all threads and use the current stack top as the stack base. This simplified 
    approach is based on the following assumptions:
    1) If the default stack size is insufficient for the user app needs, the
    required amount will be explicitly specified by the user at the point of the
    TBB scheduler initialization (as an argument to tbb::task_scheduler_init
    constructor).
    2) When a master thread initializes the scheduler, it has enough space on its
    stack. Here "enough" means "at least as much as worker threads have".
    3) If the user app strives to conserve the memory by cutting stack size, it
    should do this for TBB workers too (as in the #1).
*/
