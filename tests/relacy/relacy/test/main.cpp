#include "stdafx.h"

//#define RL_MSVC_OUTPUT

#include "../relacy/relacy_std.hpp"
#include "memory_order.hpp"
#include "fence.hpp"
#include "data_race.hpp"
#include "mutex.hpp"
#include "condvar.hpp"
#include "semaphore.hpp"
#include "event.hpp"
#include "scheduler.hpp"
#include "compare_swap.hpp"
#include "wfmo.hpp"
#include "thread_local.hpp"
#include "dyn_thread.hpp"
#include "memory.hpp"
#include "pthread.hpp"
#include "windows.hpp"
#include "addr_hash.hpp"
#include "futex.hpp"

#include "../relacy/windows.h"
#include "../relacy/pthread.h"

#include <cstdio>
#include <climits>

class queue_t
{
public:
    queue_t()
    {
        VAR(head) = 0;
        VAR(tail) = 0;
        pthread_mutex_init(&mtx, 0);
        pthread_cond_init(&cv, 0);
    }

    ~queue_t()
    {
        pthread_mutex_destroy(&mtx);
        pthread_cond_destroy(&cv);
    }

    void enqueue(void* data)
    {
        node_t* n = new node_t;
        n->VAR(next) = 0;
        n->VAR(data) = data;
        bool was_empty = false;

        pthread_mutex_lock(&mtx);
        if (VAR(head) == 0)
        {
            was_empty = true;
            VAR(head) = n;
            VAR(tail) = n;
        }
        else
        {
            VAR(tail)->VAR(next) = n;
            VAR(tail) = n;
        }
        pthread_mutex_unlock(&mtx);

        if (was_empty)
            pthread_cond_broadcast(&cv);
    }

    void* dequeue()
    {
        node_t* n = 0;

        pthread_mutex_lock(&mtx);
        while (VAR(head) == 0)
            pthread_cond_wait(&cv, &mtx);
        n = VAR(head);
        if (n->VAR(next) == 0)
            VAR(tail) = 0;
        VAR(head) = n->VAR(next);
        pthread_mutex_unlock(&mtx);

        void* data = n->VAR(data);
        delete n;
        return data;
    }

private:
    struct node_t
    {
        VAR_T(node_t*) next;
        VAR_T(void*) data;
    };

    VAR_T(node_t*) head;
    VAR_T(node_t*) tail;

    pthread_mutex_t mtx;
    pthread_cond_t cv;
};

void* enqueue_thread(void* ctx)
{
    queue_t* q = static_cast<queue_t*>(ctx);
    for (size_t i = 0; i != 4; i += 1)
        q->enqueue((void*)(i + 1));
    return 0;
}

void* dequeue_thread(void* ctx)
{
    queue_t* q = static_cast<queue_t*>(ctx);
    for (size_t i = 0; i != 4; i += 1)
    {
        void* data = q->dequeue();
        assert((int)(uintptr_t)data >= 1 && (int)(uintptr_t)data <= 4);
    }
    return 0;
}

void queue_test()
{
    queue_t q;

    pthread_t th [4];
    for (size_t i = 0; i != 2; i += 1)
        pthread_create(&th[i], 0, enqueue_thread, &q);
    for (size_t i = 2; i != 4; i += 1)
        pthread_create(&th[i], 0, dequeue_thread, &q);

    void* res = 0;
    for (size_t i = 0; i != 4; i += 1)
        pthread_join(th[i], &res);
}

