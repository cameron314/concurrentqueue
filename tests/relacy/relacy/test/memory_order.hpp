#pragma once

#include "../relacy/relacy_std.hpp"

struct coherent_read_read_test : rl::test_suite<coherent_read_read_test, 3>
{
    std::atomic<int> x;
    std::atomic<int> y;

    void before()
    {
        x($) = 0;
        y($) = 0;
    }

    void thread(unsigned th)
    {
        if (0 == th)
            x.store(1, rl::memory_order_relaxed);
        else if (1 == th)
        {
            if (0 == x.load(rl::memory_order_relaxed))
                return;
            y.store(1, rl::memory_order_release);
            x.load(rl::memory_order_relaxed);
        }
        else
        {
            if (0 == y.load(rl::memory_order_acquire))
                return;
            RL_ASSERT(1 == x.load(rl::memory_order_relaxed));
        }
    }
};


template<int index>
struct order_relaxed_test : rl::test_suite<order_relaxed_test<index>, 2>
{
    std::atomic<int> x1;
    std::atomic<int> x2;

    void before()
    {
        x1($) = 0;
        x2($) = 0;
    }

    void thread(unsigned th)
    {
        if (th)
        {
            x1.store(1, order().first, $);
            x2.store(1, order().first, $);
        }
        else
        {
            int y2 = x2.load(order().second, $);
            int y1 = x1.load(order().second, $);
            //RL_UNTIL(0 == y1 && 0 != y2);
            (void)y2;
            (void)y1;
        }
    }

    std::pair<rl::memory_order, rl::memory_order> order()
    {
        switch (index)
        {
        default: RL_VERIFY(false);
        case 0: return std::make_pair(rl::mo_relaxed, rl::mo_relaxed);
        case 1: return std::make_pair(rl::mo_release, rl::mo_relaxed);
        case 2: return std::make_pair(rl::mo_seq_cst, rl::mo_relaxed);
        case 3: return std::make_pair(rl::mo_relaxed, rl::mo_acquire);
        case 4: return std::make_pair(rl::mo_relaxed, rl::mo_seq_cst);
        }
    }
};




struct reorder_single_var_test : rl::test_suite<reorder_single_var_test, 2>
{
    std::atomic<int> x;

    void before()
    {
        x($) = 0;
    }

    void thread(unsigned index)
    {
        if (index)
        {
            x.store(1, rl::memory_order_relaxed);
        }
        else
        {
            int y1 = x.load(rl::memory_order_relaxed);
            int y2 = x.load(rl::memory_order_relaxed);
            RL_ASSERT(y1 == 0 || y2 == 1);
        }
    }
};




struct acq_rel_test : rl::test_suite<acq_rel_test, 2>
{
    std::atomic<int> x;
    rl::var<int> y;

    void before()
    {
        x($) = 0;
    }

    void thread(unsigned index)
    {
        if (index)
        {
            VAR(y) = 1;
            x.store(1, std::memory_order_release);
        }
        else
        {
            int f = x.load(rl::memory_order_acquire);
            if (f)
            {
                int d = VAR(y);
                RL_ASSERT(1 == d);
            }
        }
    }
};




template<int index>
struct seq_cst_test : rl::test_suite<seq_cst_test<index>, 4,
    (rl::test_result_e)((1 - index) * rl::test_result_until_condition_hit)>
{
    std::atomic<int> x1;
    std::atomic<int> x2;

    int res;

    void before()
    {
        x1($) = 0;
        x2($) = 0;
        res = 0;
    }

    void thread(unsigned th)
    {
        if (0 == th)
        {
            x1.store(1, order().first, $);
        }
        else if (1 == th)
        {
            x2.store(1, order().first, $);
        }
        else if (2 == th)
        {
            int v1 = x1.load(order().second, $);
            int v2 = x2.load(order().second, $);
            res += (v1 == 1 && v2 == 0);
        }
        else if (3 == th)
        {
            int v2 = x2.load(order().second, $);
            int v1 = x1.load(order().second, $);
            res += (v2 == 1 && v1 == 0);
        }
    }

    void after()
    {
        if ((void)0, 0 == index)
        {
            RL_UNTIL(2 == res);
        }
        else
        {
            RL_ASSERT(2 != res);
        }
    }

    std::pair<rl::memory_order, rl::memory_order> order()
    {
        switch (index)
        {
        default: RL_VERIFY(false);
        case 0: return std::make_pair(rl::mo_release, rl::mo_acquire);
        case 1: return std::make_pair(rl::mo_seq_cst, rl::mo_seq_cst);
        }
    }
};

