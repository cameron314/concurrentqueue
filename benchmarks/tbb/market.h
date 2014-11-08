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

#ifndef _TBB_market_H
#define _TBB_market_H

#include "tbb/tbb_stddef.h"

#include "scheduler_common.h"
#include "tbb/atomic.h"
#include "tbb/spin_rw_mutex.h"
#include "../rml/include/rml_tbb.h"

#include "intrusive_list.h"

#if defined(_MSC_VER) && defined(_Wp64)
    // Workaround for overzealous compiler warnings in /Wp64 mode
    #pragma warning (push)
    #pragma warning (disable: 4244)
#endif

namespace tbb {

class task_group_context;

namespace internal {

//------------------------------------------------------------------------
// Class market
//------------------------------------------------------------------------

class market : no_copy, rml::tbb_client {
    friend class generic_scheduler;
    friend class arena;
    template<typename SchedulerTraits> friend class custom_scheduler;
    friend class tbb::task_group_context;
private:
    friend void ITT_DoUnsafeOneTimeInitialization ();

    typedef intrusive_list<arena> arena_list_type;

    //! Currently active global market
    static market* theMarket;

    typedef scheduler_mutex_type global_market_mutex_type;

    //! Mutex guarding creation/destruction of theMarket, insertions/deletions in my_arenas, and cancellation propagation
    static global_market_mutex_type  theMarketMutex;

    //! Reference count controlling market object lifetime
    intptr_t my_ref_count;

    //! Lightweight mutex guarding accounting operations with arenas list
    typedef spin_rw_mutex arenas_list_mutex_type;
    arenas_list_mutex_type my_arenas_list_mutex;

    //! Pointer to the RML server object that services this TBB instance.
    rml::tbb_server* my_server;

    //! Stack size of worker threads
    size_t my_stack_size;

    //! Number of workers requested from the underlying resource manager
    unsigned my_max_num_workers;

    //! Number of workers that have been delivered by RML
    /** Used to assign indices to the new workers coming from RML, and busy part
        of my_workers array. **/
    atomic<unsigned> my_num_workers;

#if __TBB_TASK_PRIORITY
    //! Highest priority among active arenas in the market.
    /** Arena priority level is its tasks highest priority (specified by arena's
        my_top_priority member).
        Arena is active when it has outstanding request for workers. Note that 
        inactive arena may have workers lingering there for some time. **/
    intptr_t my_global_top_priority;

    //! Lowest priority among active arenas in the market.
    /** See also my_global_top_priority **/
    intptr_t my_global_bottom_priority;

    //! Tracks events that may bring tasks in offload areas to the top priority level.
    /** Incremented when global top priority is decremented or a task group priority
        is elevated to the current top level. **/
    uintptr_t my_global_reload_epoch;

    //! Information about arenas at a particular priority level
    struct priority_level_info {
        //! List of arenas at this priority level
        arena_list_type arenas;

        //! The first arena to be checked when idle worker seeks for an arena to enter
        /** The check happens in round-robin fashion. **/
        arena *next_arena;

        //! Total amount of workers requested by arenas at this priority level.
        int workers_requested;

        //! Maximal amount of workers the market can tell off to this priority level.
        int workers_available;

#if __TBB_TRACK_PRIORITY_LEVEL_SATURATION
        //! Total amount of workers that are in arenas at this priority level.
        int workers_present;
#endif /* __TBB_TRACK_PRIORITY_LEVEL_SATURATION */
    }; // struct priority_level_info

    //! Information about arenas at different priority levels
    priority_level_info my_priority_levels[num_priority_levels];

#if __TBB_TRACK_PRIORITY_LEVEL_SATURATION
    //! Lowest priority level having workers available.
    intptr_t my_lowest_populated_level;
#endif /* __TBB_TRACK_PRIORITY_LEVEL_SATURATION */

#else /* !__TBB_TASK_PRIORITY */

    //! List of registered arenas
    arena_list_type my_arenas;

    //! The first arena to be checked when idle worker seeks for an arena to enter
    /** The check happens in round-robin fashion. **/
    arena *my_next_arena;

    //! Number of workers that were requested by all arenas
    int my_total_demand;
#endif /* !__TBB_TASK_PRIORITY */

