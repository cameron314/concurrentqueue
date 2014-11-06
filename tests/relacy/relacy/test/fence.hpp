#pragma once

#include "../relacy/relacy_std.hpp"


template<int index, int mo_index>
struct fence_synch_test : rl::test_suite<fence_synch_test<index, mo_index>, 2>
{
    std::atomic<int> x;
    rl::var<int> data;

    void before()
    {
        x($) = 0;
    }

    void thread(unsigned th)
    {
        if (0 == th)
        {
            data($) = 1;
            if (0 == index || 1 == index)
            {
                std::atomic_thread_fence(order().first, $);
                x.store(1, std::memory_order_relaxed);
            }
            else
            {
                x.store(1, order().first, $);
            }
        }
        else
        {
            if (0 == index || 2 == index)
            {
                if (x.load(std::memory_order_relaxed))
                {
                    std::atomic_thread_fence(order().second, $);
                    data($).load();
                }
            }
            else
            {
                if (x.load(order().second, $))
                {
                    data($).load();
                }
            }
        }
    }

    std::pair<std::memory_order, std::memory_order> order()
    {
        switch (mo_index)
        {
        default: RL_VERIFY(false);
        case 0: return std::make_pair(std::mo_release, std::mo_acquire);
        case 1: return std::make_pair(std::mo_seq_cst, std::mo_seq_cst);
        }
    }
};




struct two_fence_synch_test : rl::test_suite<two_fence_synch_test, 3>
{
    std::atomic<int> x0;
    std::atomic<int> x1;
    rl::var<int> data0;
    rl::var<int> data1;

    void before()
    {
        x0($) = 0;
        x1($) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            data0($) = 1;
            std::atomic_thread_fence(std::memory_order_release);
            x0.store(1, std::memory_order_relaxed);
        }
        else if (1 == index)
        {
            data1($) = 1;
            std::atomic_thread_fence(std::memory_order_release);
            x1.store(1, std::memory_order_relaxed);
        }
        else
        {
            int y0 = x0.load(std::memory_order_relaxed);
            int y1 = x1.load(std::memory_order_relaxed);
            if (y0 || y1)
            {
                std::atomic_thread_fence(std::memory_order_acquire);
                if (y0)
                    data0($).load();
                if (y1)
                    data1($).load();
            }
        }
    }
};




template<int index>
struct seq_cst_fence_test : rl::test_suite<seq_cst_fence_test<index>, 2,
    (rl::test_result_e)((0 == index) * rl::test_result_success
    + (1 == index) * rl::test_result_until_condition_hit)>
{
    std::atomic<int> x0;
    std::atomic<int> x1;
    rl::var<int> r0;
    rl::var<int> r1;

    void before()
    {
        x0($) = 0;
        x1($) = 0;
    }

    void thread(unsigned th)
    {
        if (0 == th)
        {
            x0.store(1, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            r0($) = x1.load(std::memory_order_relaxed);
        }
        else
        {
            x1.store(1, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            r1($) = x0.load(std::memory_order_relaxed);
        }
    }

    void after()
    {
        if (0 == index)
            RL_ASSERT(r0($) || r1($));
        else if (1 == index)
            RL_UNTIL(r0($) && r1($));
    }
};













