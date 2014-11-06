#include "stdafx.h"

#include "../../relacy/windows.h"

/*
#define HANDLE rl::HANDLE

#define CreateSemaphoreA rl::RL_CreateSemaphore($)
#define CreateSemaphoreW rl::RL_CreateSemaphore($)
#ifndef CreateSemaphore
#   define CreateSemaphore CreateSemaphoreW
#endif

//#define CRITICAL_SECTION rl::CRITICAL_SECTION
//#define InitializeCriticalSection rl::InitializeCriticalSection($)

#define CloseHandle rl::RL_CloseHandle($)
*/

#include <stddef.h>


#if defined(WIN32) && defined(_MSC_VER)

#include <windows.h>
#include <intrin.h>

class semaphore
{
public:
    semaphore()
    {
        h_ = rl::CreateSemaphore(0, 0, LONG_MAX, 0, $);
    }

    ~semaphore()
    {
        rl::CloseHandle(h_, $);
    }

    void wait()
    {
        rl::WaitForSingleObject(h_, rl::RL_INFINITE, $);
    }

    void post()
    {
        rl::ReleaseSemaphore(h_, 1, 0, $);
    }

private:
    rl::HANDLE h_;

    semaphore(semaphore const&);
    semaphore& operator = (semaphore const&);
};

class mutex
{
public:
    mutex()
    {
        rl::InitializeCriticalSection(&cs_, $);
    }

    ~mutex()
    {
        rl::DeleteCriticalSection(&cs_, $);
    }

    void lock()
    {
        rl::EnterCriticalSection(&cs_, $);
    }

    void unlock()
    {
        rl::LeaveCriticalSection(&cs_, $);
    }

private:
    rl::CRITICAL_SECTION    cs_;

    mutex(mutex const&);
    mutex& operator = (mutex const&);
};

//void full_memory_fence()
//{
//    _mm_mfence();
//}

//#define THREAD_LOCAL __declspec(thread)

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

//#define THREAD_LOCAL __thread

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
        rl::var<node*>  prev_;
        rl::var<node*>  next_;

        node()
        {
            prev_($) = 0;
            next_($) = 0;
        }
    };

    dlist()
    {
        reset();
    }

    void push(node* n)
    {
        size_t s = size_($).load(rl::memory_order_relaxed);
        size_($).store(s + 1, rl::memory_order_relaxed);
        n->next_($) = head_.next_($);
        n->prev_($) = &head_;
        head_.next_($)->prev_($) = n;
        head_.next_($) = n;
    }

    node* pop()
    {
        if (size_($).load(rl::memory_order_relaxed) == 0)
            return 0;
        node* n = head_.next_($);
        remove(n);
        return n;
    }

    void remove(node* n)
    {
        size_t s = size_($).load(rl::memory_order_relaxed);
        size_($).store(s - 1, rl::memory_order_relaxed);
        n->prev_($)->next_($) = n->next_($);
        n->next_($)->prev_($) = n->prev_($);
    }

    size_t size() const
    {
        return size_($).load(rl::memory_order_relaxed);
    }

    node* begin()
    {
        return head_.next_($);
    }

    void flush_to(dlist& target)
    {
        if (size_($).load(rl::memory_order_relaxed))
        {
            target.size_($).store(size_($).load(rl::memory_order_relaxed));
            target.head_.next_($) = head_.next_($);
            target.head_.next_($)->prev_($) = &target.head_;
            target.tail_.prev_($) = tail_.prev_($);
            target.tail_.prev_($)->next_($) = &target.tail_;
        }
        else
        {
            target.reset();
        }
        reset();
    }

    static bool not_last(node* n)
    {
        return n->next_($) != 0;
    }

    static node* get_next(node* n)
    {
        return n->next_($);
    }

