#pragma once

#include "../relacy/relacy_std.hpp"



template<int T>
struct cas_spurious_fail_test : rl::test_suite<cas_spurious_fail_test<T>, 1, rl::test_result_until_condition_hit>
{
    std::atomic<int> x;
    std::atomic<int> y;

    void before()
    {
        x.store(0, std::memory_order_relaxed);
        y.store(0, std::memory_order_relaxed);
    }

    void thread(unsigned /*index*/)
    {
        int cmp = 0;
        if (x.compare_exchange_weak(cmp, 1, std::memory_order_seq_cst, std::memory_order_seq_cst))
        {
            cmp = 1;
            if (x.compare_exchange_weak(cmp, 2, std::memory_order_seq_cst))
            {
                cmp = 0;
                if (y.compare_exchange_weak(cmp, 1, std::memory_order_seq_cst))
                {
                }
                else
                {
                    if (T == 2) RL_UNTIL(true);
                }
            }
            else
            {
                if (T == 1) RL_UNTIL(true);
            }
        }
        else
        {
            if (T == 0) RL_UNTIL(true);
        }
    }
};

