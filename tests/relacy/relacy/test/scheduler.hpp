#pragma once

#include "../relacy/relacy_std.hpp"



struct livelock_test : rl::test_suite<livelock_test, 2, rl::test_result_livelock>
{
    std::atomic<int> x;

    void before()
    {
        x($) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            for (;;)
            {
                int cmp = 1;
                if (x($).compare_exchange_weak(cmp, 2))
                    break;
            }
        }
        else if (1 == index)
        {
            x($).store(1);
        }
    }
};




struct yield_livelock_test : rl::test_suite<yield_livelock_test, 2, rl::test_result_livelock>
{
    std::atomic<int> x, y;

    void before()
    {
        x($) = 0;
        y($) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            rl::backoff b;
            for (;;)
            {
                int cmp = 0;
                if (x($).compare_exchange_weak(cmp, 1))
                {
                    cmp = 0;
                    if (y($).compare_exchange_weak(cmp, 1))
                    {
                        x($).store(0);
                        y($).store(0);
                        break;
                    }
                    else
                    {
                        x($).store(0);
                    }
                }
                b.yield($);
            }
        }
        else if (1 == index)
        {
            rl::backoff b;
            for (;;)
            {
                int cmp = 0;
                if (y($).compare_exchange_weak(cmp, 1))
                {
                    cmp = 0;
                    if (x($).compare_exchange_weak(cmp, 1))
                    {
                        y($).store(0);
                        x($).store(0);
                        break;
                    }
                    else
                    {
                        y($).store(0);
                    }
                }
                b.yield($);
            }
        }
    }
};




struct sched_load_test : rl::test_suite<sched_load_test, 2>
{
    std::recursive_mutex mtx1, mtx2;
    std::condition_variable_any cv1, cv2;
    VAR_T(int) data1, data2;

    void before()
    {
    }

    void thread(unsigned index)
    {
        if (index % 2)
        {
            mtx1.lock($);
            VAR(data1) = 1;
            mtx1.unlock($);

            mtx2.lock($);
            mtx2.lock($);
            VAR(data2) = 1;
            mtx2.unlock($);
            mtx2.unlock($);

            if (mtx1.try_lock($))
            {
                //mtx1.lock($);
                VAR(data1) = 1;
                //mtx1.unlock($);
                mtx1.unlock($);
            }

            mtx1.lock($);
            VAR(data1) = 2;
            cv1.notify_all($);
            mtx1.unlock($);

            mtx2.lock($);
            while (VAR(data2) != 2)
            {
                rl::yield(1, $);
                cv2.wait_for(mtx2, 1, $);
            }
            mtx2.unlock($);
        }
        else
        {
            mtx2.lock($);
            VAR(data2) = 1;
            mtx2.unlock($);

            mtx1.lock($);
            mtx1.lock($);
            VAR(data1) = 1;
            mtx1.unlock($);
            mtx1.unlock($);

            if (mtx2.try_lock($))
            {
                //mtx2.lock($);
                VAR(data2) = 1;
                //mtx2.unlock($);
                mtx2.unlock($);
            }

            mtx2.lock($);
            VAR(data2) = 2;
            mtx2.unlock($);
            cv2.notify_all($);

            mtx1.lock($);
            while (VAR(data1) != 2)
            {
                rl::yield(1, $);
                cv1.wait_for(mtx1, 1, $);
            }
            mtx1.unlock($);
        }
    }
};



