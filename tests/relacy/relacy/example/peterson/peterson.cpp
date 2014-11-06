#include "stdafx.h"
#include "../../relacy/relacy_std.hpp"
#include "../../relacy/windows.h"


struct peterson_mutex_test : rl::test_suite<peterson_mutex_test, 2>
{
    std::atomic<int> flag0;
    std::atomic<int> flag1;
    std::atomic<int> turn;

    rl::var<int> data;

    void before()
    {
        flag0($) = 0;
        flag1($) = 0;
        turn($) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            flag0($).store(1);
            turn($).store(1);

            while (flag1($).load()
                && 1 == turn($).load());

            data($) = 1;

            flag0($).store(0);
        }
        else
        {
            flag1($).store(1);
            turn($).store(0);

            while (flag0($).load()
                && 0 == turn($).load());

            data($) = 2;

            flag1($).store(0);
        }
    }
};




struct peterson_mutex_test2 : rl::test_suite<peterson_mutex_test2, 2>
{
    std::atomic<int> flag0;
    std::atomic<int> flag1;
    std::atomic<int> turn;

    rl::var<int> data;

    void before()
    {
        flag0($) = 0;
        flag1($) = 0;
        turn($) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            flag0.store(1, rl::memory_order_relaxed);
            turn.exchange(1, rl::memory_order_acq_rel);

            while (flag1.load(rl::memory_order_acquire)
                && 1 == turn.load(rl::memory_order_relaxed))
                rl::yield(1, $);

            data($) = 1;

            flag0.store(0, rl::memory_order_release);
        }
        else
        {
            flag1.store(1, rl::memory_order_relaxed);
            turn.exchange(0, rl::memory_order_acq_rel);

            while (flag0.load(rl::memory_order_acquire)
                && 0 == turn.load(rl::memory_order_relaxed))
                rl::yield(1, $);

            data($) = 2;

            flag1.store(0, rl::memory_order_release);
        }
    }
};




struct peterson_mutex_test3 : rl::test_suite<peterson_mutex_test3, 2>
{
    std::atomic<int> flag0;
    std::atomic<int> flag1;
    std::atomic<int> turn;

    rl::var<int> data;

    void before()
    {
        flag0($) = 0;
        flag1($) = 0;
        turn($) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            flag0.store(1, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            turn.store(1, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_seq_cst);

            while (flag1.load(std::memory_order_acquire)
                && 1 == turn.load(std::memory_order_relaxed));

            data($) = 1;

            flag0.store(0, std::memory_order_release);
        }
        else
        {
            flag1.store(1, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            turn.store(0, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_seq_cst);

            while (flag0.load(std::memory_order_acquire)
                && 0 == turn.load(std::memory_order_relaxed));

            data($) = 2;

            flag1.store(0, std::memory_order_release);
        }
    }
};



// FAILS WITH DATA RACE
struct peterson_mutex_test4 : rl::test_suite<peterson_mutex_test4, 2>
{
    std::atomic<int> flag0;
    std::atomic<int> flag1;
    std::atomic<int> turn;

    rl::var<int> data;

    void before()
    {
        flag0($) = 0;
        flag1($) = 0;
        turn($) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            flag0.exchange(1, rl::memory_order_acq_rel);
            turn.store(1, rl::memory_order_release);

            while (flag1.load(rl::memory_order_acquire)
                && 1 == turn.load(rl::memory_order_acquire))
                rl::yield(1, $);

            data($) = 1;

            flag0.store(0, rl::memory_order_release);
        }
        else
        {
            flag1.exchange(1, rl::memory_order_acq_rel);
            turn.store(0, rl::memory_order_release);

            while (flag0.load(rl::memory_order_acquire)
                && 0 == turn.load(rl::memory_order_relaxed))
                rl::yield(1, $);

            data($) = 2;

            flag1.store(0, rl::memory_order_release);
        }
    }
};


class eventcount
{
public:
    typedef unsigned state_t;

    eventcount()
    {
        state_.store(0, std::memory_order_relaxed);
        sema_ = CreateSemaphore(0, 0, LONG_MAX, 0);
    }

    ~eventcount()
    {
        CloseHandle(sema_);
    }

    state_t prepare()
    {
        return state_.fetch_add(waiters_inc, std::memory_order_seq_cst);
    }

    void retire()
    {
        state_.fetch_add((state_t)-(int)waiters_inc, std::memory_order_seq_cst);
    }

    void wait(state_t cmp)
    {
        WaitForSingleObject(sema_, INFINITE);
        state_t cmp0 = state_.load(std::memory_order_seq_cst);
        if ((cmp & generation_mask) == (cmp0 & generation_mask))
        {
            state_.fetch_add((state_t)-(int)waiters_inc, std::memory_order_seq_cst);
            ReleaseSemaphore(sema_, 1, 0);
            SwitchToThread();
        }
    }

    void signal()
    {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        signal_relaxed();
    }

    void signal_relaxed()
    {
        state_t cmp = state_.load(std::memory_order_seq_cst);
        if (0 == (cmp & waiters_mask))
            return;
        for (;;)
        {
            state_t xchg = (cmp & ~waiters_mask) + generation_inc;
            if (state_.compare_exchange_weak(cmp, xchg, std::memory_order_seq_cst))
            {
                ReleaseSemaphore(sema_, cmp & waiters_mask, 0);
                return;
            }
            if (0 == (cmp & waiters_mask))
                return;
        }
    }
    
private:
    std::atomic<state_t> state_;
    HANDLE sema_;

