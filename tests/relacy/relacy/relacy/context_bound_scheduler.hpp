/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_CONTEXT_BOUND_SCHEDULER_HPP
#define RL_CONTEXT_BOUND_SCHEDULER_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "full_search_scheduler.hpp"
#include "foreach.hpp"


namespace rl
{


template<thread_id_t thread_count>
struct context_bound_scheduler_thread_info : tree_search_scheduler_thread_info<thread_count>
{
    unsigned sched_count_;
    unsigned forced_context_switch_count_;

    void reset(test_params& params)
    {
        tree_search_scheduler_thread_info<thread_count>::reset(params);
        sched_count_ = 0;
        forced_context_switch_count_ = 0;
    }
};




template<thread_id_t thread_count>
class context_bound_scheduler
    : public tree_search_scheduler<context_bound_scheduler<thread_count>
        , context_bound_scheduler_thread_info<thread_count>, thread_count>
{
public:
    typedef tree_search_scheduler<context_bound_scheduler<thread_count>
        , context_bound_scheduler_thread_info<thread_count>, thread_count> base_t;
    typedef typename base_t::thread_info_t thread_info_t;
    typedef typename base_t::shared_context_t shared_context_t;

    context_bound_scheduler(test_params& params, shared_context_t& ctx, thread_id_t dynamic_thread_count)
        : base_t(params, ctx, dynamic_thread_count)
    {
    }

    thread_id_t iteration_begin_impl()
    {
        switches_remain_ = this->params_.context_bound;
        return base_t::iteration_begin_impl();
    }

    bool can_switch(thread_info_t& t)
    {
        t.sched_count_ += 1;
        return switches_remain_ != 0;
    }

    void on_switch(thread_info_t& t)
    {
        if (t.state_ == thread_state_running)
        {
            RL_VERIFY(switches_remain_);
            switches_remain_ -= 1;
        }
        else
        {
            t.forced_context_switch_count_ += 1;
        }
    }

    double iteration_count_approx()
    {
        return 1.0;
        /*
        iteration_t const P = thread_count;
        iteration_t const C0 = this->params_.context_bound;
        iteration_t total = 1;//factorial(P);// * power(P, P * C0);
        for (iteration_t i = 0; i != P - 1; ++i)
            total *= power(i + 1, C0 + 1);
        //if (C0)
        //    total *= power(P - 1, P - 1);
        if (val(P) > 1)
        {
            for (iteration_t i = 0; i != P; ++i)
            {
                iteration_t const N = this->threads_[i].sched_count_;
                iteration_t const C = C0 + this->threads_[i].forced_context_switch_count_;
                //total *= (iteration_t)pow((double)(threads_[i].sched_count_ + 2) * (thread_count - 1), (int)(params_.context_bound + threads_[i].forced_context_switch_count_));
                total *= factorial(N, C) / factorial(C);
                //C$ += C + 1;
                //total *= (int)(params_.context_bound + threads_[i].forced_context_switch_count_));
            }
            //total *= factorial(C$);
        }
        else
        {
            total = 1;
        }
        //iteration_t total = (iteration_t)pow((double)sched_count / thread_count + 1, (int)(params_.context_bound * thread_count + forced_context_switch_mean_ + 0.5));
        //total *= thread_count;
        //total *= (iteration_t)pow((double)thread_count - 1, thread_count);
        for (size_t i = 0; i != this->stree_.size(); ++i)
        {
            if (this->stree_[i].type_ != sched_type_sched)
            {
                total *= this->stree_[i].count_;
            }
        }
        return (double)total;
        */
    }

private:
    unsigned switches_remain_;

    template<typename T>
    static T factorial(T x, T i)
    {
        if (0 == i)
            return 1;
        T r = x;
        for (--i; i; --i)
            r *= x - i;
        return r;
    }

    template<typename T>
    static T factorial(T x)
    {
        if (0 == x)
            return 1;
        T r = x;
        for (T i = x - 1; i; --i)
            r *= i;
        return r;
    }

    template<typename T>
    static T power(T x, T y)
    {
        if (0 == y)
            return 1;
        T r = x;
        for (T i = y - 1; i; --i)
            r *= x;
        return r;
    }

    RL_NOCOPY(context_bound_scheduler);
};


}

#endif

