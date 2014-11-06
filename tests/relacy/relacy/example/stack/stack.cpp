#include "stdafx.h"

#include "../../relacy/relacy_std.hpp"


// TEST FAILS WITH "ACCESS TO FREED MEMORY"

class stack
{
public:
    stack()
        : head_(0)
    {
    }

    void push(int data)
    {
        rl::var<node*> n = new node ();
        n($)->data_($) = data;
        node* next = head_.load(rl::memory_order_relaxed);
        for (;;)
        {
            n($)->next_.store(next, rl::memory_order_relaxed);
            if (head_.compare_exchange_weak(next, n($), rl::memory_order_release))
                break;
        }
    }

    int pop()
    {
        node* n = head_.load(rl::memory_order_relaxed);
        for (;;)
        {
            if (0 == n)
                break;
            node* next = n->next_.load(rl::memory_order_relaxed);
            if (head_.compare_exchange_weak(n, next, rl::memory_order_acquire))
                break;
        }
        if (n)
        {
            int data = n->data_($);
            delete n;
            return data;
        }
        else
        {
            return 0;
        }
    }

private:
    struct node
    {
        std::atomic<node*> next_;
        rl::var<int> data_;
    };

    std::atomic<node*> head_;

    stack(stack const&);
    stack& operator = (stack const&);
};




struct stack_test : rl::test_suite<stack_test, 4>
{
    stack s_;

    int produced_count_;
    int consumed_count_;

    void before()
    {
        produced_count_ = 0;
        consumed_count_ = 0;
    }

    void after()
    {
        typedef rl::test_suite<stack_test, 4> base_t;
        RL_ASSERT(base_t::params::thread_count == produced_count_);
        RL_ASSERT(base_t::params::thread_count == consumed_count_);
    }

    void thread(unsigned /*index*/)
    {
        s_.push(rand() + 1);
        produced_count_ += 1;
        int data = s_.pop();
        RL_ASSERT(data);
        consumed_count_ += 1;
    }
};




int main()
{
    rl::simulate<stack_test>();
}

