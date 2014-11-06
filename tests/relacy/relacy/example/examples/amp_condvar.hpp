#pragma once


struct amp_raw_condition_variable_s
{
    CRITICAL_SECTION access_waiting_threads_count_critsec;
    HANDLE wake_waiting_threads_mutex;
    HANDLE waking_waiting_threads_count_control_sem;
    HANDLE finished_waking_waiting_threads_event;
    VAR_T(LONG) waiting_thread_count;
    VAR_T(BOOL) broadcast_in_progress;
};


struct amp_raw_mutex_s
{
    CRITICAL_SECTION critical_section;
    BOOL is_locked;
};

typedef amp_raw_condition_variable_s* amp_raw_condition_variable_t;
typedef amp_raw_mutex_s* amp_raw_mutex_t;

int const AMP_SUCCESS = 0;


int amp_raw_mutex_init(amp_raw_mutex_t mutex)
{
    InitializeCriticalSectionAndSpinCount(&mutex->critical_section, 1);
    
    mutex->is_locked = FALSE;
    
    return AMP_SUCCESS;
}
 
 
 
int amp_raw_mutex_finalize(amp_raw_mutex_t mutex)
{
    assert(NULL != mutex);
    
    int retval = AMP_SUCCESS;
    
    DeleteCriticalSection(&mutex->critical_section);
    
    return retval;
}
 
 
 
int amp_raw_mutex_lock(amp_raw_mutex_t mutex)
{
    assert(NULL != mutex);
    
    EnterCriticalSection(&mutex->critical_section);
    
    mutex->is_locked = TRUE;
    
    return AMP_SUCCESS;
}
 
int amp_raw_mutex_unlock(amp_raw_mutex_t mutex)
{
    assert(NULL != mutex);
 
    mutex->is_locked = FALSE;
    LeaveCriticalSection(&mutex->critical_section);
    
    return AMP_SUCCESS;
}




int amp_raw_condition_variable_init(amp_raw_condition_variable_t cond)
{
    InitializeCriticalSectionAndSpinCount(&cond->access_waiting_threads_count_critsec, 1);
    
    cond->wake_waiting_threads_mutex = CreateMutex(0, 0, 0);

    cond->waking_waiting_threads_count_control_sem = CreateSemaphore(NULL, /* No inheritance to child processes */
                                                                     0, /* Initially no threads can pass */
                                                                     LONG_MAX, /* Max semaphore count */
                                                                     NULL); /* Only intra-process semaphore */

    cond->finished_waking_waiting_threads_event = CreateEvent(NULL, /* Default security and no inheritance to child processes */
                                                              FALSE, /* No manual reset */
                                                              0, /* Initially not signaled */
                                                              NULL /* Not inter-process available */
                                                              );
    
    
    cond->VAR(waiting_thread_count) = 0l;
    cond->VAR(broadcast_in_progress) = FALSE;
    
    return AMP_SUCCESS;
}
 
 
 
int amp_raw_condition_variable_finalize(amp_raw_condition_variable_t cond)
{
    DeleteCriticalSection(&cond->access_waiting_threads_count_critsec);
    
    CloseHandle(cond->wake_waiting_threads_mutex);
    CloseHandle(cond->waking_waiting_threads_count_control_sem);
    CloseHandle(cond->finished_waking_waiting_threads_event);
    
    int ret_error_code = AMP_SUCCESS;
    return ret_error_code;
}
 


int amp_raw_condition_variable_signal(amp_raw_condition_variable_t cond)
{
    WaitForSingleObject(cond->wake_waiting_threads_mutex,
                                                  INFINITE);
    BOOL at_least_one_waiting_thread = (0l != cond->VAR(waiting_thread_count));
    
    if (at_least_one_waiting_thread) {
        LONG prev_sem_count = 0;
        ReleaseSemaphore(cond->waking_waiting_threads_count_control_sem,
                                                     1,
                                                     &prev_sem_count /* No interest in the previous sem count. */
                                                     );

        WaitForSingleObject(cond->finished_waking_waiting_threads_event,
                                                     INFINITE);
    }
    
    ReleaseMutex(cond->wake_waiting_threads_mutex);
    
    return AMP_SUCCESS;
}


