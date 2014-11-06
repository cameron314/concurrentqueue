/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_SCHEDULER_HPP
#define RL_SCHEDULER_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "context_base.hpp"


namespace rl
{


enum thread_state_e
{
    thread_state_running,
    thread_state_blocked,
    thread_state_finished,
};

enum thread_finish_result
{
    thread_finish_result_normal,
    thread_finish_result_last,
    thread_finish_result_deadlock,
};



struct scheduler_thread_info
{
    thread_id_t             index_;
    unsigned                block_count_;
    thread_state_e          state_;

    void reset(test_params& /*params*/)
    {
        block_count_ = 0;
        state_ = thread_state_running;
    }
};




template<typename derived_t, typename thread_info_type, thread_id_t thread_count>
class scheduler : nocopy<>
{
public:
    typedef thread_info_type                    thread_info_t;

    struct shared_context_t
    {
        typedef typename derived_t::task_t      task_t;
        //CRITICAL_SECTION                        guard_;
        queue<task_t>                           queue_;
    };

    scheduler(test_params& params, shared_context_t& ctx, thread_id_t dynamic_thread_count)
        : params_(params)
        , ctx_(ctx)
        , total_dynamic_threads_(dynamic_thread_count)
        , iter_()
        , thread_()
    {
        for (thread_id_t i = 0; i != thread_count; ++i)
        {
            threads_[i].index_ = i;
        }
    }

    thread_id_t iteration_begin(iteration_t iter)
    {
        iter_ = iter;
        running_threads_count = thread_count;
        finished_thread_count_ = 0;
        timed_thread_count_ = 0;
        spurious_thread_count_ = 0;
        dynamic_thread_count_ = 0;

        for (thread_id_t i = 0; i != thread_count; ++i)
        {
            running_threads.push_back(i);
            threads_[i].reset(params_);
        }

        for (thread_id_t i = thread_count - total_dynamic_threads_; i != thread_count; ++i)
        {
            dynamic_threads_[dynamic_thread_count_++] = &threads_[i];
            block_thread(i, false);
        }

        thread_id_t const th = self().iteration_begin_impl();
    
        thread_ = &threads_[th];

        return th;
    }

    bool iteration_end()
    {
        bool const finish = self().iteration_end_impl();

        thread_ = 0;

        return finish;
    }

    thread_id_t schedule(unpark_reason& reason, unsigned yield)
    {
        thread_id_t const th = self().schedule_impl(reason, yield);

        RL_VERIFY(threads_[th].state_ == thread_state_running);
        thread_ = &threads_[th];

        return th;
    }

    RL_INLINE
    unsigned rand(unsigned limit, sched_type t)
    {
        RL_VERIFY(limit);
        return self().rand_impl(limit, t);
    }

    iteration_t iteration_count()
    {
        return self().iteration_count_impl();
    }

    bool park_current_thread(bool is_timed, bool allow_spurious_wakeup)
    {
        if (is_timed)
        {
            timed_threads_[timed_thread_count_++] = thread_;
            RL_VERIFY(timed_thread_count_ <= thread_count);
        }

        if (allow_spurious_wakeup)
        {
            spurious_threads_[spurious_thread_count_++] = thread_;
            RL_VERIFY(spurious_thread_count_ <= thread_count);
        }

        block_thread(thread_->index_, true);

        return is_deadlock() ? false : true;
    }

    void unpark_thread(thread_id_t th, bool do_switch = false)
    {
        (void)do_switch;
        unblock_thread(th);

        thread_info_t& t = threads_[th];

        //!!! store flag as to whether thread is spurious blocked in thread object
        // (to eliminate iteration over all threads)
        for (thread_id_t i = 0; i != spurious_thread_count_; ++i)
        {
            if (spurious_threads_[i] == &t)
            {
                for (thread_id_t j = i + 1; j != spurious_thread_count_; ++j)
                    spurious_threads_[j - 1] = spurious_threads_[j];
                spurious_thread_count_ -= 1;
                break;
            }
        }

        //!!! store flag as to whether thread is spurious blocked in thread object
        for (thread_id_t i = 0; i != timed_thread_count_; ++i)
        {
            if (timed_threads_[i] == &t)
            {
                for (thread_id_t j = i + 1; j != timed_thread_count_; ++j)
                    timed_threads_[j - 1] = timed_threads_[j];
                timed_thread_count_ -= 1;
                break;
            }
        }
    }

