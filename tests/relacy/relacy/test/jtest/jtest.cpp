#include "stdafx.h"

#include "../relacy/relacy_java.hpp"





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
        VAR(n)->VAR(data_) = data;
        node* next = head_.load(rl::memory_order_relaxed);
        for (;;)
        {
            VAR(n)->next_.store(next, rl::memory_order_relaxed);
            if (head_.compare_exchange_weak(next, VAR(n), rl::memory_order_release))
                break;
        }
    }

    int pop()
    {
        node* n = head_.load(rl::memory_order_acquire);
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
            int data = n->VAR(data_);
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
        rl::atomic<node*> next_;
        rl::var<int> data_;
    };

    rl::atomic<node*> head_;

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


struct test_api : rl::test_suite<test_api, 1>
{
    void thread(unsigned)
    {
        rl::jvolatile<int> jv1;
        rl::jvolatile<int> jv2 (2);
        rl::jvolatile<int> jv3 (jv2($));
        rl::jvolatile<int> jv4 (jv1);
        jv1($) = jv3($);
        jv1($) = 2;
        (int)jv1($);
        jv1($) += 1;
        jv1($) -= 1;
        int x = jv1($)++;
        x = jv1($)--;
        x = --jv1($);
        x = ++jv1($);

        rl::AtomicInteger ai, ai2(1), ai3(x), ai4(ai($)), ai5(ai);
        x = ai($).get();
        ai($).set(1);
        x = ai($).addAndGet(2);
        bool b = ai($).compareAndSet(1, 2);
        (void)b;
        x = ai($).addAndGet(2);
        x = ai($).getAndSet(2);
    }
};

struct test_seq_cst_volatiles : rl::test_suite<test_seq_cst_volatiles, 2>
{
    rl::jvolatile<int> flag0;
    rl::jvolatile<int> flag1;
    rl::jvolatile<int> turn;

    rl::var<int> data;

    void thread(unsigned index)
    {
        if (0 == index)
        {
            flag0($) = 1;
            turn($) = 1;
            while (flag1($) && 1 == turn($))
                rl::yield(1, $);
            data($) = 1;
            flag0($) = 0;
        }
        else
        {
            flag1($) = 1;
            turn($) = 0;
            while (flag0($) && 0 == turn($))
                rl::yield(1, $);
            data($) = 2;
            flag1($) = 0;
        }
    }
};

struct test_seq_cst_volatiles2 : rl::test_suite<test_seq_cst_volatiles2, 4>
{
    rl::jvolatile<int> x;
    rl::jvolatile<int> y;

    int r1, r2, r3, r4;

    void before()
    {
        r1 = r2 = r3 = r4 = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            x($) = 0;
        }
        else if (1 == index)
        {
            y($) = 0;
        }
        else if (2 == index)
        {
            r1 = x($);
            r2 = y($);
        }
        else if (3 == index)
        {
            r3 = y($);
            r4 = x($);
        }
    }

    void after()
    {
        RL_ASSERT(false == (r1 && !r2 && r3 && !r4));
    }
};

template<int expected>
struct test_unitialized_var : rl::test_suite<test_unitialized_var<expected>, 2, rl::test_result_until_condition_hit>
{
    rl::jvar<rl::jvar<int>*> www;

    void thread(unsigned index)
    {
        if (0 == index)
        {
            www($) = new rl::jvar<int> (1);
        }
        else
        {
            while (0 == www($))
                rl::yield(1, $);
            int x = (*www($))($);
            RL_UNTIL(x == expected);
        }
    }
};


int main()
{
    rl::simulate_f tests[] = 
    {
        //!!! broken &rl::simulate<test_unitialized_var<0> >,
        &rl::simulate<test_unitialized_var<1> >,
        &rl::simulate<test_seq_cst_volatiles>,
        &rl::simulate<test_seq_cst_volatiles2>,
        &rl::simulate<test_api>,
        &rl::simulate<stack_test>,
    };

    for (size_t i = 0; i != sizeof(tests)/sizeof(*tests); ++i)
    {
        rl::ostringstream stream;
        rl::test_params params;
        params.iteration_count = 10000;
        params.output_stream = &stream;
        params.progress_stream = &stream;
        params.context_bound = 2;
        params.execution_depth_limit = 500;

        if (false == tests[i](params))
        {
            std::cout << std::endl;
            std::cout << "FAILED" << std::endl;
            std::cout << stream.str();
            return 1;
        }
        else
        {
            std::cout << params.test_name << "...OK" << std::endl;
        }
    }

    std::cout << std::endl << "SUCCESS" << std::endl;
}