int amp_raw_condition_variable_broadcast(amp_raw_condition_variable_t cond)
{
    WaitForSingleObject(cond->wake_waiting_threads_mutex,
                                                  INFINITE);
    
    LONG const waiting_thread_count = cond->VAR(waiting_thread_count);
    
    if (0 < waiting_thread_count) {
        
        cond->VAR(broadcast_in_progress) = TRUE;
        /* Releasing the sem here and waiting on it should update the memory of
* the waiting threads to see that a broadcast is in progress.
*/
        LONG prev_sem_count = 0;
        /* Assuming that less threads exist than max possible semaphore count.
* TODO: @todo Decide if to spin here if the assumption doesn't hold
* true in the future?
*/
        ReleaseSemaphore(cond->waking_waiting_threads_count_control_sem,
                                                     waiting_thread_count,
                                                     &prev_sem_count /* No interest in the previous sem count. */
                                                     );

        WaitForSingleObject(cond->finished_waking_waiting_threads_event,
                                                     INFINITE);
        cond->VAR(broadcast_in_progress) = FALSE;
        
    }
    
    ReleaseMutex(cond->wake_waiting_threads_mutex);
    
    return AMP_SUCCESS;
}




int amp_raw_condition_variable_wait(amp_raw_condition_variable_t cond,
                                    struct amp_raw_mutex_s *mutex)
{
    WaitForSingleObject(cond->wake_waiting_threads_mutex,
                                                  INFINITE);
    {
        ++(cond->VAR(waiting_thread_count));
    }
    
    amp_raw_mutex_unlock(mutex);

    SignalObjectAndWait(cond->wake_waiting_threads_mutex, cond->waking_waiting_threads_count_control_sem, INFINITE, FALSE);
 
    BOOL broadcast_in_progress = FALSE;
    LONG count = 0;
    EnterCriticalSection(&cond->access_waiting_threads_count_critsec);
    {
        count = --(cond->VAR(waiting_thread_count));
        
        broadcast_in_progress = cond->VAR(broadcast_in_progress);
    }
    LeaveCriticalSection(&cond->access_waiting_threads_count_critsec);
    
    BOOL all_waiting_threads_awake = TRUE;
    if (TRUE == broadcast_in_progress && count > 0) {
        all_waiting_threads_awake = FALSE;
    }
    
    if (TRUE == all_waiting_threads_awake) {
        SetEvent(cond->finished_waking_waiting_threads_event);
    }
    
    
    amp_raw_mutex_lock(mutex);
    
    return AMP_SUCCESS;
}


struct amp_condvar_test : rl::test_suite<amp_condvar_test, 2>
{
    VAR_T(int) data;
    amp_raw_mutex_s mtx;
    amp_raw_condition_variable_s cv;

    void before()
    {
        VAR(data) = 0;
        amp_raw_mutex_init(&mtx);
        amp_raw_condition_variable_init(&cv);
    }

    void after()
    {
        amp_raw_mutex_finalize(&mtx);
        amp_raw_condition_variable_finalize(&cv);
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            amp_raw_mutex_lock(&mtx);
            data($) += 1;
            amp_raw_condition_variable_signal(&cv);
            amp_raw_mutex_unlock(&mtx);
        }
        else
        {
            amp_raw_mutex_lock(&mtx);
            while (0 == data($))
            {
                amp_raw_condition_variable_wait(&cv, &mtx);
            }
            amp_raw_mutex_unlock(&mtx);
        }
    }
};




struct amp_condvar_test2 : rl::test_suite<amp_condvar_test2, 4>
{
    VAR_T(int) stage;
    amp_raw_mutex_s mtx;
    amp_raw_condition_variable_s cv;

    void before()
    {
        VAR(stage) = 0;
        amp_raw_mutex_init(&mtx);
        amp_raw_condition_variable_init(&cv);
    }

    void after()
    {
        amp_raw_mutex_finalize(&mtx);
        amp_raw_condition_variable_finalize(&cv);
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            amp_raw_mutex_lock(&mtx);
            stage($) += 1;
            amp_raw_condition_variable_broadcast(&cv);
            while (stage($) < 2)
                amp_raw_condition_variable_wait(&cv, &mtx);
            amp_raw_mutex_unlock(&mtx);
        }
        else if (1 == index)
        {
            amp_raw_mutex_lock(&mtx);
            while (stage($) != 1)
                amp_raw_condition_variable_wait(&cv, &mtx);
            stage($) += 1;
            amp_raw_condition_variable_broadcast(&cv);
            amp_raw_mutex_unlock(&mtx);
        }
        else if (2 == index)
        {
            amp_raw_mutex_lock(&mtx);
            while (stage($) != 2)
                amp_raw_condition_variable_wait(&cv, &mtx);
            stage($) += 1;
            //amp_raw_condition_variable_broadcast(&cv);
            amp_raw_mutex_unlock(&mtx);
            amp_raw_condition_variable_signal(&cv);
        }
        else if (3 == index)
        {
            amp_raw_mutex_lock(&mtx);
            while (stage($) != 3)
                amp_raw_condition_variable_wait(&cv, &mtx);
            amp_raw_mutex_unlock(&mtx);
        }
    }
};


