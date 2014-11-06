#pragma once

#include "../relacy/relacy_std.hpp"



struct test_event_auto : rl::test_suite<test_event_auto, 2>
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
        }
        else
        {
            unsigned rv = WaitForSingleObject(ev, INFINITE);
            assert(rv == WAIT_OBJECT_0);
            assert(VAR(data) == 1);
						rv = WaitForSingleObject(ev, 0);
						assert(rv == WAIT_TIMEOUT);
        }
    }
};




struct test_event_atomic : rl::test_suite<test_event_atomic, 2>
{
	HANDLE ev1;
	HANDLE ev2;
	
	void before()
	{
		ev1 = CreateEvent(0, 0, 0, 0);
		ev2 = CreateEvent(0, 0, 0, 0);
	}
	
	void after()
	{
		CloseHandle(ev1);
		CloseHandle(ev2);
	}
	
	void thread(unsigned index)
	{
		if (0 == index)
		{
			unsigned rv = WaitForSingleObject(ev1, INFINITE);
			assert(rv == WAIT_OBJECT_0);
			SetEvent(ev2);
			rv = WaitForSingleObject(ev2, 0);
			assert(rv == WAIT_TIMEOUT);
		}
		else
		{
			unsigned rv = SignalObjectAndWait(ev1, ev2, INFINITE, 0);
			assert(rv == WAIT_OBJECT_0);
			rv = WaitForSingleObject(ev2, 0);
			assert(rv == WAIT_TIMEOUT);
		}
	}
};




struct test_event_manual : rl::test_suite<test_event_manual, 2>
{
	HANDLE ev;
	VAR_T(int) data;
	
	void before()
	{
		VAR(data) = 0;
		ev = CreateEvent(0, 1, 0, 0);
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
		}
		else
		{
			unsigned rv = WaitForSingleObject(ev, INFINITE);
			assert(rv == WAIT_OBJECT_0);
			assert(VAR(data) == 1);
			rv = WaitForSingleObject(ev, 0);
			assert(rv == WAIT_OBJECT_0);
		}
	}
};


