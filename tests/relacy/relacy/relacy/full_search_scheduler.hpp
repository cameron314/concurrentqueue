/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_FULL_SEARCH_SCHEDULER_HPP
#define RL_FULL_SEARCH_SCHEDULER_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "scheduler.hpp"
#include "foreach.hpp"


namespace rl
{


template<thread_id_t thread_count>
struct tree_search_scheduler_thread_info : scheduler_thread_info
{
    unsigned                    yield_sched_count_ [thread_count];
    unsigned                    yield_priority_ [thread_count];
    unsigned                    total_yield_priority_;
    //unsigned                    subsequent_timed_waits_;

    void reset(test_params& params)
    {
        scheduler_thread_info::reset(params);
        foreach<thread_count>(yield_sched_count_, &assign_zero_u);
        foreach<thread_count>(yield_priority_, &assign_zero_u);
        total_yield_priority_ = 0;
        //subsequent_timed_waits_ = 0;
    }
};




template<typename derived_t, typename thread_info_type, thread_id_t thread_count>
class tree_search_scheduler
    : public scheduler<derived_t, thread_info_type, thread_count>
{
public:
    typedef scheduler<derived_t, thread_info_type, thread_count> base_t;
    typedef typename base_t::thread_info_t thread_info_t;
    typedef typename base_t::shared_context_t shared_context_t;

    struct task_t
    {
    };

    tree_search_scheduler(test_params& params, shared_context_t& ctx, thread_id_t dynamic_thread_count)
        : base_t(params, ctx, dynamic_thread_count)
        , stree_depth_()
        , iteration_count_mean_()
        , iteration_count_probe_count_()
    {
        stree_.reserve(128);
    }

    thread_id_t iteration_begin_impl()
    {
        stree_depth_ = 0;

        unsigned const index = rand_impl(this->running_threads_count, sched_type_sched);
        thread_id_t const th = this->running_threads[index];
        return th;
    }

    bool iteration_end_impl()
    {
        RL_VERIFY(stree_depth_ == stree_.size());

        for (size_t i = stree_.size(); i != 0; --i)
        {
            stree_node& n = stree_[i - 1];
            if (n.index_ != n.count_ - 1)
            {
                stree_.resize(i);
                n.index_ += 1;
                RL_VERIFY(n.index_ < n.count_);
                return false;
            }
        }
        return true;
    }

    void yield_priority(unsigned yield)
    {
        RL_VERIFY(yield);

        thread_info_t& t = *this->thread_;
        thread_id_t const& running_thread_count = this->running_threads_count;

        for (thread_id_t i = 0; i != thread_count; ++i)
        {
            thread_info_t& y = this->threads_[i];
            RL_VERIFY(0 == y.yield_priority_[t.index_]);

            if (t.index_ != i
                && y.yield_sched_count_[t.index_] < yield
                && y.state_ != thread_state_finished)
            {
                y.yield_priority_[t.index_] = yield;
                y.total_yield_priority_ += yield;
                this->block_thread(t.index_, false);
            }
            y.yield_sched_count_[t.index_] = 0;
        }

        if (0 == running_thread_count)
            purge_blocked_threads();
    }

    thread_id_t schedule_impl(unpark_reason& reason, unsigned yield)
    {
        thread_info_t& t = *this->thread_;
        thread_id_t const& running_thread_count = this->running_threads_count;

#ifdef _DEBUG
        {
            unsigned tmp = 0;
            for (thread_id_t i = 0; i != thread_count; ++i)
                tmp += t.yield_priority_[i];
            RL_VERIFY(t.total_yield_priority_ == tmp);
        }
#endif

        if (t.total_yield_priority_)
        {
            for (thread_id_t i = 0; i != thread_count; ++i)
            {
                unsigned& prio = t.yield_priority_[i];
                if (prio)
                {
                    prio -= 1;
                    t.total_yield_priority_ -= 1;
                    if (0 == prio)
                    {
                        this->unblock_thread(i);
                    }
                }
                t.yield_sched_count_[i] += 1;
            }
        }

        if (yield)
            yield_priority(yield);

        reason = unpark_reason_normal;
        thread_id_t thread_index = 0;

        if (self().can_switch(t)
            || t.state_ != thread_state_running)
        {
            thread_id_t timed_thread_count = this->timed_thread_count_;
            if (timed_thread_count)
            {
                thread_id_t cnt;
                if (running_thread_count)
                    cnt = timed_thread_count + 1;
                else
                    //!!! spurious thread will be never unblocked in such case - bad
                    cnt = timed_thread_count;
                thread_id_t idx = this->rand(cnt, sched_type_user);
                if (idx < timed_thread_count)
                {
                    thread_info_t* thr = this->timed_threads_[idx];
                    thread_index = thr->index_;
                    //??? suboptimal state space exploration
                    // if (1 != thr->block_count_) then we are making
                    // superfluous rand()
                    if (1 == thr->block_count_)
                    {
                        this->unpark_thread(thread_index);
                        RL_VERIFY(thr->state_ == thread_state_running);
                        reason = unpark_reason_timeout;
                    }
                }
            }

            RL_VERIFY(running_thread_count);

            if (unpark_reason_normal == reason)
            {
                thread_id_t spurious_thread_count = this->spurious_thread_count_;
                if (spurious_thread_count)
                {
                    thread_id_t cnt = spurious_thread_count + 1;
                    thread_id_t idx = this->rand(cnt, sched_type_user);
                    if (idx < spurious_thread_count)
                    {
                        thread_info_t* thr = this->spurious_threads_[idx];
                        thread_index = thr->index_;
                        //??? suboptimal state space exploration
                        // if (1 != thr->block_count_) then we are making
                        // superfluous rand()
                        if (1 == thr->block_count_)
                        {
                            this->unpark_thread(thread_index);
                            RL_VERIFY(thr->state_ == thread_state_running);
                            reason = unpark_reason_spurious;
                        }
                    }
                }
            }

            if (unpark_reason_normal == reason)
            {
                if (1 != running_thread_count)
                {
                    unsigned const index = this->rand(running_thread_count, sched_type_sched);
                    thread_index = this->running_threads[index];
                }
                else
                {
                    thread_index = this->running_threads[0];
                }
            }
        }
        else
        {
            RL_VERIFY(t.state_ == thread_state_running);
            thread_index = t.index_;
        }

        if (t.index_ == thread_index)
            return thread_index;

        //t.subsequent_timed_waits_ = 0;
        self().on_switch(t);

        return thread_index;
    }

    void thread_finished_impl()
    {
    }

    void purge_blocked_threads()
    {
        for (thread_id_t i = 0; i != thread_count; ++i)
        {
            on_thread_block(i, false);
        }
    }

    unsigned rand_impl(unsigned limit, sched_type t)
    {
        unsigned result = 0;
        size_t const size = stree_.size();
        if (stree_depth_ == size)
        {
            stree_node n = {limit, 0, t};
            stree_.push_back(n);
        }
        else
        {
            RL_VERIFY(size);
            stree_node& n = stree_[stree_depth_];

            // If you hit assert here, then probably your test is non-deterministic
            // Check whether you are using functions like ::rand()
            // or static variables or values of object addresses (for hashing) in your test
            // Replace ::rand() with rl::rand(), eliminate static variables in the test
            RL_VERIFY(n.type_ == t);

            RL_VERIFY(n.count_ == limit);
            RL_VERIFY(n.index_ < n.count_);
            result = n.index_;
        }
        stree_depth_ += 1;
        return result;
    }

    iteration_t iteration_count_impl()
    {
        double current = self().iteration_count_approx();
        if (current <= this->iter_)
            current = this->iter_ + 1.0;

        iteration_count_mean_ *= iteration_count_probe_count_;
        iteration_count_probe_count_ += 1;
        iteration_count_mean_ /= iteration_count_probe_count_;
        iteration_count_mean_ += current / iteration_count_probe_count_;

        iteration_t result = (iteration_t)(iteration_count_mean_ + 0.5);
        if (result <= this->iter_)
            result = this->iter_ + 1;
        return result;
    }

    void get_state_impl(std::ostream& ss)
    {
        ss << (unsigned)stree_.size() << " ";
        for (size_t i = 0; i != stree_.size(); ++i)
        {
            stree_node& n = stree_[i];
            ss << n.count_ << " ";
            ss << n.index_ << " ";
            ss << static_cast<unsigned>(n.type_) << " ";
        }
    }

    void set_state_impl(std::istream& ss)
    {
        size_t size = 0;
        ss >> size;
        for (size_t i = 0; i != size; ++i)
        {
            stree_node n = {};
            ss >> n.count_;
            ss >> n.index_;
            unsigned type = 0;
            ss >> type;
            n.type_ = static_cast<sched_type>(type);
            stree_.push_back(n);
        }
    }

    void on_thread_block(thread_id_t th, bool yield)
    {
        //!!! doubled in schedule_impl()
        thread_info_t& t = this->threads_[th];
        if (t.total_yield_priority_)
        {
            for (thread_id_t i = 0; i != thread_count; ++i)
            {
                if (t.yield_priority_[i])
                {
                    t.total_yield_priority_ -= t.yield_priority_[i];
                    t.yield_priority_[i] = 0;
                    this->unblock_thread(i);
                }
            }
        }

        (void)yield;
        //if (yield)
        //    yield_priority(1);
    }

protected:
    struct stree_node
    {
        unsigned    count_;
        unsigned    index_;
        sched_type  type_;
        unsigned    pad_;
    };

    typedef typename vector<stree_node>::type stree_t;
    stree_t         stree_;
    size_t          stree_depth_;

private:
    double          iteration_count_mean_;
    unsigned        iteration_count_probe_count_;

    derived_t& self()
    {
        return *static_cast<derived_t*>(this);
    }

    RL_NOCOPY(tree_search_scheduler);
};




template<thread_id_t thread_count>
class full_search_scheduler
    : public tree_search_scheduler<full_search_scheduler<thread_count>
        , tree_search_scheduler_thread_info<thread_count>, thread_count>
{
public:
    typedef tree_search_scheduler<full_search_scheduler<thread_count>
        , tree_search_scheduler_thread_info<thread_count>, thread_count> base_t;
    typedef typename base_t::thread_info_t thread_info_t;
    typedef typename base_t::shared_context_t shared_context_t;

    full_search_scheduler(test_params& params, shared_context_t& ctx, thread_id_t dynamic_thread_count)
        : base_t(params, ctx, dynamic_thread_count)
    {
    }

    bool can_switch(thread_info_t& /*t*/)
    {
        return true;
    }

    void on_switch(thread_info_t& /*t*/)
    {
    }

    double iteration_count_approx()
    {
        double total = 1;
        size_t const size = this->stree_.size();
        for (size_t i = 0; i != size; ++i)
        {
            total *= this->stree_[i].count_;
        }
        return total;
    }

    RL_NOCOPY(full_search_scheduler);
};


}

#endif

