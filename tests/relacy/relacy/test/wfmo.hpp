#pragma once

#include "../relacy/relacy_std.hpp"



struct test_wfmo_all : rl::test_suite<test_wfmo_all, 2>
{
    HANDLE sema1;
    HANDLE sema2;
    rl::var<int> data;

    void before()
    {
        sema1 = CreateSemaphore(0, 0, 2, 0);
        sema2 = CreateSemaphore(0, 0, 2, 0);
        data($) = 0;
    }

    void after()
    {
        CloseHandle(sema1);
        CloseHandle(sema2);
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            HANDLE handles [2] = {sema1, sema2};
            WaitForMultipleObjects(2, handles, 1, INFINITE);
            RL_ASSERT(data($) == 2);
        }
        else
        {
            data($) = 1;
            ReleaseSemaphore(sema1, 1, 0);
            data($) = 2;
            ReleaseSemaphore(sema2, 1, 0);
        }
    }
};




struct test_wfmo_single : rl::test_suite<test_wfmo_single, 2, rl::test_result_until_condition_hit>
{
    HANDLE sema1;
    HANDLE sema2;
    rl::atomic<int> data;

    void before()
    {
        sema1 = CreateSemaphore(0, 0, 2, 0);
        sema2 = CreateSemaphore(0, 0, 2, 0);
        data($) = 0;
    }

    void after()
    {
        CloseHandle(sema1);
        CloseHandle(sema2);
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            HANDLE handles [2] = {sema1, sema2};
            WaitForMultipleObjects(2, handles, 0, INFINITE);
            int d = data.load(rl::memory_order_relaxed);
            RL_ASSERT(d == 1 || d == 2);
            RL_UNTIL(d == 1);
        }
        else
        {
            data.store(1, rl::memory_order_relaxed);
            ReleaseSemaphore(sema1, 1, 0);
            data.store(2, rl::memory_order_relaxed);
            ReleaseSemaphore(sema2, 1, 0);
        }
    }
};




struct test_wfmo_timeout : rl::test_suite<test_wfmo_timeout, 2, rl::test_result_until_condition_hit>
{
    HANDLE sema1;
    HANDLE sema2;
    rl::atomic<int> data;

    void before()
    {
        sema1 = CreateSemaphore(0, 0, 2, 0);
        sema2 = CreateSemaphore(0, 0, 2, 0);
        data($) = 0;
    }

    void after()
    {
        CloseHandle(sema1);
        CloseHandle(sema2);
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            HANDLE handles [2] = {sema1, sema2};
            WaitForMultipleObjects(2, handles, 0, 100);
            int d = data.load(rl::memory_order_relaxed);
            RL_ASSERT(d == 0 || d == 1 || d == 2);
            RL_UNTIL(d == 0);
        }
        else
        {
            data.store(1, rl::memory_order_relaxed);
            ReleaseSemaphore(sema1, 1, 0);
            data.store(2, rl::memory_order_relaxed);
            ReleaseSemaphore(sema2, 1, 0);
        }
    }
};




struct test_wfmo_try : rl::test_suite<test_wfmo_try, 2>
{
    HANDLE sema1;
    HANDLE sema2;
    rl::atomic<int> d;
    rl::atomic<int> d1;
    rl::atomic<int> d2;

    void before()
    {
        sema1 = CreateSemaphore(0, 1, 2, 0);
        sema2 = CreateSemaphore(0, 1, 2, 0);
        d1($) = 0;
        d2($) = 0;
    }

    void after()
    {
        CloseHandle(sema1);
        CloseHandle(sema2);
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            d1.store(1, rl::memory_order_relaxed);
            HANDLE handles [2] = {sema1, sema2};
            if (WAIT_TIMEOUT == WaitForMultipleObjects(2, handles, 1, 0))
                RL_ASSERT(1 == d2.load(rl::memory_order_relaxed));
        }
        else if (1 == index)
        {
            d2.store(1, rl::memory_order_relaxed);
            HANDLE handles [2] = {sema2, sema1};
            if (WAIT_TIMEOUT == WaitForMultipleObjects(2, handles, 1, 0))
                RL_ASSERT(1 == d1.load(rl::memory_order_relaxed));
        }
    }
};




struct test_wfmo_mixed : rl::test_suite<test_wfmo_mixed, 3>
{
	HANDLE sem [2];
	
	void before()
	{
		sem[0] = CreateSemaphore(0, 0, 2, 0);
		sem[1] = CreateSemaphore(0, 0, 2, 0);
	}
	
	void after()
	{
		CloseHandle(sem[0]);
		CloseHandle(sem[1]);
	}
	
