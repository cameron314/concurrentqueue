#pragma once

#include "../relacy/windows.h"


struct test_win_thread : rl::test_suite<test_win_thread, 1>
{
    static size_t const dynamic_thread_count = 2;

    VAR_T(int) data;

    static unsigned long RL_STDCALL win_func(void* param)
    {
        static_cast<test_win_thread*>(param)->VAR(data) += 1;
        return 0;
    }

    static unsigned RL_STDCALL msvc_func(void* param)
    {
        static_cast<test_win_thread*>(param)->VAR(data) += 1;
        return 0;
    }

    void thread(unsigned)
    {
        VAR(data) = 0;

        HANDLE th1 = CreateThread(0, 0, &test_win_thread::win_func, this, 0, 0);
        WaitForSingleObject(th1, INFINITE);
        RL_ASSERT(VAR(data) == 1);

        HANDLE th2 = (HANDLE)_beginthreadex(0, 0, &test_win_thread::msvc_func, this, 0, 0);
        WaitForSingleObject(th2, INFINITE);
        RL_ASSERT(VAR(data) == 2);
    }
};




struct test_win_mutex : rl::test_suite<test_win_mutex, 2>
{
    HANDLE mtx;
    VAR_T(int) data;

    void before()
    {
        mtx = CreateMutex(0, 0, 0);
        VAR(data) = 0;
    }

    void after()
    {
        CloseHandle(mtx);
    }

    void thread(unsigned)
    {
        WaitForSingleObject(mtx, INFINITE);
        WaitForSingleObject(mtx, INFINITE);
        VAR(data) += 1;
        ReleaseMutex(mtx);
        ReleaseMutex(mtx);

        if (WAIT_OBJECT_0 == WaitForSingleObject(mtx, 0))
        {
            VAR(data) += 1;
            ReleaseMutex(mtx);
        }
    }
};




struct test_win_cs : rl::test_suite<test_win_cs, 2>
{
    CRITICAL_SECTION mtx;
    VAR_T(int) data;

    void before()
    {
        InitializeCriticalSection(&mtx);
        VAR(data) = 0;
    }

    void after()
    {
        DeleteCriticalSection(&mtx);
    }

    void thread(unsigned)
    {
        EnterCriticalSection(&mtx);
        VAR(data) += 1;
        LeaveCriticalSection(&mtx);

        if (TryEnterCriticalSection(&mtx))
        {
            VAR(data) += 1;
            LeaveCriticalSection(&mtx);
        }
    }
};


struct test_win_condvar : rl::test_suite<test_win_condvar, 3>
{
    CONDITION_VARIABLE cv;
    CRITICAL_SECTION mtx;
    VAR_T(int) stage;

    void before()
    {
        InitializeConditionVariable(&cv);
        InitializeCriticalSection(&mtx);
        VAR(stage) = 0;
    }

    void after()
    {
        DeleteCriticalSection(&mtx);
        DeleteConditionVariable(&cv);
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            EnterCriticalSection(&mtx);
            VAR(stage) += 1;
            WakeAllConditionVariable(&cv);
            while (VAR(stage) != 2)
                SleepConditionVariableCS(&cv, &mtx, INFINITE);
            LeaveCriticalSection(&mtx);
        }
        else if (1 == index)
        {
            EnterCriticalSection(&mtx);
            while (VAR(stage) != 1)
                SleepConditionVariableCS(&cv, &mtx, 1);
            VAR(stage) += 1;
            WakeAllConditionVariable(&cv);
            LeaveCriticalSection(&mtx);
        }
        else if (2 == index)
        {
            EnterCriticalSection(&mtx);
            while (VAR(stage) != 2)
                SleepConditionVariableCS(&cv, &mtx, INFINITE);
            LeaveCriticalSection(&mtx);
            WakeConditionVariable(&cv);
        }
    }
};



struct test_win_condvar_srw : rl::test_suite<test_win_condvar_srw, 2>
{
    CONDITION_VARIABLE cv;
    SRWLOCK mtx;
    VAR_T(int) stage;

    void before()
    {
        InitializeConditionVariable(&cv);
        InitializeSRWLock(&mtx);
        VAR(stage) = 0;
    }

