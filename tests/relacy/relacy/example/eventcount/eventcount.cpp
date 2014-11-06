#include "stdafx.h"

#include "../../relacy/windows.h"

#define HANDLE rl::HANDLE

#define CreateSemaphoreA rl::RL_CreateSemaphore($)
#define CreateSemaphoreW rl::RL_CreateSemaphore($)
#ifndef CreateSemaphore
#   define CreateSemaphore CreateSemaphoreW
#endif


#define CloseHandle rl::RL_CloseHandle($)


#include <stddef.h>


#if defined(WIN32) && defined(_MSC_VER)

#include <windows.h>
#include <intrin.h>

class semaphore
{
public:
    semaphore()
    {
        h_ = CreateSemaphore(0, 0, LONG_MAX, 0);
    }

    ~semaphore()
    {
        CloseHandle(h_);
    }

    void wait()
    {
        WaitForSingleObject(h_, INFINITE);
    }

    void post()
    {
        ReleaseSemaphore(h_, 1, 0);
    }

private:
    HANDLE h_;

    semaphore(semaphore const&);
    semaphore& operator = (semaphore const&);
};

class mutex
{
public:
    mutex()
    {
        InitializeCriticalSection(&cs_);
    }

    ~mutex()
    {
        DeleteCriticalSection(&cs_);
    }

    void lock()
    {
        EnterCriticalSection(&cs_);
    }

    void unlock()
    {
        LeaveCriticalSection(&cs_);
    }

private:
    CRITICAL_SECTION    cs_;

    mutex(mutex const&);
    mutex& operator = (mutex const&);
};

void full_memory_fence()
{
    _mm_mfence();
}

#define THREAD_LOCAL __declspec(thread)

#elif defined(POSIX) && defined(GCC)

#include <pthread.h>
#include <semaphore.h>

class semaphore
{
public:
    semaphore()
    {
        sem_init(&sem_, 0, 0);
    }

    ~semaphore()
    {
        sem_destroy(&sem_);
    }

    void wait()
    {
        sem_wait(&sem_);
    }

    void post()
    {
        sem_post(&sem_);
    }

private:
    sem_t               sem_;

    semaphore(semaphore const&);
    semaphore& operator = (semaphore const&);
};

class mutex
{
public:
    mutex()
    {
        pthread_mutex_init(&mutex_, 0);
    }

    ~mutex()
    {
        pthread_mutex_destroy(&mutex_);
    }

    void lock()
    {
        pthread_mutex_lock(&mutex_);
    }

    void unlock()
    {
        pthread_mutex_unlock(&mutex_);
    }

private:
    pthread_mutex_t     mutex_;

    mutex(mutex const&);
    mutex& operator = (mutex const&);
};

void full_memory_fence()
{
    __sync_synchronize();
}

#define THREAD_LOCAL __thread

#endif



class lock
{
public:
    lock(mutex& m)
        : m_(m)
    {
        m.lock();
    }

    ~lock()
    {
        m_.unlock();
    }

private:
    mutex&              m_;

    lock(lock const&);
    lock& operator = (lock const&);
};




/** simple single-threaded double-linked list
 *  nothing interesting
 */
class dlist
{
public:
    struct node
    {
        node*           prev_;
        node*           next_;

        node()
        {
            prev_ = 0;
            next_ = 0;
        }
    };

    dlist()
    {
        reset();
    }

    void push(node* n)
    {
        size_ += 1;
        n->next_ = head_.next_;
        n->prev_ = &head_;
        head_.next_->prev_ = n;
        head_.next_ = n;
    }

    node* pop()
    {
        if (size_ == 0)
            return 0;
        node* n = head_.next_;
        remove(n);
        return n;
    }

    void remove(node* n)
    {
        size_ -= 1;
        n->prev_->next_ = n->next_;
        n->next_->prev_ = n->prev_;
    }

    size_t size() const
    {
        return size_;
    }

    node* begin()
    {
        return head_.next_;
    }

    void flush_to(dlist& target)
    {
        if (size_)
        {
            target.size_ = size_;
            target.head_.next_ = head_.next_;
            target.head_.next_->prev_ = &target.head_;
            target.tail_.prev_ = tail_.prev_;
            target.tail_.prev_->next_ = &target.tail_;
        }
        else
        {
            target.reset();
        }
        reset();
    }

    static bool not_last(node* n)
    {
        return n->next_ != 0;
    }