	void thread(unsigned index)
	{
		if (0 == index)
		{
			ReleaseSemaphore(sem[0], 1, 0);
			ReleaseSemaphore(sem[0], 1, 0);
			ReleaseSemaphore(sem[1], 1, 0);
		}
		else if (1 == index)
		{
			unsigned rv = WaitForMultipleObjects(2, sem, 1, INFINITE);
			assert(rv == WAIT_OBJECT_0);
		}
		else if (2 == index)
		{
			unsigned rv = WaitForSingleObject(sem[0], INFINITE);
			assert(rv == WAIT_OBJECT_0);
		}
	}
};




struct test_wfmo_mixed2 : rl::test_suite<test_wfmo_mixed2, 4>
{
	HANDLE sem [2];
	
	void before()
	{
		sem[0] = CreateSemaphore(0, 0, 2, 0);
		sem[1] = CreateSemaphore(0, 0, 2, 0);
	}
	
	void after()
	{
		CloseHandle(sem[0]);
		CloseHandle(sem[1]);
	}
	
	void thread(unsigned index)
	{
		if (0 == index)
		{
			ReleaseSemaphore(sem[1], 1, 0);
			ReleaseSemaphore(sem[0], 1, 0);
			ReleaseSemaphore(sem[0], 1, 0);
		}
		else if (1 == index)
		{
			unsigned rv = WaitForSingleObject(sem[0], INFINITE);
			assert(rv == WAIT_OBJECT_0);
		}
		else if (2 == index || 3 == index)
		{
			unsigned rv = WaitForMultipleObjects(2, sem, 1, 42);
			assert(rv == WAIT_OBJECT_0 || rv == WAIT_TIMEOUT);
		}
	}
};




struct test_wfmo_event_all : rl::test_suite<test_wfmo_event_all, 2>
{
	HANDLE ev [2];
	rl::atomic<int> state;
	
	void before()
	{
		ev[0] = CreateEvent(0, 0, 0, 0);
		ev[1] = CreateEvent(0, 1, 0, 0);
		state.store(0, rl::memory_order_relaxed);
	}
	
	void after()
	{
		CloseHandle(ev[0]);
		CloseHandle(ev[1]);
	}
	
	void thread(unsigned index)
	{
		if (0 == index)
		{
			unsigned rv = WaitForMultipleObjects(2, ev, 1, INFINITE);
			assert(rv == WAIT_OBJECT_0 + 0 || rv == WAIT_OBJECT_0 + 1);
			assert(state.load(rl::memory_order_relaxed) == 1);
		}
		else if (1 == index)
		{
			SetEvent(ev[0]);
			state.store(1, rl::memory_order_relaxed);
			SetEvent(ev[1]);
		}
	}
};




struct test_wfmo_event_any : rl::test_suite<test_wfmo_event_any, 2>
{
	HANDLE ev [2];
	rl::atomic<int> state;
	
	void before()
	{
		ev[0] = CreateEvent(0, 0, 0, 0);
		ev[1] = CreateEvent(0, 1, 0, 0);
		state.store(0, rl::memory_order_relaxed);
	}
	
	void after()
	{
		CloseHandle(ev[0]);
		CloseHandle(ev[1]);
	}
	
	void thread(unsigned index)
	{
		if (0 == index)
		{
			unsigned rv = WaitForMultipleObjects(2, ev, 0, INFINITE);
			assert(rv == WAIT_OBJECT_0 + 0 || rv == WAIT_OBJECT_0 + 1);
			assert(state.load(rl::memory_order_relaxed) == 1);
		}
		else if (1 == index)
		{
			state.store(1, rl::memory_order_relaxed);
			SetEvent(ev[0]);
			SetEvent(ev[1]);
		}
	}
};




struct test_wfmo_atomic : rl::test_suite<test_wfmo_atomic, 2, rl::test_result_until_condition_hit>
{
	HANDLE ev [2];
	rl::atomic<int> state;
	
	void before()
	{
		ev[0] = CreateEvent(0, 0, 0, 0);
		ev[1] = CreateEvent(0, 0, 0, 0);
	}
	
	void after()
	{
		CloseHandle(ev[0]);
		CloseHandle(ev[1]);
	}
	
	void thread(unsigned index)
	{
		if (0 == index)
		{
			state.store(1, rl::memory_order_relaxed);
			WaitForMultipleObjects(2, ev, 0, 1);
		}
		else if (1 == index)
		{
			SetEvent(ev[0]);
			SetEvent(ev[1]);
			unsigned rv = WaitForSingleObject(ev[0], 0);
			if (rv == WAIT_TIMEOUT) {
				assert(state.load(rl::memory_order_relaxed) == 1);
				RL_UNTIL(true);
			}
		}
	}
};



