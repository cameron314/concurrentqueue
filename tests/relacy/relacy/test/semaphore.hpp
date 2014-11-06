#pragma once

#include "../relacy/relacy_std.hpp"



struct test_semaphore : rl::test_suite<test_semaphore, 2>
{
    HANDLE sema;
    VAR_T(int) data;

    void before()
    {
        VAR(data) = 0;
        sema = CreateSemaphore(0, 0, 2, 0);
    }

    void after()
    {
        CloseHandle(sema);
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            VAR(data) = 1;
            ReleaseSemaphore(sema, 1, 0);
        }
        else
        {
            unsigned rv = WaitForSingleObject(sema, INFINITE);
	  				assert(rv == WAIT_OBJECT_0);
            assert(VAR(data) == 1);
			  		rv = WaitForSingleObject(sema, 0);
					  assert(rv == WAIT_TIMEOUT);
        }
    }
};




struct test_semaphore_atomic : rl::test_suite<test_semaphore_atomic, 2>
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
			unsigned rv = WaitForSingleObject(sem[0], INFINITE);
			assert(rv == WAIT_OBJECT_0);
			ReleaseSemaphore(sem[1], 1, 0);
			rv = WaitForSingleObject(sem[1], 0);
			assert(rv == WAIT_TIMEOUT);
		}
		else
		{
			unsigned rv = SignalObjectAndWait(sem[0], sem[1], INFINITE, 0);
			assert(rv == WAIT_OBJECT_0);
			rv = WaitForSingleObject(sem[1], 0);
			assert(rv == WAIT_TIMEOUT);
			rv = WaitForSingleObject(sem[0], 0);
			assert(rv == WAIT_TIMEOUT);
		}
	}
};