private:
    rl::atomic<size_t>  size_;
    node                head_;
    node                tail_;

    void reset()
    {
        size_($) = 0;
        head_.next_($) = &tail_;
        head_.prev_($) = 0;
        tail_.next_($) = 0;
        tail_.prev_($) = &head_;
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
    rl::var<unsigned>   epoch_;
    rl::atomic<bool>    in_waitset_;
    rl::var<bool>       spurious_;
    rl::var<void*>      ctx_;

    ec_thread()
    {
        epoch_($) = 0;
        in_waitset_($) = false;
        spurious_($) = false;
        ctx_($) = 0;
    }

    ~ec_thread()
    {
        if (spurious_($))
            sema_.wait();
    }

    /*
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
    */

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
        epoch_($) = 0;
    }

    void prepare_wait(ec_thread* th = 0, void* ctx = 0)
    {
        RL_ASSERT(th);
        // this is good place to pump previous spurious wakeup
        if (th->spurious_($))
        {
            th->spurious_($) = false;
            th->sema_.wait();
        }
        th->in_waitset_($).store(true, rl::memory_order_relaxed);
        th->ctx_($) = ctx;
        {
            lock l (mtx_);
            th->epoch_($) = epoch_($).load(rl::memory_order_relaxed);
            waitset_.push(&th->node_);
        }
        rl::atomic_thread_fence($)(rl::memory_order_seq_cst);
    }

    void commit_wait(ec_thread* th = 0)
    {
        RL_ASSERT(th);
        // this check is just an optimization
        //if (th->epoch_($) == epoch_($).load(rl::memory_order_relaxed))
        if (th->in_waitset_($).load(rl::memory_order_acquire))
            th->sema_.wait();
        else
            cancel_wait(true, th); //!!! add 'th'
    }

    void cancel_wait(bool /*from_commit*/, ec_thread* th = 0)
    {
        RL_ASSERT(th);
        // spurious wakeup will be pumped in the following prepare_wait()
        th->spurious_($)  = true;
        // try to remove node from waitset
        if (th->in_waitset_($).load(rl::memory_order_acquire))
        {
            lock l (mtx_);
            if (th->in_waitset_($).load(rl::memory_order_relaxed))
            {
                // successfully removed from waitset,
                // so there will be no spurious wakeup
                th->in_waitset_($).store(false, rl::memory_order_relaxed);
                th->spurious_($) = false;
                waitset_.remove(&th->node_);
            }
            else
            {
                //if (from_commit)
                    //int volatile x = 0;
            }
        }
        else
        {
            //RL_ASSERT(from_commit == false);
            //if (from_commit)
              //  int volatile x = 0;
        }
    }

    void notify_one()
    {
        rl::atomic_thread_fence($)(rl::memory_order_seq_cst);
        notify_one_relaxed();
    }

    template<typename predicate_t>
    void notify(predicate_t pred)
    {
        rl::atomic_thread_fence($)(rl::memory_order_seq_cst);
        notify_relaxed(pred);
    }

    void notify_all()
    {
        rl::atomic_thread_fence($)(rl::memory_order_seq_cst);
        notify_all_relaxed();
    }

    void notify_one_relaxed()
    {
        if (waitset_.size() == 0)
            return;
        dlist::node* n;
        {
            lock l (mtx_);
            unsigned ep = epoch_($).load(rl::memory_order_relaxed);
            epoch_($).store(ep + 1, rl::memory_order_relaxed);
            n = waitset_.pop();
            if (n)
                to_ec_thread(n)->in_waitset_($).store(false, rl::memory_order_release);
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
            unsigned ep = epoch_($).load(rl::memory_order_relaxed);
            epoch_($).store(ep + 1, rl::memory_order_relaxed);
            size_t size = waitset_.size();
            size_t idx = 0;
            dlist::node* n = waitset_.begin();
            while (dlist::not_last(n))
            {
                dlist::node* next = dlist::get_next(n);
                ec_thread* th = to_ec_thread(n);
                if (pred(th->ctx_($), size, idx))
                {
                    waitset_.remove(n);
                    temp.push(n);
                    th->in_waitset_($).store(false, rl::memory_order_release);
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
            waitset_.flush_to(temp);
            dlist::node* n = temp.begin();
            while (dlist::not_last(n))
            {
                to_ec_thread(n)->in_waitset_($).store(false, rl::memory_order_release);
                n = dlist::get_next(n);
            }
            unsigned ep = epoch_($).load(rl::memory_order_relaxed);
            epoch_($).store(ep + 1, rl::memory_order_relaxed);
        }
        dlist::node* n = temp.begin();
        while (dlist::not_last(n))
        {
            dlist::node* next = dlist::get_next(n);
            to_ec_thread(n)->sema_.post();
            n = next;
        }
    }

    class wait_guard;

private:
    mutex               mtx_;
    dlist               waitset_;
    rl::atomic<unsigned>epoch_;

    ec_thread* to_ec_thread(dlist::node* n)
    {
        return (ec_thread*)((char*)n - offsetof(ec_thread, node_));
    }

    eventcount(eventcount const&);
    eventcount& operator = (eventcount const&);
};




class eventcount::wait_guard
{
public:
    wait_guard(eventcount& ec, ec_thread* th = 0, void* ctx = 0)
        : ec_(ec)
        , th_(th)
        , wait_(false)
    {
        ec_.prepare_wait(th_, ctx);
    }

    void commit_wait()
    {
        assert(false == wait_);
        wait_ = true;
        ec_.commit_wait(th_);
    }

    ~wait_guard()
    {
        if (false == wait_)
            ec_.cancel_wait(false, th_);
    }

private:
    eventcount& ec_;
    ec_thread* th_;
    bool wait_;

    wait_guard(wait_guard const&);
    wait_guard& operator = (wait_guard const&);
};





struct scheduler
{
    struct tbb_thread
    {
        ec_thread       th;
    };

    eventcount          ec_;
    tbb_thread*         threads_;
    bool volatile       is_permanently_open_;

    void wait_while_pool_is_empty(tbb_thread* th)
    {
        if (is_permanently_open_)
            return;
        eventcount::wait_guard wait (ec_, &th->th);
        if (pool_is_empty())
            wait.commit_wait();
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
    rl::atomic<int>     producer_idx_;
    rl::atomic<int>     consumer_idx_;

    rl::atomic<void*>*  buffer_;

    eventcount          ec_;

    queue()
    {
        producer_idx_($) = 0;
        consumer_idx_($) = 0;
        buffer_ = RL_NEW_ARR(rl::atomic<void*>, 10);
        for (size_t i = 0; i != 10; ++i)
            buffer_[i]($) = 0;
    }

    ~queue()
    {
        RL_DELETE_ARR(buffer_);
    }

    void enqueue(void* data)
    {
        int idx = producer_idx_($).fetch_add(1) + 1; // atomic
        buffer_[idx]($).store(data, rl::memory_order_relaxed);

        struct local
        {
            int         idx_;
            bool operator () (void* ctx, size_t /*count*/, size_t /*idx*/)
            {
                return idx_ == (*(rl::var<int>*)ctx)($);
            }
        }
        pred = {idx};
        ec_.notify(pred); // not relaxed!!!
    }

    void* dequeue(ec_thread* th)
    {
        int idx = consumer_idx_($).fetch_add(1) + 1; // atomic
        void* data = buffer_[idx]($).load(rl::memory_order_relaxed);
        if (data)
            return data;
        for (;;)
        {
            rl::var<int> idxv (idx);
            eventcount::wait_guard wait (ec_, th, &idxv);
            data = buffer_[idx]($).load(rl::memory_order_relaxed);
            if (data)
            {
                return data;
            }
            wait.commit_wait();
            idxv($) = 0;
            data = buffer_[idx]($).load(rl::memory_order_relaxed);
            if (data)
            {
                return data;
            }
            rl::yield($, 1);
            //RL_ASSERT(false);
        }
    }
};



class condition_variable
{
    eventcount ec_;

public:
    void wait(mutex& mtx, ec_thread* th)
    {
        eventcount::wait_guard wait (ec_, th);
        mtx.unlock();
        wait.commit_wait();
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


struct queue_test : rl::test_suite<queue_test, 4>
{
    ec_thread threads_ [6];
    queue q_;

    void thread(unsigned index)
    {
        if (index < 2)
        {
            q_.enqueue((void*)(index*2+1));
            q_.enqueue((void*)(index*2+2));
        }
        else
        {
            int data1 = (int)q_.dequeue(&threads_[index]);
            RL_ASSERT(data1 >= 1 && data1 <= 6);
            int data2 = (int)q_.dequeue(&threads_[index]);
            RL_ASSERT(data2 >= 1 && data2 <= 6);
        }
    }
};

struct condvar_test : rl::test_suite<condvar_test, 3>
{
    rl::var<int> stage;
    condition_variable cv;
    mutex mtx;
    ec_thread th [3];

    void before()
    {
        stage($) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            mtx.lock();
            stage($) += 1;
            cv.broadcast();
            while (stage($) != 2)
                cv.wait(mtx, &th[index]);
            mtx.unlock();
        }
        else if (1 == index)
        {
            mtx.lock();
            while (stage($) != 1)
                cv.wait(mtx, &th[index]);
            stage($) += 1;
            cv.broadcast();
            mtx.unlock();
        }
        else if (2 == index)
        {
            mtx.lock();
            while (stage($) != 2)
                cv.wait(mtx, &th[index]);
            mtx.unlock();
        }
    }
};


int main()
{
    rl::test_params p;
    p.iteration_count = 100000000;
    //p.initial_state = "30000000";
    //p.search_type = rl::fair_context_bound_scheduler_type;
    rl::simulate<queue_test>(p);
    //rl::simulate<condvar_test>(p);
}