    //! ABA prevention marker to assign to newly created arenas
    uintptr_t my_arenas_aba_epoch;

#if __TBB_COUNT_TASK_NODES
    //! Net number of nodes that have been allocated from heap.
    /** Updated each time a scheduler or arena is destroyed. */
    atomic<intptr_t> my_task_node_count;
#endif /* __TBB_COUNT_TASK_NODES */

    //! Constructor
    market ( unsigned max_num_workers, size_t stack_size );

    //! Factory method creating new market object
    static market& global_market ( unsigned max_num_workers, size_t stack_size );

    //! Destroys and deallocates market object created by market::create()
    void destroy ();

    void try_destroy_arena ( arena*, uintptr_t aba_epoch );

#if __TBB_TASK_PRIORITY
    //! Returns next arena that needs more workers, or NULL.
    arena* arena_in_need ( arena* prev_arena );

    //! Recalculates the number of workers assigned to each arena at and below the specified priority.
    /** The actual number of workers servicing a particular arena may temporarily 
        deviate from the calculated value. **/
    void update_allotment ( intptr_t highest_affected_priority );

    //! Changes arena's top priority and updates affected priority levels info in the market.
    void update_arena_top_priority ( arena& a, intptr_t newPriority );

    //! Changes market's global top priority and related settings.
    inline void update_global_top_priority ( intptr_t newPriority );

    //! Resets empty market's global top and bottom priority to the normal level.
    inline void reset_global_priority ();

    inline void advance_global_reload_epoch () {
        __TBB_store_with_release( my_global_reload_epoch, my_global_reload_epoch + 1 );
    }

    void assert_market_valid () const {
        __TBB_ASSERT( (my_priority_levels[my_global_top_priority].workers_requested > 0
                           && !my_priority_levels[my_global_top_priority].arenas.empty())
                       || (my_global_top_priority == my_global_bottom_priority &&
                           my_global_top_priority == normalized_normal_priority), NULL );
    }

    bool has_any_demand() const {
        for(int p = 0; p < num_priority_levels; p++)
            if( __TBB_load_with_acquire(my_priority_levels[p].workers_requested) > 0 ) // TODO: use as_atomic here and below
                return true;
        return false;
    }

#else /* !__TBB_TASK_PRIORITY */

    //! Recalculates the number of workers assigned to each arena in the list.
    /** The actual number of workers servicing a particular arena may temporarily 
        deviate from the calculated value. **/
    void update_allotment () {
        if ( my_total_demand )
            update_allotment( my_arenas, my_total_demand, (int)my_max_num_workers );
    }

    //! Returns next arena that needs more workers, or NULL.
    arena* arena_in_need (arena*) {
        if(__TBB_load_with_acquire(my_total_demand) <= 0)
            return NULL;
        arenas_list_mutex_type::scoped_lock lock(my_arenas_list_mutex, /*is_writer=*/false);
        return arena_in_need(my_arenas, my_next_arena);
    }
    void assert_market_valid () const {}
#endif /* !__TBB_TASK_PRIORITY */

    //! Returns number of masters doing computational (CPU-intensive) work
    int num_active_masters () { return 1; }  // APM TODO: replace with a real mechanism


    ////////////////////////////////////////////////////////////////////////////////
    // Helpers to unify code branches dependent on priority feature presence

    void insert_arena_into_list ( arena& a );

    void remove_arena_from_list ( arena& a );

    arena* arena_in_need ( arena_list_type &arenas, arena *&next );

    static void update_allotment ( arena_list_type& arenas, int total_demand, int max_workers );


    ////////////////////////////////////////////////////////////////////////////////
    // Implementation of rml::tbb_client interface methods

    /*override*/ version_type version () const { return 0; }

    /*override*/ unsigned max_job_count () const { return my_max_num_workers; }

    /*override*/ size_t min_stack_size () const { return worker_stack_size(); }

    /*override*/ policy_type policy () const { return throughput; }

    /*override*/ job* create_one_job ();

    /*override*/ void cleanup( job& j );

    /*override*/ void acknowledge_close_connection ();

    /*override*/ void process( job& j );

public:
    //! Creates an arena object
    /** If necessary, also creates global market instance, and boosts its ref count.
        Each call to create_arena() must be matched by the call to arena::free_arena(). **/
    static arena& create_arena ( unsigned max_num_workers, size_t stack_size );

