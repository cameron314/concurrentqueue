#pragma once

#include "../relacy/relacy.hpp"
#include "../relacy/dyn_thread.hpp"



struct dyn_thread_basic_test : rl::test_suite<dyn_thread_basic_test, 2>
{
    static unsigned const dynamic_thread_count = 4;

    rl::var<int> data1;
    rl::var<int> data2;
    rl::atomic<int> data3;

    void before()
    {
        data3($) = 0;
    }

    static void* thread1(void* p)
    {
        dyn_thread_basic_test& self = *(dyn_thread_basic_test*)p;
        self.data1($) = 1;
        return 0;
    }

    static void* thread2(void* p)
    {
        dyn_thread_basic_test& self = *(dyn_thread_basic_test*)p;
        self.data2($) = 2;
        return 0;
    }

    static void* thread3(void* p)
    {
        dyn_thread_basic_test& self = *(dyn_thread_basic_test*)p;
        self.data3.store(3, rl::memory_order_relaxed);
        return 0;
    }

    void thread(unsigned index)
    {
        if (index == 0)
        {
            rl::dyn_thread t1;
            t1.start(&dyn_thread_basic_test::thread1, this);
            rl::dyn_thread t2;
            t2.start(&dyn_thread_basic_test::thread2, this);
            t1.join();
            t2.join();
            RL_ASSERT(data1($) == 1);
            RL_ASSERT(data2($) == 2);
        }
        else if (index == 1)
        {
            rl::dyn_thread t1;
            t1.start(&dyn_thread_basic_test::thread3, this);
            while (data3.load(rl::memory_order_relaxed) != 3)
                rl::yield(1, $);
            t1.join();
        }
        else
        {
            RL_ASSERT(false);
        }
    }
};




struct dyn_thread_win32_test : rl::test_suite<dyn_thread_win32_test, 2>
{
    static unsigned const dynamic_thread_count = 4;

    rl::var<int> data1;
    rl::var<int> data2;
    rl::atomic<int> data3;

    void before()
    {
        data3($) = 0;
    }

    static unsigned long RL_STDCALL thread1(void* p)
    {
        dyn_thread_win32_test& self = *(dyn_thread_win32_test*)p;
        self.data1($) = 1;
        return 0;
    }

    static unsigned long RL_STDCALL thread2(void* p)
    {
        dyn_thread_win32_test& self = *(dyn_thread_win32_test*)p;
        self.data2($) = 2;
        return 0;
    }

    static unsigned long RL_STDCALL thread3(void* p)
    {
        dyn_thread_win32_test& self = *(dyn_thread_win32_test*)p;
        self.data3.store(3, rl::memory_order_relaxed);
        return 0;
    }

    void thread(unsigned index)
    {
        if (index == 0)
        {
            HANDLE threads [2];
            threads[0] = CreateThread(0, 0, &dyn_thread_win32_test::thread1, this, 0, 0);
            threads[1] = CreateThread(0, 0, &dyn_thread_win32_test::thread2, this, 0, 0);
            WaitForMultipleObjects(2, threads, 1, INFINITE);
            RL_ASSERT(VAR(data1) == 1);
            RL_ASSERT(VAR(data2) == 2);
        }
        else if (index == 1)
        {
            HANDLE th = CreateThread(0, 0, &dyn_thread_win32_test::thread3, this, 0, 0);
            while (data3.load(rl::memory_order_relaxed) != 3)
                rl::yield(1, $);
            WaitForSingleObject(th, INFINITE);
        }
        else
        {
            RL_ASSERT(false);
        }
    }
};


struct dyn_thread_visibility_test : rl::test_suite<dyn_thread_visibility_test, 1>
{
    static unsigned const dynamic_thread_count = 1;

    rl::var<int> data;

    static unsigned long RL_STDCALL thread(void* p)
    {
        dyn_thread_visibility_test& self = *(dyn_thread_visibility_test*)p;
        RL_ASSERT(self.data($) == 1);
        self.data($) = 2;
        return 0;
    }

    void thread(unsigned /*index*/)
    {
        data($) = 1;
        HANDLE th = CreateThread(0, 0, &dyn_thread_visibility_test::thread, this, 0, 0);
        WaitForSingleObject(th, INFINITE);
        RL_ASSERT(data($) == 2);
    }
};

