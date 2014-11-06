/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_SEMAPHORE_HPP
#define RL_SEMAPHORE_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "../base.hpp"
#include "../context_base.hpp"
#include "../sync_var.hpp"
#include "../waitset.hpp"
#include "../signature.hpp"


namespace rl
{

enum sema_wakeup_reason
{
    sema_wakeup_reason_success,
    sema_wakeup_reason_failed,
    sema_wakeup_reason_timeout,
    sema_wakeup_reason_spurious,
};

struct win_object
{
    virtual void deinit(debug_info_param info) = 0;
    virtual ~win_object() {}
};

struct win_waitable_object : win_object
{
    virtual sema_wakeup_reason wait(bool try_wait, bool is_timed, debug_info_param info) = 0;
    virtual bool signal(debug_info_param info) = 0;

    virtual bool is_signaled(debug_info_param info) = 0;
    virtual void memory_acquire(debug_info_param info) = 0;
    virtual void* prepare_wait(debug_info_param info) = 0;
};




struct sema_data
{
    virtual sema_wakeup_reason wait(bool try_wait, bool is_timed, debug_info_param info) = 0;
    virtual bool post(unsigned count, unsigned& prev_count, debug_info_param info) = 0;
    virtual int get_value(debug_info_param info) = 0;
    virtual bool is_signaled(debug_info_param info) = 0;
    virtual void memory_acquire(debug_info_param info) = 0;
    virtual void* prepare_wait(debug_info_param info) = 0;
    virtual ~sema_data() {} // just to calm down gcc
};




template<thread_id_t thread_count>
class sema_data_impl : public sema_data
{
public:
    sema_data_impl(bool spurious_wakeups, unsigned initial_count, unsigned max_count)
        : spurious_wakeups_(spurious_wakeups)
        , count_(initial_count)
        , max_count_(max_count)
    {
        RL_VERIFY(max_count <= INT_MAX);
    }

    ~sema_data_impl()
    {
        //!!! detect destruction with waiters
    }

    struct wait_event
    {
        sema_data_impl*         addr_;
        bool                    try_wait_;
        bool                    is_timed_;
        unsigned                count_;
        sema_wakeup_reason      reason_;

        void output(std::ostream& s) const
        {
            s << "<" << std::hex << addr_ << std::dec << "> semaphore: ";
            if (try_wait_)
                s << "try_wait ";
            else if (is_timed_)
                s << "timed wait ";
            else
                s << "wait ";

            if (reason_ == sema_wakeup_reason_success)
                s << "succeeded ";
            else if (reason_ == sema_wakeup_reason_failed)
                s << "failed ";
            else if (reason_ == sema_wakeup_reason_timeout)
                s << "timed out ";
            else if (reason_ == sema_wakeup_reason_spurious)
                s << "spuriously failed ";

            s << "new_count=" << count_;
        }
    };

    struct post_event
    {
        sema_data_impl*         addr_;
        unsigned                value_;
        unsigned                count_;
        bool                    result_;
        thread_id_t             unblocked_;

        void output(std::ostream& s) const
        {
            s << "<" << std::hex << addr_ << std::dec << "> semaphore: ";
            if (result_)
                s << "post ";
            else
                s << "post FAILED ";

            s << "value=" << value_;
            s << " new_count=" << count_;
            s << " unblocked=" << unblocked_;
        }
    };

    struct get_value_event
    {
        sema_data_impl* addr_;
        unsigned count_;

        void output(std::ostream& s) const
        {
            s << "<" << std::hex << addr_ << std::dec << "> semaphore: ";
            s << "get_value count=" << count_;
        }
    };

    virtual sema_wakeup_reason wait(bool try_wait,
                                    bool is_timed,
                                    debug_info_param info)
    {
        context& c = ctx();
        c.sched();
        sign_.check(info);

        sema_wakeup_reason reason = sema_wakeup_reason_success;
        for (;;)
        {
            if (count_)
            {
                count_ -= 1;
                sync_.acq_rel(c.threadx_);
                reason = sema_wakeup_reason_success;
                break;
            }

            if (try_wait)
            {
                sync_.acquire(c.threadx_);
                reason = sema_wakeup_reason_failed;
                break;
            }

            unpark_reason wr = ws_.park_current(c, is_timed, spurious_wakeups_, true, info);
            if (unpark_reason_timeout == wr)
            {
                RL_VERIFY(is_timed);
                sync_.acquire(c.threadx_);
                reason = sema_wakeup_reason_timeout;
                break;
            }
            else if (unpark_reason_spurious == wr)
            {
                RL_VERIFY(spurious_wakeups_);
                sync_.acquire(c.threadx_);
                reason = sema_wakeup_reason_spurious;
                break;
            }
            else if (unpark_reason_normal == wr)
            {
                RL_VERIFY(count_ > 0);
                count_ -= 1;
                sync_.acq_rel(c.threadx_);
                c.switch_back(info);
                reason = sema_wakeup_reason_success;
                break;
            }
            RL_VERIFY(false);
        }

        RL_HIST(wait_event) {this, try_wait, is_timed, count_, reason} RL_HIST_END();
        return reason;
    }

