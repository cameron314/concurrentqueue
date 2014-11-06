#include "stdafx.h"

#include "../../relacy/relacy_std.hpp"


unsigned const thread_count = 3;
unsigned const node_count = 6;


struct smr_test : rl::test_suite<smr_test, thread_count>
{
    struct node
    {
        std::atomic<node*> next_;
        rl::var<int> data_;
    };

    std::atomic<node*> head_;

    std::atomic<node*> hp_ [thread_count];
    rl::var<node*> defer_ [thread_count][thread_count];
    rl::var<int> defer_size_ [thread_count];

    void before()
    {
        head_.store(0, std::memory_order_relaxed);

        for (size_t i = 0; i != thread_count; ++i)
        {
            hp_[i].store(0, std::memory_order_relaxed);
            VAR(defer_size_[i]) = 0;
            for (size_t j = 0; j != thread_count; ++j)
                VAR(defer_[i][j]) = 0;
        }
    }

    void push(unsigned index, int data)
    {
        node* n = new node ();
        n->VAR(data_) = data;
        node* next = head_.load(std::memory_order_relaxed);
        for (;;)
        {
            n->next_.store(next, rl::memory_order_relaxed);
            if (head_.compare_exchange_weak(next, n, rl::memory_order_release))
                break;
        }
    }

    int pop(unsigned index)
    {
        node* n = 0;
        for (;;)
        {
            n = smr_acquire(index, head_);
            if (0 == n)
                break;
            node* next = n->next_.load(rl::memory_order_relaxed);
            if (head_.compare_exchange_weak(n, next, rl::memory_order_acquire))
                break;
            smr_release(index);
        }
        smr_release(index);
        if (n)
        {
            int data = n->VAR(data_);
            smr_defer(index, n);
            return data;
        }
        else
        {
            return 0;
        }
    }

    void smr_pump(unsigned index)
    {
        node* hp [thread_count] = {};
        for (size_t i = 0; i != thread_count; ++i)
        {
            hp[i] = hp_[i].load(std::memory_order_relaxed);
        }

        for (size_t i = 0; i != thread_count; ++i)
        {
            node* nn = VAR(defer_[index][i]);
            if (nn)
            {
                for (size_t j = 0; j != thread_count; ++j)
                {
                    if (nn == hp[j])
                    {
                        nn = 0;
                        break;
                    }
                }
                if (nn)
                {
                    VAR(defer_[index][i]) = 0;
                    VAR(defer_size_[index]) -= 1;
                    delete nn;
                }
            }
        }
    }

    void smr_defer(unsigned index, node* n)
    {
        std::atomic_thread_fence(std::memory_order_seq_cst);

        smr_pump(index);

        if (VAR(defer_size_[index]) == thread_count)
        {
            delete n;
        }
        else
        {
            bool found = false;
            for (size_t i = 0; i != thread_count; ++i)
            {
                if (VAR(defer_[index][i]) == 0)
                {
                    VAR(defer_[index][i]) = n;
                    found = true;
                    break;
                }
            }
            RL_ASSERT(found);
            VAR(defer_size_[index]) += 1;
        }
    }

    node* smr_acquire(unsigned index, std::atomic<node*>& n)
    {
        node* v = 0;
        for (;;)
        {
            v = n.load(std::memory_order_relaxed);
            hp_[index].store(v, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            node* v2 = n.load(std::memory_order_acquire);
            if (v2 == v)
                break;
        }
        return v;
    }

    void smr_release(unsigned index)
    {
        hp_[index].store(0, std::memory_order_relaxed);
    }

    void thread(unsigned index)
    {
        for (unsigned i = 0; i != node_count; ++i)
        {
            push(index, index * thread_count + i + 1);
        }
        for (unsigned i = 0; i != node_count; ++i)
        {
            int data = pop(index);
            RL_ASSERT(0 != data);
        }
    }

    void after()
    {
        for (unsigned i = 0; i != ::thread_count; ++i)
        {
            smr_pump(i);
        }
    }
};




int main()
{
    rl::test_params p;
    //p.collect_history = true;
    //p.output_history = true;
    //p.initial_state = "991172";
    p.iteration_count = 1000;
    rl::simulate<smr_test>(p);
}