    thread_finish_result thread_finished()
    {
        RL_VERIFY(thread_->state_ == thread_state_running);
        block_thread(thread_->index_, false);
        thread_->state_ = thread_state_finished;
        finished_thread_count_ += 1;
        self().thread_finished_impl();
retry:
        if (finished_thread_count_ == thread_count)
        {
            return thread_finish_result_last;
        }
        else if (is_deadlock())
        {
            if (dynamic_thread_count_)
            {
                while (dynamic_thread_count_)
                {
                    thread_info_t* th = dynamic_threads_[--dynamic_thread_count_];
                    unblock_thread(th->index_);
                }
                goto retry;
            }
            return thread_finish_result_deadlock;
        }
        else
        {
            return thread_finish_result_normal;
        }
    }

    thread_id_t create_thread()
    {
        RL_VERIFY(dynamic_thread_count_);
        thread_info_t* th = dynamic_threads_[--dynamic_thread_count_];
        unblock_thread(th->index_);
        return th->index_;
    }

    void get_state(std::ostream& ss)
    {
        self().get_state_impl(ss);
    }

    void set_state(std::istream& ss)
    {
        self().set_state_impl(ss);
    }

protected:
    test_params&                    params_;
    shared_context_t&               ctx_;
    thread_id_t const               total_dynamic_threads_;
    iteration_t                     iter_;

    aligned<thread_info_t>          threads_ [thread_count];
    thread_info_t*                  thread_;

    vector<thread_id_t>::type       running_threads;
    thread_id_t                     running_threads_count;
    thread_id_t                     finished_thread_count_;

    //!!! doesn't timed/spurious waits must belong to full scheduler?
    // hyphotesis: random scheduler can ignore timed/spurious waits
    // (however must detect deadlock with spurious threads)
    thread_info_t*                  timed_threads_ [thread_count];
    thread_id_t                     timed_thread_count_;

    thread_info_t*                  spurious_threads_ [thread_count];
    thread_id_t                     spurious_thread_count_;

    thread_info_t*                  dynamic_threads_ [thread_count];
    thread_id_t                     dynamic_thread_count_;

    void block_thread(thread_id_t th, bool yield)
    {
        RL_VERIFY(th < thread_count);
        thread_info_t& t = threads_[th];
        RL_VERIFY(t.state_ != thread_state_finished);
        if (t.block_count_++)
            return;

        for (thread_id_t i = 0; i != running_threads_count; ++i)
        {
            if (running_threads[i] == th)
            {
                running_threads.erase(running_threads.begin() + i);
                running_threads_count -= 1;
                t.state_ = thread_state_blocked;
                self().on_thread_block(th, yield);
                return;
            }
        }
        RL_VERIFY(false);
    }

    bool unblock_thread(thread_id_t th)
    {
        RL_VERIFY(th < thread_count);
        thread_info_t& t = threads_[th];
        RL_VERIFY(t.state_ == thread_state_blocked);
        if (--t.block_count_)
            return false;

        running_threads.push_back(th);
        running_threads_count += 1;
        t.state_ = thread_state_running;
        return true;
    }

private:
    derived_t& self()
    {
        return *static_cast<derived_t*>(this);
    }

    bool is_deadlock()
    {
        if ((0 == running_threads_count) && (0 == timed_thread_count_))
        {
            self().purge_blocked_threads();
            if ((0 == running_threads_count) && (0 == timed_thread_count_))
                return true;
        }
        return false;
    }

    void thread_finished_impl()
    {
    }

    void purge_blocked_threads()
    {
    }
};


}

#endif