    virtual bool post(unsigned count, unsigned& prev_count, debug_info_param info)
    {
        context& c = ctx();
        c.sched();
        sign_.check(info);

        bool result = false;
        prev_count = count_;
        thread_id_t unblocked = 0;
        if (false == (count >= INT_MAX || count + count_ > max_count_))
        {
            result = true;
            count_ += count;
            sync_.acq_rel(c.threadx_);
            for (unsigned i = 0; i != count; ++i)
            {
                if (false == ws_.unpark_one(c, info))
                    break;
                unblocked += 1;
            }
        }
        else
        {
            sync_.acquire(c.threadx_);
        }
        RL_HIST(post_event) {this, count, count_, result, unblocked} RL_HIST_END();
        return result;
    }

    virtual int get_value(debug_info_param info)
    {
        context& c = ctx();
        c.sched();
        sign_.check(info);
        
        RL_VERIFY(count_ <= INT_MAX);
        int result = (int)count_ - ws_.size();
        sync_.acquire(c.threadx_);

        RL_HIST(get_value_event) {this, (unsigned)result} RL_HIST_END();
        return result;
    }

private:
    signature<0xaabb6634> sign_;
    bool const spurious_wakeups_;
    unsigned count_;
    unsigned const max_count_;
    waitset<thread_count> ws_;
    sync_var<thread_count> sync_;

    virtual bool is_signaled(debug_info_param info)
    {
        (void)info;
        return count_ > 0;
    }

    virtual void memory_acquire(debug_info_param info)
    {
        (void)info;
        sync_.acquire(ctx().threadx_);
    }

    virtual void* prepare_wait(debug_info_param info)
    {
        (void)info;
        return &ws_;
    }

    RL_NOCOPY(sema_data_impl);
};



template<typename tag_t>
class semaphore : public win_waitable_object
{
public:
    semaphore()
        : impl_()
    {
    }

    semaphore(semaphore const&)
        : impl_()
    {
    }

    semaphore& operator = (semaphore const&)
    {
        return *this;
    }

    void init(bool spurious_wakeups, unsigned initial_count, unsigned max_count, debug_info_param info)
    {
        context& c = ctx();
        RL_ASSERT_IMPL(0 == impl_, test_result_double_initialization_of_semaphore, "", info);
        sign_.check(info);
        impl_ = c.sema_ctor(spurious_wakeups, initial_count, max_count);
    }

    void deinit(debug_info_param info)
    {
        context& c = ctx();
        check(info);
        c.sema_dtor(impl_);
        impl_ = 0;
    }

    virtual sema_wakeup_reason wait(bool try_wait, bool is_timed, debug_info_param info)
    {
        check(info);
        return impl_->wait(try_wait, is_timed, info);
    }

    virtual bool signal(debug_info_param info)
    {
        unsigned prev_count = 0;
        return post(1, prev_count, info);
    }

    bool post(unsigned count, unsigned& prev_count, debug_info_param info)
    {
        check(info);
        return impl_->post(count, prev_count, info);
    }

    int get_value(debug_info_param info)
    {
        check(info);
        return impl_->get_value(info);
    }

private:
    sema_data* impl_;
    signature<0x228855dd> sign_;

    sema_data* check(debug_info_param info)
    {
        RL_ASSERT_IMPL(impl_, test_result_usage_of_non_initialized_semaphore, "", info);
        sign_.check(info);
        return impl_;
    }

    virtual bool is_signaled(debug_info_param info)
    {
        return check(info)->is_signaled(info);
    }

    virtual void memory_acquire(debug_info_param info)
    {
        check(info)->memory_acquire(info);
    }

    virtual void* prepare_wait(debug_info_param info)
    {
        return check(info)->prepare_wait(info);
    }
};



struct wfmo_event
{
    unsigned long               count_;
    bool                        wait_all_;
    bool                        try_wait_;
    bool                        is_timed_;
    sema_wakeup_reason          result_;
    size_t                      signaled_;

