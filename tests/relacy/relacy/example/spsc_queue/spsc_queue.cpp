#include "stdafx.h"
#include "../../relacy/relacy_std.hpp"

template<typename T>
class nonblocking_spsc_queue
{
public:
    nonblocking_spsc_queue()
    {
        node* n = new node ();
        VAR(head) = n;
        VAR(tail) = n;
    }

    ~nonblocking_spsc_queue()
    {
        RL_ASSERT(VAR(head) == VAR(tail));
        delete (node*)VAR(head);
    }

    void enqueue(T data)
    {
        node* n = new node (data);
        VAR(head)->next.store(n, std::memory_order_release); 
        VAR(head) = n;
    }

    bool dequeue(T& data)
    {
        node* t = VAR(tail);
        node* n = t->next.load(std::memory_order_acquire);
        if (0 == n)
            return false;
        data = n->VAR(data);
        delete t;
        VAR(tail) = n;
        return true;
    }

private:
    struct node
    {
        std::atomic<node*> next;
        VAR_T(T) data;

        node(T data = T())
            : next(0)
            , data(data)
        {}
    };

    VAR_T(node*) head;
    VAR_T(node*) tail;
};

struct nonblocking_spsc_queue_test : rl::test_suite<nonblocking_spsc_queue_test, 2>
{
    nonblocking_spsc_queue<int> q;

    void thread(unsigned thread_index)
    {
        if (0 == thread_index)
        {
            q.enqueue(11);
        }
        else
        {
            int data = 0;
            while (false == q.dequeue(data))
            {}
            RL_ASSERT(11 == data);
        }
    }
};


class eventcount
{
public:
    eventcount()
        : count(0)
        , waiters(0)
    {}

    void signal_relaxed()
    {
        unsigned cmp = count.load(std::memory_order_relaxed);
        signal_impl(cmp);
    }

    void signal()
    {
        unsigned cmp = count.fetch_add(0, std::memory_order_seq_cst);
        signal_impl(cmp);
    }

    unsigned get()
    {
        unsigned cmp = count.fetch_or(0x80000000, std::memory_order_acquire);
        return cmp & 0x7FFFFFFF;
    }

    void wait(unsigned cmp)
    {
        unsigned ec = count.load(std::memory_order_seq_cst);
        if (cmp == (ec & 0x7FFFFFFF))
        {
            guard.lock($);
            ec = count.load(std::memory_order_seq_cst);
            if (cmp == (ec & 0x7FFFFFFF))
            {
                waiters($) += 1;
                cv.wait(guard, $);
            }
            guard.unlock($);
        }
    }

private:
    std::atomic<unsigned> count;
    VAR_T(unsigned) waiters;
    std::mutex guard;
    std::condition_variable cv;

    void signal_impl(unsigned cmp)
    {
        if (cmp & 0x80000000)
        {
            guard.lock($);
            while (false == count.compare_exchange_weak(cmp,
                (cmp + 1) & 0x7FFFFFFF, std::memory_order_relaxed));
            unsigned w = VAR(waiters);
            VAR(waiters) = 0;
            guard.unlock($);
            if (w)
                cv.notify_all($);
        }
    }
};



template<typename T>
class spsc_queue : nonblocking_spsc_queue<T>
{
public:
    typedef nonblocking_spsc_queue<T> base_t;

    void enqueue(T data)
    {
        base_t::enqueue(data);
        ec.signal/*_relaxed*/();
    }

    T dequeue()
    {
        T data;
        bool res = base_t::dequeue(data);
        while (false == res)
        {
            int cmp = ec.get();
            res = base_t::dequeue(data);
            if (res)
                break;
            ec.wait(cmp);
            res = base_t::dequeue(data);
            if (res)
                break;
        }
        return data;
    }

private:
    eventcount ec;
};


struct spsc_queue_test : rl::test_suite<spsc_queue_test, 2>
{
    spsc_queue<int> q;

    void thread(unsigned thread_index)
    {
        if (0 == thread_index)
        {
            q.enqueue(11);
        }
        else
        {
            int d = q.dequeue();
            RL_ASSERT(11 == d);
        }
    }
};


int main()
{
    rl::simulate<nonblocking_spsc_queue_test>();
    rl::simulate<spsc_queue_test>();
}

