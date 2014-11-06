#pragma once

#include "../relacy/pthread.h"



struct test_pthread_thread : rl::test_suite<test_pthread_thread, 1>
{
    static size_t const dynamic_thread_count = 2;

    VAR_T(int) data;

    static void* func(void* param)
    {
        static_cast<test_pthread_thread*>(param)->VAR(data) += 1;
        return 0;
    }

    void thread(unsigned)
    {
        VAR(data) = 0;

        pthread_t th1;
        pthread_create(&th1, 0, &test_pthread_thread::func, this);
        void* res1 = 0;
        pthread_join(th1, &res1);

        RL_ASSERT(VAR(data) == 1);

        pthread_t th2;
        pthread_create(&th2, 0, &test_pthread_thread::func, this);
        void* res2 = 0;
        pthread_join(th2, &res2);

        RL_ASSERT(VAR(data) == 2);
    }
};




struct test_pthread_mutex : rl::test_suite<test_pthread_mutex, 2>
{
    pthread_mutex_t mtx;
    VAR_T(int) data;

    void before()
    {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&mtx, &attr);
        pthread_mutexattr_destroy(&attr);
        VAR(data) = 0;
    }

    void after()
    {
        pthread_mutex_destroy(&mtx);
    }

    void thread(unsigned /*index*/)
    {
        pthread_mutex_lock(&mtx);
        VAR(data) += 1;
        pthread_mutex_unlock(&mtx);

        if (0 == pthread_mutex_try_lock(&mtx))
        {
            VAR(data) += 1;
            pthread_mutex_unlock(&mtx);
        }

        //pthread_mutex_timedlock
    }
};




struct test_pthread_condvar : rl::test_suite<test_pthread_condvar, 3>
{
    pthread_cond_t cv;
    pthread_mutex_t mtx;
    VAR_T(int) stage;

    void before()
    {
        pthread_condattr_t attr;
        pthread_cond_init(&cv, &attr);
        pthread_mutex_init(&mtx, 0);
        VAR(stage) = 0;
    }

    void after()
    {
        pthread_cond_destroy(&cv);
        pthread_mutex_destroy(&mtx);
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            pthread_mutex_lock(&mtx);
            VAR(stage) += 1;
            pthread_cond_broadcast(&cv);
            while (VAR(stage) != 2)
                pthread_cond_wait(&cv, &mtx);
            pthread_mutex_unlock(&mtx);
        }
        else if (1 == index)
        {
            pthread_mutex_lock(&mtx);
            while (VAR(stage) != 1)
            {
                int ts = 1;
                pthread_cond_timedwait(&cv, &mtx, &ts);
            }
            VAR(stage) += 1;
            pthread_cond_broadcast(&cv);
            pthread_mutex_unlock(&mtx);
        }
        else if (2 == index)
        {
            pthread_mutex_lock(&mtx);
            while (VAR(stage) != 2)
                pthread_cond_wait(&cv, &mtx);
            pthread_mutex_unlock(&mtx);
            pthread_cond_signal(&cv);
        }
    }
};




struct test_pthread_condvar2 : rl::test_suite<test_pthread_condvar2, 2>
{
    pthread_cond_t cv1, cv2;
    pthread_mutex_t mtx1, mtx2;
    VAR_T(int) stage;

    void before()
    {
        pthread_cond_init(&cv1, 0);
        pthread_cond_init(&cv2, 0);
        pthread_mutex_init(&mtx1, 0);
        pthread_mutex_init(&mtx2, 0);
        VAR(stage) = 0;
    }

    void after()
    {
        pthread_cond_destroy(&cv1);
        pthread_cond_destroy(&cv2);
        pthread_mutex_destroy(&mtx1);
        pthread_mutex_destroy(&mtx2);
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            pthread_mutex_lock(&mtx1);
            int ts = 1;
            pthread_cond_timedwait(&cv1, &mtx1, &ts);
            pthread_mutex_unlock(&mtx1);
        }
        else if (1 == index)
        {
            pthread_mutex_lock(&mtx2);
            int ts = 1;
            pthread_cond_timedwait(&cv2, &mtx2, &ts);
            pthread_mutex_unlock(&mtx2);
        }
    }
};




struct test_pthread_rwlock : rl::test_suite<test_pthread_rwlock, 3>
{
    pthread_rwlock_t mtx;
    VAR_T(int) data;

    void before()
    {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
        pthread_rwlock_init(&mtx, &attr);
        pthread_mutexattr_destroy(&attr);
        VAR(data) = 0;
    }

    void after()
    {
        pthread_rwlock_destroy(&mtx);
    }

    void thread(unsigned /*index*/)
    {
        pthread_rwlock_wrlock(&mtx);
        VAR(data) += 1;
        pthread_rwlock_unlock(&mtx);

        if (0 == pthread_rwlock_trywrlock(&mtx))
        {
            VAR(data) += 1;
            pthread_rwlock_unlock(&mtx);
        }

        pthread_rwlock_rdlock(&mtx);
        (void)(int)VAR(data);
        pthread_rwlock_unlock(&mtx);

        if (0 == pthread_rwlock_tryrdlock(&mtx))
        {
            (void)(int)VAR(data);
            pthread_rwlock_unlock(&mtx);
        }
    }
};




struct test_pthread_sem : rl::test_suite<test_pthread_sem, 2>
{
    sem_t sem1, sem2;
    VAR_T(int) data;

    void before()
    {
        sem_init(&sem1, 0, 0);
        sem_init(&sem2, 0, 0);
        VAR(data) = 0;
    }

    void after()
    {
        sem_destroy(&sem1);
        sem_destroy(&sem2);
    }

    void thread(unsigned index)
    {
        if (index)
        {
            VAR(data) = 1;
            sem_post(&sem1);
            while (sem_trywait(&sem2))
            {
                assert(errno == EINTR || errno == EAGAIN);
                pthread_yield();
            }
            RL_ASSERT(VAR(data) == 2);
            VAR(data) = 3;
            int count = -1;
            sem_getvalue(&sem2, &count);
            RL_ASSERT(count == 0);
            sem_post(&sem2);
            sem_getvalue(&sem2, &count);
            RL_ASSERT(count == 1);
        }
        else
        {
            while (sem_wait(&sem1))
                assert(errno == EINTR);
            RL_ASSERT(VAR(data) == 1);
            VAR(data) = 2;
            sem_post(&sem2);
        }
    }
};