    static state_t const waiters_inc = 1;
    static state_t const waiters_mask = (1 << 20) - 1;
    static state_t const generation_inc = 1 << 20;
    static state_t const generation_mask = ~waiters_mask;

    eventcount(eventcount const&);
    eventcount& operator = (eventcount const&);
};




class eventcount_blocking
{
public:
    eventcount_blocking(eventcount& ec)
        : ec_(ec)
    {
        cmp_ = ec_.prepare();
        wait_ = false;
    }

    void wait()
    {
        RL_ASSERT(false == wait_);
        wait_ = true;
        ec_.wait(cmp_);
    }

    ~eventcount_blocking()
    {
        if (false == wait_)
            ec_.retire();
    }

private:
    eventcount& ec_;
    eventcount::state_t cmp_;
    bool wait_;

    eventcount_blocking(eventcount_blocking const&);
    eventcount_blocking& operator = (eventcount_blocking const&);
};





struct signaling_test : rl::test_suite<signaling_test, 6>
{
    //rl::HANDLE              var_wait_for_items;
    //rl::CRITICAL_SECTION    mtx_items_avail;
    //std::atomic<unsigned>   n_waiting_consumers;
    //rl::var<unsigned>       consumer_wait_generation;
    //rl::var<unsigned>       n_consumers_to_wakeup;

    eventcount ec_;

    static int const max_queue_length = 4;
    int queue [max_queue_length];
    int queue_head;
    int queue_tail;
    int queue_head_data;
    int queue_tail_data;

    void before()
    {
        //var_wait_for_items = rl::CreateEvent(0, 1, 0, 0, $);
        //rl::InitializeCriticalSection(&mtx_items_avail, $);
        //n_waiting_consumers($) = 0;
        //consumer_wait_generation($) = 0;
        //n_consumers_to_wakeup($) = 0;
        for (int i = 0; i != max_queue_length; ++i)
            queue[i] = 0;
        queue_head = 0;
        queue_tail = 0;
        queue_head_data = 0;
        queue_tail_data = 0;
    }

    void after()
    {
        //rl::CloseHandle(var_wait_for_items, $);
        //rl::DeleteCriticalSection(&mtx_items_avail, $);
    }

    struct enqueue_desc
    {
        int pos;

        void output(std::ostream& s) const
        {
            s << "enqueue " << pos;
        }
    };

    void enqueue()
    {
        queue[queue_head++] = ++queue_head_data;
        RL_HIST_IMPL(rl::ctx(), $, enqueue_desc) {queue_head - 1} RL_HIST_END();
        signal();
    }

    void dequeue()
    {
        int my_pos = queue_tail++;
        for (;;)
        {
            if (queue[my_pos])
            {
                RL_ASSERT(queue[my_pos] == my_pos + 1);
                return;
            }
            wait(my_pos);
        }
    }

    void signal()
    {
        ec_.signal();
        /*
        std::atomic_thread_fence($)(std::memory_order_seq_cst);
        if (n_waiting_consumers($).load(std::memory_order_relaxed))
        {
            rl::EnterCriticalSection(&mtx_items_avail, $);
            if (n_waiting_consumers($).load(std::memory_order_relaxed) > 0)
            {
                consumer_wait_generation($) += 1;
                //RL_ASSERT(n_consumers_to_wakeup($) == 0);
                n_consumers_to_wakeup($) = n_waiting_consumers($).load(std::memory_order_relaxed);
                rl::SetEvent(var_wait_for_items, $);
            }
            rl::LeaveCriticalSection(&mtx_items_avail, $);
        }
        */
    }

    void wait(int my_pos)
    {
        eventcount_blocking block (ec_);
        if (queue[my_pos])
            return;
        block.wait();

        /*
        rl::EnterCriticalSection(&mtx_items_avail, $);
        n_waiting_consumers($).store(n_waiting_consumers($).load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        std::atomic_thread_fence($)(std::memory_order_seq_cst);
        while (0 == queue[my_pos])
        {
            unsigned my_generation = consumer_wait_generation($);
            for (;;)
            {
                rl::LeaveCriticalSection(&mtx_items_avail, $);
                rl::WaitForSingleObject(var_wait_for_items, rl::RL_INFINITE, $);
                rl::EnterCriticalSection(&mtx_items_avail, $);
                if (n_consumers_to_wakeup($) > 0 && consumer_wait_generation($) != my_generation)
                    break;
            }
            if (--n_consumers_to_wakeup($) == 0)
                rl::ResetEvent(var_wait_for_items, $);
        }
        n_waiting_consumers($).store(n_waiting_consumers($).load(std::memory_order_relaxed) - 1, std::memory_order_relaxed);
        rl::LeaveCriticalSection(&mtx_items_avail, $);
        */
    }

    void thread(unsigned index)
    {
        if (index < rl::test_suite<signaling_test, 6>::params::thread_count/2+1)
        {
            enqueue();
        }
        else
        {
            dequeue();
        }
    }
};




int main()
{
    rl::test_params p;
    //p.search_type = rl::fair_context_bound_scheduler_type;
    p.search_type = rl::sched_bound;
    //p.context_bound = 1;
    //p.execution_depth_limit = 100;
    //p.iteration_count = 5000;
    //p.initial_state = "280572";
    //rl::simulate<signaling_test>(p);

    rl::simulate<peterson_mutex_test>();
    rl::simulate<peterson_mutex_test2>(p);
    rl::simulate<peterson_mutex_test3>();
    rl::simulate<peterson_mutex_test4>(p);
}