/*
class recursive_timed_mutex
{
public:
    recursive_timed_mutex()
    {
        sema.init(false, 1, 1, $);
        owner = -1;
        recursion_count = 0;
    }

    ~recursive_timed_mutex()
    {
        assert(owner == -1 && recursion_count == 0);
        sema.deinit($);
    }
    
    void lock(rl::debug_info_param info)
    {
        rl::context& c = rl::ctx();
        if (owner == c.current_thread())
        {
            RL_HIST(rl::user_msg_event) {"recursive mutex lock"} RL_HIST_END();
            assert(recursion_count > 0);
            recursion_count += 1;
        }
        else
        {
            sema.wait(false, false, info);
            assert(owner == -1 && recursion_count == 0);
            owner = c.current_thread();
            recursion_count = 1;
        }
    }

    bool try_lock(rl::debug_info_param info)
    {
        rl::context& c = rl::ctx();
        if (owner == c.current_thread())
        {
            RL_HIST(rl::user_msg_event) {"recursive mutex try lock"} RL_HIST_END();
            assert(recursion_count > 0);
            recursion_count += 1;
            return true;
        }
        else
        {
            rl::sema_wakeup_reason r = sema.wait(true, false, info);
            if (r == rl::sema_wakeup_reason_success)
            {
                assert(owner == -1 && recursion_count == 0);
                owner = c.current_thread();
                recursion_count = 1;
                return true;
            }
            else
            {
                return false;
            }
        }
    }

    void unlock(rl::debug_info_param info)
    {
        rl::context& c = rl::ctx();
        assert(owner == c.current_thread() && recursion_count > 0);
        RL_HIST(rl::user_msg_event) {"recursive mutex unlock"} RL_HIST_END();
        recursion_count -= 1;
        if (recursion_count == 0)
        {
            owner = -1;
            unsigned prev;
            sema.post(1, prev, info);
        }
    }

    bool timed_lock(rl::debug_info_param info, ... )
    {
        rl::context& c = rl::ctx();
        if (owner == c.current_thread())
        {
            RL_HIST(rl::user_msg_event) {"recursive mutex timed lock"} RL_HIST_END();
            assert(recursion_count > 0);
            recursion_count += 1;
            return true;
        }
        else
        {
            rl::sema_wakeup_reason r = sema.wait(false, true, info);
            if (r == rl::sema_wakeup_reason_success)
            {
                assert(owner == -1 && recursion_count == 0);
                owner = c.current_thread();
                recursion_count = 1;
                return true;
            }
            else
            {
                return false;
            }
        }
    }

private:
    struct tag_t;
    rl::semaphore<tag_t> sema;
    rl::thread_id_t owner;
    int recursion_count;

    recursive_timed_mutex(recursive_timed_mutex const&);
    recursive_timed_mutex& operator = (recursive_timed_mutex const&);
};
*/

class recursive_timed_mutex
{
public:
    recursive_timed_mutex()
    {
        mtx = CreateMutex(0, 0, 0);
    }

    ~recursive_timed_mutex()
    {
        CloseHandle(mtx);
    }
    
    void lock(rl::debug_info_param info)
    {
        rl::rl_WaitForSingleObject(mtx, INFINITE, info);
    }

    bool try_lock(rl::debug_info_param info)
    {
        return WAIT_OBJECT_0 == rl::rl_WaitForSingleObject(mtx, 0, info);
    }

    void unlock(rl::debug_info_param info)
    {
        rl::rl_ReleaseMutex(mtx, info);
    }

    bool timed_lock(rl::debug_info_param info, ... /*abs_time*/)
    {
        return WAIT_OBJECT_0 == rl::rl_WaitForSingleObject(mtx, 1, info);
    }

private:
    HANDLE mtx;

    recursive_timed_mutex(recursive_timed_mutex const&);
    recursive_timed_mutex& operator = (recursive_timed_mutex const&);
};



struct recursive_timed_mutex_test : rl::test_suite<recursive_timed_mutex_test, 3>
{
    recursive_timed_mutex mtx;
    VAR_T(int) data;

    void thread(unsigned idx)
    {
        if (idx)
        {
            mtx.lock($);
            mtx.lock($);
            VAR(data) = 1;
            mtx.unlock($);
            mtx.unlock($);
        }
        else
        {
            if (mtx.timed_lock($))
            {
                VAR(data) = 2;
                mtx.unlock($);
            }
        }
    }

    void after()
    {
        //assert(VAR(data) != 2);
    }
};


