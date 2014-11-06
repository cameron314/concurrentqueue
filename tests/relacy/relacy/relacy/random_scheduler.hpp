/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_RANDOM_SCHEDULER_HPP
#define RL_RANDOM_SCHEDULER_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "scheduler.hpp"
#include "random.hpp"


namespace rl
{


template<thread_id_t thread_count>
class random_scheduler : public scheduler<random_scheduler<thread_count>, scheduler_thread_info, thread_count>
{
public:
    typedef scheduler<random_scheduler<thread_count>, scheduler_thread_info, thread_count> base_t;
    typedef typename base_t::thread_info_t thread_info_t;
    typedef typename base_t::shared_context_t shared_context_t;

    struct task_t
    {
    };

    random_scheduler(test_params& params, shared_context_t& ctx, thread_id_t dynamic_thread_count)
        : base_t(params, ctx, dynamic_thread_count)
    {
    }

    thread_id_t iteration_begin_impl()
    {
        rand_.seed(this->iter_);
        unpark_reason reason;
        return schedule_impl(reason, false);
    }

    bool iteration_end_impl()
    {
        return this->iter_ == this->params_.iteration_count;
    }

    thread_id_t schedule_impl(unpark_reason& reason, unsigned /*yield*/)
    {
        thread_id_t const running_thread_count = this->running_threads_count;

        thread_id_t timed_thread_count = this->timed_thread_count_;
        if (timed_thread_count)
        {
            thread_id_t cnt = running_thread_count ? timed_thread_count * 4 : timed_thread_count;
            thread_id_t idx = rand_.rand() % cnt;
            if (idx < timed_thread_count)
            {
                thread_info_t* thr = this->timed_threads_[idx];
                thread_id_t th = thr->index_;
                RL_VERIFY(1 == thr->block_count_);
                this->unpark_thread(th);
                RL_VERIFY(thr->state_ == thread_state_running);
                reason = unpark_reason_timeout;
                return th;
            }
        }

        thread_id_t spurious_thread_count = this->spurious_thread_count_;
        if (spurious_thread_count && running_thread_count)
        {
            thread_id_t cnt = spurious_thread_count * 8;
            thread_id_t idx = rand_.rand() % cnt;
            if (idx < spurious_thread_count)
            {
                thread_info_t* thr = this->spurious_threads_[idx];
                thread_id_t th = thr->index_;
                RL_VERIFY(1 == thr->block_count_);
                this->unpark_thread(th);
                RL_VERIFY(thr->state_ == thread_state_running);
                reason = unpark_reason_spurious;
                return th;
            }
        }

        RL_VERIFY(running_thread_count);
        unsigned index = rand_.rand() % running_thread_count;
        thread_id_t th = this->running_threads[index];
        reason = unpark_reason_normal;
        return th;
    }

    unsigned rand_impl(unsigned limit, sched_type t)
    {
        (void)t;
        unsigned r = rand_.rand() % limit;
        ///!!!
#ifdef RL_MY_TEST
        if (this->iter_ == 8761115)
        {
            char buf [1024];
            sprintf(buf, "rand(%u, %u) = %u\n", t, limit, r);
            OutputDebugStringA(buf);
        }
#endif
        return r;
    }

    iteration_t iteration_count_impl()
    {
        return this->params_.iteration_count;
    }

    void get_state_impl(std::ostream& /*ss*/)
    {
    }

    void set_state_impl(std::istream& /*ss*/)
    {
    }

    void on_thread_block(thread_id_t /*th*/, bool /*yield*/)
    {
    }

private:
    random_generator rand_;

    RL_NOCOPY(random_scheduler);
};


}

#endif
