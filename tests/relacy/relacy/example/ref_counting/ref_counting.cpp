#include "stdafx.h"

#include "../../relacy/relacy_std.hpp"


struct rc_object
{
    std::atomic<int> rc;
    rl::var<int> data;

    void acquire()
    {
        rc.fetch_add(1, rl::memory_order_acquire);
    }

    void release()
    {
        if (1 == rc.fetch_sub(1, rl::memory_order_release))
        {
            rc.load(rl::memory_order_acquire);
            data($) = 0;
            delete this;
        }
    }

    rc_object(int data)
        : rc(1)
        , data(data)
    {
    }
};




void post_to_channel(rl::atomic<rc_object*>& ch, rc_object* obj)
{
    obj->acquire();
    rl::backoff b;
    for (;;)
    {
        rc_object* cmp = 0;
        if (ch.compare_exchange_weak(cmp, obj, rl::memory_order_release))
            break;
        b.yield($);
    }
}

rc_object* get_from_channel(rl::atomic<rc_object*>& ch)
{
    return ch.exchange(0, rl::memory_order_acquire);
}




struct ref_counting_test : rl::test_suite<ref_counting_test, 2>
{
    std::atomic<rc_object*> channel;

    void before()
    {
        channel($) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            rc_object* obj = new rc_object (rand());
            post_to_channel(channel, obj);
            int data = obj->data($);
            (void)data;
            obj->release();
        }
        else if (1 == index)
        {
            rl::backoff b;
            for (;;)
            {
                rc_object* obj = get_from_channel(channel);
                if (obj)
                {
                    int data = obj->data($);
                    (void)data;
                    obj->release();
                    break;
                }
                else
                {
                    b.yield($);
                }
            }
        }
    }
};




struct ref_counting_test2 : rl::test_suite<ref_counting_test2, 3>
{
    std::atomic<rc_object*> channel01;
    std::atomic<rc_object*> channel02;
    std::atomic<rc_object*> channel12;
    std::atomic<rc_object*> channel21;

    void before()
    {
        channel01($) = 0;
        channel02($) = 0;
        channel12($) = 0;
        channel21($) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            {
                rc_object* obj1 = new rc_object (rand());
                post_to_channel(channel01, obj1);
                volatile int data = obj1->data($);
                (void)data;
                obj1->release();
            }

            {
                rc_object* obj2 = new rc_object (rand());
                post_to_channel(channel02, obj2);
                volatile int data = obj2->data($);
                (void)data;
                obj2->release();
            }
        }
        else if (1 == index)
        {
            rl::backoff b;
            bool ch0 = false;
            bool ch2 = false;
            while (!ch0 || !ch2)
            {
                {
                    rc_object* obj = get_from_channel(channel01);
                    if (obj)
                    {
                        post_to_channel(channel12, obj);
                        volatile int data = obj->data($);
                        (void)data;
                        obj->release();
                        ch0 = true;
                    }
                    else
                    {
                        b.yield($);
                    }
                }
                {
                    rc_object* obj = get_from_channel(channel21);
                    if (obj)
                    {
                        volatile int data = obj->data($);
                        (void)data;
                        obj->release();
                        ch2 = true;
                    }
                    else
                    {
                        b.yield($);
                    }
                }
            }
        }
        else
        {
            rl::backoff b;
            bool ch0 = false;
            bool ch1 = false;
            while (!ch0 || !ch1)
            {
                {
                    rc_object* obj = get_from_channel(channel02);
                    if (obj)
                    {
                        post_to_channel(channel21, obj);
                        volatile int data = obj->data($);
                        (void)data;
                        obj->release();
                        ch0 = true;
                    }
                    else
                    {
                        b.yield($);
                    }
                }
                {
                    rc_object* obj = get_from_channel(channel12);
                    if (obj)
                    {
                        volatile int data = obj->data($);
                        (void)data;
                        obj->release();
                        ch1 = true;
                    }
                    else
                    {
                        b.yield($);
                    }
                }
            }
        }
    }
};




struct ref_counting_test3 : rl::test_suite<ref_counting_test3, 2>
{
    std::atomic<rc_object*> channel;

    void before()
    {
        channel($) = 0;
    }

    void thread(unsigned index)
    {
        if (0 == index)
        {
            rc_object* obj = new rc_object (rand());
            post_to_channel(channel, obj);
            volatile int data = obj->data($);
            (void)data;
            obj->release();
        }
        else if (1 == index)
        {
            rl::backoff b;
            rc_object* obj = 0;
            for (;;)
            {
                obj = get_from_channel(channel);
                if (obj)
                    break;
                else
                    b.yield($);
            }
            obj->acquire();
            obj->release();
            //volatile int data = obj->data($);
            //(void)data;
            obj->release();
        }
    }
};




int main()
{
    rl::test_params params;
    params.context_bound = 2;
    params.iteration_count = 10000;
    rl::simulate<ref_counting_test>(params);
    std::cout << "count: " << params.stop_iteration << std::endl;
}