    //! Removes the arena from the market's list
    static void try_destroy_arena ( market*, arena*, uintptr_t aba_epoch, bool master );

    //! Removes the arena from the market's list
    void detach_arena ( arena& );

    //! Decrements market's refcount and destroys it in the end
    void release ();

    //! Request that arena's need in workers should be adjusted.
    /** Concurrent invocations are possible only on behalf of different arenas. **/
    void adjust_demand ( arena&, int delta );

    //! Guarantee that request_close_connection() is called by master, not some worker
    /** Must be called before arena::on_thread_leaving() **/
    void prepare_wait_workers() { ++my_ref_count; }

    //! Wait workers termination
    void wait_workers ();

    //! Returns the requested stack size of worker threads.
    size_t worker_stack_size () const { return my_stack_size; }

#if _WIN32||_WIN64
    //! register master with the resource manager
    void register_master( ::rml::server::execution_resource_t& rsc_handle ) {
        __TBB_ASSERT( my_server, "RML server not defined?" );
        // the server may ignore registration and set master_exec_resource to NULL.
        my_server->register_master( rsc_handle );
    }

    //! unregister master with the resource manager
    void unregister_master( ::rml::server::execution_resource_t& rsc_handle ) const {
        my_server->unregister_master( rsc_handle );
    }
#endif /* WIN */

#if __TBB_TASK_GROUP_CONTEXT
    //! Finds all contexts affected by the state change and propagates the new state to them.
    template <typename T>
    bool propagate_task_group_state ( T task_group_context::*mptr_state, task_group_context& src, T new_state );
#endif /* __TBB_TASK_GROUP_CONTEXT */

#if __TBB_TASK_PRIORITY
    //! Lowers arena's priority is not higher than newPriority 
    /** Returns true if arena priority was actually elevated. **/ 
    bool lower_arena_priority ( arena& a, intptr_t new_priority, uintptr_t old_reload_epoch );

    //! Makes sure arena's priority is not lower than newPriority 
    /** Returns true if arena priority was elevated. Also updates arena's bottom
        priority boundary if necessary.

        This method is called whenever a user changes priority, because whether
        it was hiked or sunk can be determined for sure only under the lock used
        by this function. **/
    bool update_arena_priority ( arena& a, intptr_t new_priority );
#endif /* __TBB_TASK_PRIORITY */

#if __TBB_COUNT_TASK_NODES
    //! Returns the number of task objects "living" in worker threads
    intptr_t workers_task_node_count();

    //! Net number of nodes that have been allocated from heap.
    /** Updated each time a scheduler or arena is destroyed. */
    void update_task_node_count( intptr_t delta ) { my_task_node_count += delta; }
#endif /* __TBB_COUNT_TASK_NODES */

#if __TBB_TASK_GROUP_CONTEXT
    //! Array of pointers to the registered workers
    /** Used by cancellation propagation mechanism.
        Must be the last data member of the class market. **/
    generic_scheduler* my_workers[1];
#endif /* __TBB_TASK_GROUP_CONTEXT */

}; // class market

#if __TBB_TASK_PRIORITY
    #define BeginForEachArena(a)    \
        arenas_list_mutex_type::scoped_lock arena_list_lock(my_arenas_list_mutex);  \
        for ( intptr_t i = my_global_top_priority; i >= my_global_bottom_priority; --i ) {  \
            /*arenas_list_mutex_type::scoped_lock arena_list_lock(my_priority_levels[i].my_arenas_list_mutex);*/ \
            arena_list_type &arenas = my_priority_levels[i].arenas;
#else /* !__TBB_TASK_PRIORITY */
    #define BeginForEachArena(a)    \
        arena_list_type &arenas = my_arenas; {
#endif /* !__TBB_TASK_PRIORITY */

#define ForEachArena(a)     \
    BeginForEachArena(a)    \
        arena_list_type::iterator it = arenas.begin();  \
        for ( ; it != arenas.end(); ++it ) {            \
            arena &a = *it;

#define EndForEach() }}


} // namespace internal
} // namespace tbb

#if defined(_MSC_VER) && defined(_Wp64)
    // Workaround for overzealous compiler warnings in /Wp64 mode
    #pragma warning (pop)
#endif // warning 4244 is back

#endif /* _TBB_market_H */