    void after()
    {
        DeleteSRWLock(&mtx);
        DeleteConditionVariable(&cv);
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            AcquireSRWLockExclusive(&mtx);
            VAR(stage) += 1;
            WakeAllConditionVariable(&cv);
            while (VAR(stage) != 2)
                SleepConditionVariableSRW(&cv, &mtx, INFINITE, 0);
            ReleaseSRWLockExclusive(&mtx);
        }
        else if (1 == index)
        {
            AcquireSRWLockExclusive(&mtx);
            while (VAR(stage) != 1)
                SleepConditionVariableSRW(&cv, &mtx, 1, 0);
            VAR(stage) += 1;
            WakeAllConditionVariable(&cv);
            ReleaseSRWLockExclusive(&mtx);
        }
        else if (2 == index)
        {
            AcquireSRWLockExclusive(&mtx);
            while (VAR(stage) != 2)
                SleepConditionVariableSRW(&cv, &mtx, INFINITE, 0);
            ReleaseSRWLockExclusive(&mtx);
            WakeConditionVariable(&cv);
        }
    }
};



struct test_win_sem : rl::test_suite<test_win_sem, 2>
{
    HANDLE sem1, sem2;
    VAR_T(int) data;

    void before()
    {
        sem1 = CreateSemaphore(0, 0, 1, 0);
        sem2 = CreateSemaphore(0, 0, 1, 0);
        VAR(data) = 0;
    }

    void after()
    {
        CloseHandle(sem1);
        CloseHandle(sem2);
    }

    void thread(unsigned index)
    {
        if (index)
        {
            VAR(data) = 1;
            long count = -1;
            ReleaseSemaphore(sem1, 1, &count);
            assert(count == 0);
            for (;;)
            {
                unsigned long rv = WaitForSingleObject(sem2, 0);
                if (rv == WAIT_OBJECT_0)
                    break;
                RL_ASSERT(rv == WAIT_TIMEOUT);
                Sleep(0);
            }
            RL_ASSERT(VAR(data) == 2);
            VAR(data) = 3;
            ReleaseSemaphore(sem2, 1, &count);
            RL_ASSERT(count == 0);
            ReleaseSemaphore(sem2, 1, &count);
            RL_ASSERT(count == 1);
        }
        else
        {
            unsigned long rv = WaitForSingleObject(sem1, INFINITE);
            assert(rv == WAIT_OBJECT_0);
            RL_ASSERT(VAR(data) == 1);
            VAR(data) = 2;
            ReleaseSemaphore(sem2, 1, 0);
        }
    }
};




struct test_win_event : rl::test_suite<test_win_event, 2>
{
	HANDLE ev;
	VAR_T(int) data;
	
	void before()
	{
		VAR(data) = 0;
		ev = CreateEvent(0, 0, 0, 0);
	}
	
	void after()
	{
		CloseHandle(ev);
	}
	
	void thread(unsigned index)
	{
		if (0 == index)
		{
			VAR(data) = 1;
			SetEvent(ev);
			PulseEvent(ev);
		}
		else
		{
			unsigned rv = WaitForSingleObject(ev, INFINITE);
			assert(rv == WAIT_OBJECT_0);
			assert(VAR(data) == 1);
			rv = WaitForSingleObject(ev, 0);
			assert(rv == WAIT_TIMEOUT);
			ResetEvent(ev);
		}
	}
};




struct test_FlushProcessWriteBuffers : rl::test_suite<test_FlushProcessWriteBuffers, 2>
{
    std::atomic<int> x1;
    std::atomic<int> x2;
    int r1;
    int r2;

    void before()
    {
        x1.store(0, std::memory_order_relaxed);
        x2.store(0, std::memory_order_relaxed);
        r1 = r2 = 0;
    }

    void after()
    {
        assert(r1 == 1 || r2 == 1);
    }

    void thread(unsigned index)
    {
        if (index)
        {
            x1.store(1, std::memory_order_relaxed);
            r1 = x2.load(std::memory_order_relaxed);
        }
        else
        {
            x2.store(1, std::memory_order_relaxed);
            FlushProcessWriteBuffers();
            r2 = x1.load(std::memory_order_relaxed);
        }
    }
};