int main()
{
    //rl::test_params p;
    //p.search_type = rl::sched_full;
    //p.context_bound = 5;
    //p.execution_depth_limit = 200;
    //rl::simulate<test_pthread_condvar>(p);
    //if (rand() <= RAND_MAX) return 0;

    //rl::execute<queue_test, 4>();
    //if (rand() <= RAND_MAX) return 0;

    //rl::test_params p;
    //p.initial_state = "1000000";
    //p.iteration_count = 2000000;
    //p.collect_history = true;
    //p.output_history = true;
    //p.search_type = rl::sched_bound;
    //p.search_type = rl::sched_full;
    //p.execution_depth_limit = 500;
    //p.context_bound = 1;
    //rl::simulate<test_pthread_condvar>(p);
    //std::cout << "scheduler state = \"" << p.final_state << "\"" << std::endl;
    //std::cout << std::endl;
    //if (rand() <= RAND_MAX) return 0;

    //rl::test_params p;
    //p.iteration_count = 80000000;
    //p.initial_state = "50000000";
    //p.search_type = rl::fair_context_bound_scheduler_type;
    //p.context_bound = 1;
    //p.collect_history = true;
    //p.output_history = true;
    //rl::simulate<test>(p);
    //if (rand() <= RAND_MAX) return 0;

    //rl::test_params p;
    //p.context_bound = 1;
    //p.iteration_count = 1000;
    //p.search_type = rl::fair_full_search_scheduler_type;
    //p.search_type = rl::random_scheduler_type;
    //p.collect_history = true;
    //p.output_history = true;
    //p.execution_depth_limit = 1000;
    //p.initial_state = "550 24 3 0 0 3 0 0 3 0 0 3 0 0 2 0 4 2 0 0 2 0 4 2 1 0 2 0 4 3 1 0 3 0 0 2 0 0 1 0 4 2 0 4 3 0 0 3 0 0 2 0 4 3 1 0 3 0 0 2 1 0 2 0 4 2 1 0 2 1 0 2 1 4";
    //bool result = rl::simulate<test>(p);
    //std::cout << "result=" << result << std::endl;
    //simulate<my_test>();
    //if (rand() <= RAND_MAX) return 0;

    rl::simulate_f tests[] = 
    {
#if 1
        &rl::simulate<test_FlushProcessWriteBuffers>,

        &rl::simulate<test_addr_hash>,
        &rl::simulate<test_addr_hash2>,
        //!!! fails &rl::simulate<sched_load_test>,
        &rl::simulate<test_memory_allocation>,

        // memory model
        &rl::simulate<test_pthread_thread>,
        &rl::simulate<test_pthread_mutex>,
        &rl::simulate<test_pthread_rwlock>,
        &rl::simulate<test_pthread_condvar>,
        &rl::simulate<test_pthread_condvar2>,
        &rl::simulate<test_pthread_sem>,
        
        &rl::simulate<coherent_read_read_test>,
        &rl::simulate<order_relaxed_test<0> >,
        &rl::simulate<order_relaxed_test<1> >,
        &rl::simulate<order_relaxed_test<2> >,
        &rl::simulate<order_relaxed_test<3> >,
        &rl::simulate<order_relaxed_test<4> >,
        &rl::simulate<reorder_single_var_test>,
        &rl::simulate<acq_rel_test>,

        &rl::simulate<seq_cst_test<0> >,
        &rl::simulate<seq_cst_test<1> >,

        &rl::simulate<reordering_test>,
        &rl::simulate<reordering_test2>,

        &rl::simulate<test_win_thread>,
        &rl::simulate<test_win_mutex>,
        &rl::simulate<test_win_cs>,
        &rl::simulate<test_win_condvar>,
        &rl::simulate<test_win_condvar_srw>,
        &rl::simulate<test_win_sem>,
        &rl::simulate<test_win_event>,

        &rl::simulate<modification_order_test>,
        &rl::simulate<transitive_test>,
        &rl::simulate<cc_transitive_test>,
        &rl::simulate<occasional_test>,

        // fences
        &rl::simulate<fence_synch_test<0, 0> >,
        &rl::simulate<fence_synch_test<1, 0> >,
        &rl::simulate<fence_synch_test<2, 0> >,
        &rl::simulate<fence_synch_test<0, 1> >,
        &rl::simulate<fence_synch_test<1, 1> >,
        &rl::simulate<fence_synch_test<2, 1> >,
  
        &rl::simulate<two_fence_synch_test>,
        &rl::simulate<seq_cst_fence_test<0> >,
        &rl::simulate<seq_cst_fence_test<1> >,

        // data races
        &rl::simulate<race_ld_ld_test>,
        &rl::simulate<race_ld_st_test>,
        &rl::simulate<race_st_st_test>,

        &rl::simulate<race_seq_ld_ld_test>,
        &rl::simulate<race_seq_ld_st_test>,
        &rl::simulate<race_seq_st_ld_test>,
        &rl::simulate<race_seq_st_st_test>,

        &rl::simulate<race_uninit_test>,
        &rl::simulate<race_indirect_test>,

        // compare_exchange
        &rl::simulate<cas_spurious_fail_test<0> >,
        &rl::simulate<cas_spurious_fail_test<1> >,
        &rl::simulate<cas_spurious_fail_test<2> >,

        // mutex
        &rl::simulate<test_deadlock>,
        &rl::simulate<test_deadlock2>,
        &rl::simulate<test_mutex_destuction>,
        &rl::simulate<test_mutex_destuction2>,
        &rl::simulate<test_mutex_recursion>,
        &rl::simulate<test_mutex_recursion_error>,
        &rl::simulate<test_mutex_unlock_error>,
        &rl::simulate<test_mutex_leak>,
        &rl::simulate<test_mutex>,
        &rl::simulate<test_mutex_try_lock>,
	
        // futex
        &rl::simulate<test_futex>,
        &rl::simulate<test_futex_deadlock>,
        &rl::simulate<test_futex_sync1>,
        &rl::simulate<test_futex_sync2>,
        &rl::simulate<test_futex_intr>,

        // condition variable
        &rl::simulate<test_condvar>,
        &rl::simulate<test_condvar2>,

        // semaphore
        &rl::simulate<test_semaphore>,
        &rl::simulate<test_semaphore_atomic>,

        // event
        &rl::simulate<test_event_auto>,
        &rl::simulate<test_event_manual>,
        &rl::simulate<test_event_atomic>,

        //wfmo
        &rl::simulate<test_wfmo_all>,
        &rl::simulate<test_wfmo_single>,
        &rl::simulate<test_wfmo_timeout>,
        &rl::simulate<test_wfmo_try>,
        &rl::simulate<test_wfmo_mixed>,
        &rl::simulate<test_wfmo_mixed2>,
        &rl::simulate<test_wfmo_event_all>,
        &rl::simulate<test_wfmo_event_any>,
        &rl::simulate<test_wfmo_atomic>,

        // thread local storage
        &rl::simulate<tls_basic_test>,
        &rl::simulate<tls_reset_test>,
        &rl::simulate<tls_global_test>,
        &rl::simulate<tls_win32_test>,

        // dynamic thread
        &rl::simulate<dyn_thread_basic_test>,
        &rl::simulate<dyn_thread_win32_test>,
        &rl::simulate<dyn_thread_visibility_test>,
#endif
    };

    for (size_t sched = 0; sched != rl::sched_count; ++sched)
    {
        std::cout << format((rl::scheduler_type_e)sched) << " tests:" << std::endl;

        for (size_t i = 0; i != sizeof(tests)/sizeof(*tests); ++i)
        {
            //!!! make it work under sched_full
            if (sched == rl::sched_full
                && (tests[i] == (rl::simulate_f)&rl::simulate<test_pthread_condvar>
                    || tests[i] == (rl::simulate_f)&rl::simulate<test_win_condvar>))
                continue;

            rl::ostringstream stream;
            rl::test_params params;
            params.search_type = (rl::scheduler_type_e)sched;
            params.iteration_count =
                (params.test_result == rl::test_result_success ? 100000 : 500);
            params.output_stream = &stream;
            params.progress_stream = &stream;
            params.context_bound = 2;
            params.execution_depth_limit = 500;

            if (false == tests[i](params))
            {
                std::cout << std::endl;
                std::cout << "FAILED" << std::endl;
                std::cout << stream.str();
                std::cout << std::endl;
                return 1;
            }
            else
            {
                std::cout << params.test_name << "...OK" << std::endl;
            }
        }
        std::cout << std::endl;
    }

    rl::simulate_f scheduler_tests[] = 
    {
        &rl::simulate<livelock_test>,
        &rl::simulate<yield_livelock_test>,
    };

    std::cout << "full search scheduler tests:" << std::endl;
    for (size_t i = 0; i != sizeof(scheduler_tests)/sizeof(*scheduler_tests); ++i)
    {
        rl::ostringstream stream;
        rl::test_params params;
        params.search_type = rl::sched_full;
        params.output_stream = &stream;
        params.progress_stream = &stream;
        params.context_bound = 2;
        params.execution_depth_limit = 500;

        if (false == scheduler_tests[i](params))
        {
            std::cout << std::endl;
            std::cout << "FAILED" << std::endl;
            std::cout << stream.str();
            return 1;
        }
        else
        {
            std::cout << params.test_name << "...OK" << std::endl;
        }
    }
    std::cout << std::endl;

    std::cout << "SUCCESS" << std::endl;
}



