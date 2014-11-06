#pragma once

#include "../relacy/relacy_std.hpp"




struct race_ld_ld_test : rl::test_suite<race_ld_ld_test, 2>
{
    rl::var<int> x;

    void before()
    {
        x($) = 0;
    }

    void thread(unsigned index)
    {
        if (index)
            x($).load();
        else
            x($).load();
    }
};




struct race_ld_st_test : rl::test_suite<race_ld_st_test, 2, rl::test_result_data_race>
{
    rl::var<int> x;

    void before()
    {
        x($) = 0;
    }

    void thread(unsigned index)
    {
        if (index)
            x($).load();
        else
            x($).store(1);
    }
};




struct race_st_st_test : rl::test_suite<race_st_st_test, 2, rl::test_result_data_race>
{
    rl::var<int> x;

    void thread(unsigned index)
    {
        if (index)
            x($).store(1);
        else
            x($).store(1);
    }
};




struct race_seq_ld_ld_test : rl::test_suite<race_seq_ld_ld_test, 2>
{
    std::atomic<int> a;
    rl::var<int> x;

    void before()
    {
        a($) = 0;
        x($) = 0;
    }

    void thread(unsigned index)
    {
        if (index)
        {
            x($).load();
            a.store(1, std::memory_order_relaxed);
        }
        else
        {
            rl::backoff b;
            while (0 == a.load(std::memory_order_relaxed))
                b.yield($);
            x($).load();
        }
    }
};




struct race_seq_ld_st_test : rl::test_suite<race_seq_ld_st_test, 2, rl::test_result_data_race>
{
    std::atomic<int> a;
    rl::var<int> x;

    void before()
    {
        a($) = 0;
        x($) = 0;
    }

    void thread(unsigned index)
    {
        if (index)
        {
            x($).load();
            a.store(1, std::memory_order_relaxed);
        }
        else
        {
            rl::backoff b;
            while (0 == a.load(std::memory_order_relaxed))
                b.yield($);
            x($).store(1);
        }
    }
};




struct race_seq_st_ld_test : rl::test_suite<race_seq_st_ld_test, 2, rl::test_result_data_race>
{
    std::atomic<int> a;
    rl::var<int> x;

    void before()
    {
        a($) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            x($).store(1);
            a.store(1, std::memory_order_relaxed);
        }
        else
        {
            rl::backoff b;
            while (0 == a.load(std::memory_order_relaxed))
                b.yield($);
            x($).load();
        }
    }
};




struct race_seq_st_st_test : rl::test_suite<race_seq_st_st_test, 2, rl::test_result_data_race>
{
    std::atomic<int> a;
    rl::var<int> x;

    void before()
    {
        a($) = 0;
    }

    void thread(unsigned index)
    {
        if (index)
        {
            x($).store(1);
            a.store(1, std::memory_order_relaxed);
        }
        else
        {
            rl::backoff b;
            while (0 == a.load(std::memory_order_relaxed))
                b.yield($);
            VAR(x) = 1;
        }
    }
};




struct race_uninit_test : rl::test_suite<race_uninit_test, 2, rl::test_result_unitialized_access>
{
    std::atomic<int> a;
    std::atomic<int> x;

    void before()
    {
        a($) = 0;
    }

    void thread(unsigned index)
    {
        if (index)
        {
            x.store(1, std::memory_order_relaxed);
            a.store(1, std::memory_order_relaxed);
        }
        else
        {
            rl::backoff b;
            while (0 == a.load(std::memory_order_relaxed))
                b.yield($);
            x.load(std::memory_order_seq_cst);
        }
    }
};




struct race_indirect_test : rl::test_suite<race_indirect_test, 2, rl::test_result_data_race>
{
    std::atomic<int> a;
    rl::var<int> x;

    void before()
    {
        a($) = 0;
        x($) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            x($) = 1;
            a.store(1, std::memory_order_release);
            (void)(int)x($);
        }
        else
        {
            rl::backoff b;
            while (0 == a.load(std::memory_order_acquire))
                b.yield($);
            (void)(int)x($);
            x($) = 2;
        }
    }
};