    static node* get_next(node* n)
    {
        return n->next_;
    }

private:
    size_t volatile     size_;
    node                head_;
    node                tail_;

    void reset()
    {
        size_ = 0;
        head_.next_ = &tail_;
        head_.prev_ = 0;
        tail_.next_ = 0;
        tail_.prev_ = &head_;
    }

    dlist(dlist const&);
    dlist& operator = (dlist const&);
};



/** pre-thread descriptor for eventcount
 */
struct ec_thread
{
    dlist::node         node_;
    semaphore           sema_;
    unsigned            epoch_;
    bool volatile       in_waitset_;
    bool                spurious_;
    void*               ctx_;

    ec_thread()
    {
        epoch_ = 0;
        in_waitset_ = false;
        spurious_ = false;
        ctx_ = 0;
    }

    ~ec_thread()
    {
        if (spurious_)
            sema_.wait();
    }

    static ec_thread* current()
    {
        static THREAD_LOCAL ec_thread* ec_thread_instance = 0;
        ec_thread* instance = ec_thread_instance;
        if (instance == 0)
        {
            instance = new ec_thread;
            ec_thread_instance = instance;
        }
        return instance;
        // instance must be destroyed in DllMain() callback
        // or in pthread_key_create() callback
    }

private:
    ec_thread(ec_thread const&);
    ec_thread& operator = (ec_thread const&);
};



/** fine-grained eventcount implementation
 */
class eventcount
{
public:
    eventcount()
    {
        epoch_ = 0;
    }

    void prepare_wait(void* ctx = 0)
    {
        ec_thread* th = ec_thread::current();
        // this is good place to pump previous spurious wakeup
        if (th->spurious_)
        {
            th->spurious_ = false;
            th->sema_.wait();
        }
        th->in_waitset_ = true;
        th->ctx_ = ctx;
        {
            lock l (mtx_);
            th->epoch_ = epoch_;
            waitset_.push(&th->node_);
        }
        full_memory_fence();
    }

    void wait()
    {
        ec_thread* th = ec_thread::current();
        // this check is just an optimization
        if (th->epoch_ == epoch_)
            th->sema_.wait();
        else
            retire_wait();
    }

    void retire_wait()
    {
        ec_thread* th = ec_thread::current();
        // spurious wakeup will be pumped in following prepare_wait()
        th->spurious_  = true;
        // try to remove node from waitset
        if (th->in_waitset_)
        {
            lock l (mtx_);
            if (th->in_waitset_)
            {
                // successfully removed from waitset,
                // so there will be no spurious wakeup
                th->in_waitset_ = false;
                th->spurious_ = false;
                waitset_.remove(&th->node_);
            }
        }
    }

    void notify_one()
    {
        full_memory_fence();
        notify_one_relaxed();
    }

    template<typename predicate_t>
    void notify(predicate_t pred)
    {
        full_memory_fence();
        notify_relaxed(pred);
    }

    void notify_all()
    {
        full_memory_fence();
        notify_all_relaxed();
    }

    void notify_one_relaxed()
    {
        if (waitset_.size() == 0)
            return;
        dlist::node* n;
        {
            lock l (mtx_);
            epoch_ += 1;
            n = waitset_.pop();
            if (n)
                to_ec_thread(n)->in_waitset_ = false;
        }
        if (n)
        {
            to_ec_thread(n)->sema_.post();
        }
    }

    template<typename predicate_t>
    void notify_relaxed(predicate_t pred)
    {
        if (waitset_.size() == 0)
            return;
        dlist temp;
        {
            lock l (mtx_);
            epoch_ += 1;
            size_t size = waitset_.size();
            size_t idx = 0;
            dlist::node* n = waitset_.begin();
            while (dlist::not_last(n))
            {
                dlist::node* next = dlist::get_next(n);
                ec_thread* th = to_ec_thread(n);
                if (pred(th->ctx_, size, idx))
                {
                    waitset_.remove(n);
                    temp.push(n);
                    th->in_waitset_ = false;
                }
                n = next;
                idx += 1;
            }
        }
        dlist::node* n = temp.begin();
        while (dlist::not_last(n))
        {
            dlist::node* next = dlist::get_next(n);
            to_ec_thread(n)->sema_.post();
            n = next;
        }
    }