    void output(std::ostream& s) const
    {
        s   << "WFMO: "
            << "count=" << count_
            << ", wait_all=" << wait_all_
            << ", try_wait=" << try_wait_
            << ", is_timed=" << is_timed_
            << ", result=";
        if (sema_wakeup_reason_success == result_)
        {
            s << "success";
            if (wait_all_ == false)
                s << ", object=" << signaled_;
        }
        else
        {
            s << "timeout";
        }
    }
};

size_t const wfmo_max_objects = 32;

inline sema_wakeup_reason wait_for_multiple_objects(
    size_t& signaled,
    size_t count,
    win_waitable_object** wo,
    bool wait_all,
    bool try_wait,
    bool is_timed,
    debug_info_param info)
{
    context& c = ctx();
    c.sched();

    RL_VERIFY(count <= wfmo_max_objects);
    void* ws [wfmo_max_objects];

    sema_wakeup_reason result = sema_wakeup_reason_failed;
    signaled = 0;

    if (wait_all)
    {
        for (;;)
        {
            unsigned long i = 0;
            for (i = 0; i != count; ++i)
            {
                if (false == wo[i]->is_signaled(info))
                    break;
            }
            if (i == count)
            {
                preemption_disabler pd (c);
                for (i = 0; i != count; ++i)
                {
                    sema_wakeup_reason r = wo[i]->wait(true, false, info);
                    RL_VERIFY(r == sema_wakeup_reason_success);
                    (void)r;
                }
                result = sema_wakeup_reason_success;
                break;
            }
            else if (try_wait)
            {
                for (i = 0; i != count; ++i)
                    wo[i]->memory_acquire(info);
                result = sema_wakeup_reason_timeout;
                break;
            }
            else
            {
                for (i = 0; i != count; ++i)
                {
                    ws[i] = wo[i]->prepare_wait(info);
                }
                unpark_reason reason = c.wfmo_park(ws, wo, (unsigned)count, !!wait_all, is_timed, info);
                RL_VERIFY(unpark_reason_spurious != reason);
                if (unpark_reason_timeout == reason)
                {
                    for (i = 0; i != count; ++i)
                        wo[i]->memory_acquire(info);
                    result = sema_wakeup_reason_timeout;
                    break;
                }
                else if (unpark_reason_normal == reason)
                {
                    {
                        preemption_disabler pd (c);
                        for (unsigned long i = 0; i != count; ++i)
                        {
                            RL_VERIFY(wo[i]->is_signaled(info));
                            sema_wakeup_reason r = wo[i]->wait(true, false, info);
                            RL_VERIFY(r == sema_wakeup_reason_success);
                            (void)r;
                        }
                    }
                    c.switch_back(info);
                    result = sema_wakeup_reason_success;
                    break;
                }
                RL_VERIFY(false);
            }
        }
    }
    else
    {
        for (;;)
        {
            unsigned long i = 0;
            for (i = 0; i != count; ++i)
            {
                if (true == wo[i]->is_signaled(info))
                    break;
            }
            if (i != count)
            {
                preemption_disabler pd (c);
                sema_wakeup_reason r = wo[i]->wait(true, false, info);
                RL_VERIFY(r == sema_wakeup_reason_success);
                (void)r;
                signaled = i;
                result = sema_wakeup_reason_success;
                break;
            }
            else if (try_wait)
            {
                for (i = 0; i != count; ++i)
                    wo[i]->memory_acquire(info);
                result = sema_wakeup_reason_timeout;
                break;
            }
            else
            {
                for (i = 0; i != count; ++i)
                {
                    ws[i] = wo[i]->prepare_wait(info);
                }
                unpark_reason reason = c.wfmo_park(ws, wo, (unsigned)count, !!wait_all, is_timed, info);
                RL_VERIFY(unpark_reason_spurious != reason);
                if (unpark_reason_timeout == reason)
                {
                    for (i = 0; i != count; ++i)
                        wo[i]->memory_acquire(info);
                    result = sema_wakeup_reason_timeout;
                    break;
                }
                else if (unpark_reason_normal == reason)
                {
                    unsigned long i = 0;
                    for (i = 0; i != count; ++i)
                    {
                        if (true == wo[i]->is_signaled(info))
                            break;
                    }
                    RL_VERIFY(i != count);
                    {
                        preemption_disabler pd (c);
                        sema_wakeup_reason r = wo[i]->wait(true, false, info);
                        RL_VERIFY(r == sema_wakeup_reason_success);
                        (void)r;
                    }
                    c.switch_back(info);
                    signaled = i;
                    result = sema_wakeup_reason_success;
                    break;
                }
                RL_VERIFY(false);
            }
        }
    }
    
    RL_HIST(wfmo_event) {(unsigned)count, wait_all, try_wait, is_timed, result, signaled} RL_HIST_END();
    return result;
}


}


#endif