struct modification_order_test : rl::test_suite<modification_order_test, 2>
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
            x($) = 1;
            a.store(1, rl::memory_order_release);
            a.store(2, rl::memory_order_relaxed);
        }
        else
        {
            if (a.load(rl::memory_order_acquire))
                x($).load();
        }
    }
};

struct reordering_test : rl::test_suite<reordering_test, 3>
{
    std::atomic<int> x;
    std::atomic<int> y;
    std::atomic<int> r;

    void before()
    {
        x($) = 0;
        y($) = 0;
        r($) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            x.store(1, rl::memory_order_relaxed);
        }
        else if (1 == index)
        {
            if (x.load(rl::memory_order_relaxed))
                r.store(1, rl::memory_order_relaxed);
            y.store(1, rl::memory_order_release);
        }
        else
        {
            if (y.load(rl::memory_order_acquire))
            {
                if (r.load(rl::memory_order_relaxed))
                {
                    RL_ASSERT(x.load(rl::memory_order_relaxed));
                }
            }
        }
    }
};

struct reordering_test2 : rl::test_suite<reordering_test2, 3, rl::test_result_until_condition_hit>
{
    std::atomic<int> x1;
    std::atomic<int> x2;
    std::atomic<int> y;
    std::atomic<int> r;

    void before()
    {
        std::atomic<char*> x (0);
        char* ch = 0;
        x.compare_exchange_weak(ch, 0, std::memory_order_seq_cst);

        x1($) = 0;
        x2($) = 0;
        y($) = 0;
        r($) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            x1.store(1, rl::memory_order_relaxed);
            x2.store(1, rl::memory_order_relaxed);
        }
        else if (1 == index)
        {
            if (x2.load(rl::memory_order_relaxed))
                r.store(1, rl::memory_order_relaxed);
            y.store(1, rl::memory_order_release);
        }
        else
        {
            if (y.load(rl::memory_order_acquire))
            {
                if (r.load(rl::memory_order_relaxed))
                {
                    RL_UNTIL(0 == x1.load(rl::memory_order_relaxed));
                }
            }
        }
    }
};

struct transitive_test : rl::test_suite<transitive_test, 3>
{
    std::atomic<int> x;
    rl::var<int> y;

    void before()
    {
        x($) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            VAR(y) = 1;
            x.fetch_add(1, rl::memory_order_release);
        }
        else if (1 == index)
        {
            x.fetch_add(2, rl::memory_order_acquire);
        }
        else
        {
            x.load(rl::memory_order_acquire);
            int w = x.load(rl::memory_order_acquire);
            if (1 == w || 3 == w)
            {
                y($).load();
            }
        }
    }
};


struct cc_transitive_test : rl::test_suite<cc_transitive_test, 3>
{
    std::atomic<int> x;
    std::atomic<int> y;

    void before()
    {
        x.store(0, std::memory_order_relaxed);
        y.store(0, std::memory_order_relaxed);
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            x.store(1, std::memory_order_relaxed);
        }
        else if (1 == index)
        {
            if (x.load(std::memory_order_relaxed))
                y.store(1, std::memory_order_release);
        }
        else
        {
            if (y.load(std::memory_order_acquire))
                assert(x.load(std::memory_order_relaxed));
        }
    }
};


struct occasional_test : rl::test_suite<occasional_test, 3, rl::test_result_until_condition_hit>
{
    std::atomic<int> x, y, z;

    void before()
    {
        x.store(0, std::memory_order_relaxed);
        y.store(0, std::memory_order_relaxed);
        z.store(0, std::memory_order_relaxed);
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            x.store(1, rl::memory_order_relaxed);
            y.store(1, rl::memory_order_release);
        }
        else if (1 == index)
        {
            if (y.load(rl::memory_order_relaxed))
                z.store(1, rl::memory_order_release);
        }
        else
        {
            if (z.load(rl::memory_order_acquire))
            {
                RL_ASSERT(y.load(rl::memory_order_relaxed));
                RL_UNTIL(0 == x.load(rl::memory_order_relaxed));
            }
        }
    }
};