    void notify_all_relaxed()
    {
        if (waitset_.size() == 0)
            return;
        dlist temp;
        {
            lock l (mtx_);
            epoch_ += 1;
            waitset_.flush_to(temp);
            dlist::node* n = temp.begin();
            while (dlist::not_last(n))
            {
                to_ec_thread(n)->in_waitset_ = false;
                n = dlist::get_next(n);
            }
        }
        dlist::node* n = temp.begin();
        while (dlist::not_last(n))
        {
            dlist::node* next = dlist::get_next(n);
            to_ec_thread(n)->sema_.post();
            n = next;
        }
    }

private:
    mutex               mtx_;
    dlist               waitset_;
    volatile unsigned   epoch_;

    ec_thread* to_ec_thread(dlist::node* n)
    {
        return (ec_thread*)((char*)n - offsetof(ec_thread, node_));
    }

    eventcount(eventcount const&);
    eventcount& operator = (eventcount const&);
};





struct scheduler
{
    struct tbb_thread {};

    eventcount          ec_;
    tbb_thread*         threads_;
    bool volatile       is_permanently_open_;

    void wait_while_pool_is_empty(tbb_thread* th)
    {
        if (is_permanently_open_)
            return;
        ec_.prepare_wait(th);
        if (pool_is_empty())
            ec_.wait();
        else
            ec_.retire_wait();
    }

    void notify_about_new_task_available()
    {
        ec_.notify_one_relaxed();
    }

    void notify_about_new_task_available_with_preference(tbb_thread* preference)
    {
        struct local
        {
            tbb_thread*     preference_;
            bool            fired_;

            bool operator () (void* ctx, size_t count, size_t idx)
            {
                tbb_thread* th = (tbb_thread*)ctx;
                if (th == preference_)
                {
                    fired_ = true;
                    return true;
                }
                else if (idx == count - 1 && fired_ == false)
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
        }
        pred = {preference};
        ec_.notify_relaxed(pred);
    }

    void notify_about_list_of_tasks_available(size_t total_count, size_t preference_count, tbb_thread** preferences)
    {
        struct local
        {
            size_t          remain_to_signal_;
            size_t          preference_count_;
            tbb_thread**    preferences_;

            bool operator () (void* ctx, size_t count, size_t idx)
            {
                tbb_thread* th = (tbb_thread*)ctx;
                size_t remain_in_waitset = count - idx;
                if (remain_in_waitset <= remain_to_signal_)
                {
                    return true;
                }
                else
                {
                    for (size_t i = 0; i != preference_count_; ++i)
                    {
                        if (preferences_[i] == th)
                        {
                            remain_to_signal_ -= 1;
                            return true;
                        }
                    }
                }
                return false;
            }
        }
        pred = {total_count, preference_count, preferences};
        ec_.notify_relaxed(pred);
    }

    bool pool_is_empty()
    {
        return true;
    }
};



struct queue
{
    int                 producer_idx_;
    int                 consumer_idx_;

    void**              buffer_;

    eventcount          ec_;

    void enqueue(void* data)
    {
        int idx = ++producer_idx_; // atomic
        buffer_[idx] = data;

        struct local
        {
            int         idx_;
            bool operator () (void* ctx, size_t /*count*/, size_t /*idx*/)
            {
                return idx_ == *(int*)ctx;
            }
        }
        pred = {idx};
        ec_.notify(pred); // not relaxed!!!
    }

    void* dequeue()
    {
        int idx = ++consumer_idx_; // atomic
        void* data = buffer_[idx];
        if (data)
            return data;
        for (;;)
        {
            ec_.prepare_wait(&idx);
            data = buffer_[idx];
            if (data)
            {
                ec_.retire_wait();
                return data;
            }
            ec_.wait();
            data = buffer_[idx];
            if (data)
            {
                return data;
            }
        }
    }
};



class condition_variable
{
    eventcount ec_;

public:
    void wait(mutex& mtx)
    {
        ec_.prepare_wait();
        mtx.unlock();
        ec_.wait();
        mtx.lock();
    }

    void signal()
    {
        ec_.notify_one();
    }

    void broadcast()
    {
        ec_.notify_all();
    }
}; 


struct eventcount_test : rl::test_suite<eventcount_test, 2>
{
    void thread(unsigned index)
    {
        delete ec_thread::current();
        (void)index;
    }
};



int main()
{
    rl::test_params p;
    //p.iteration_count = 1000000;
    rl::simulate<eventcount_test>(p);
}

