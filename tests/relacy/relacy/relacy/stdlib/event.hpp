/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_EVENT_HPP
#define RL_EVENT_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "../base.hpp"
#include "../context_base.hpp"
#include "../sync_var.hpp"
#include "../waitset.hpp"
#include "semaphore.hpp"


namespace rl
{


struct event_data
{
    virtual void set(debug_info_param info) = 0;
    virtual void reset(debug_info_param info) = 0;
    virtual void pulse(debug_info_param info) = 0;
    virtual sema_wakeup_reason wait(bool try_wait, bool is_timed, debug_info_param info) = 0;
    virtual bool is_signaled(debug_info_param info) = 0;
    virtual void memory_acquire(debug_info_param info) = 0;
    virtual void* prepare_wait(debug_info_param info) = 0;
    virtual ~event_data() {} // just to calm down gcc
};




template<thread_id_t thread_count>
class event_data_impl : public event_data
{
public:
    event_data_impl(bool manual_reset, bool initial_state)
        : manual_reset_(manual_reset)
        , state_(initial_state)
    {
    }

    ~event_data_impl()
    {
        //!!! detect destuction with waiters
    }

private:
    signature<0xdada1234> sign_;
    bool const manual_reset_;
    bool state_;
    waitset<thread_count> ws_;
    sync_var<thread_count> sync_;

    struct state_event
    {
        enum type
        {
            type_set,
            type_reset,
            type_pulse,
        };

        event_data_impl* addr_;
        type type_;
        bool initial_state_;
        bool final_state_;
        thread_id_t unblocked_;

        void output(std::ostream& s) const
        {
            s << "<" << std::hex << addr_ << std::dec << "> event: ";
            if (type_set == type_)
                s << "set ";
            else if (type_reset == type_)
                s << "reset ";
            else
                s << "pulse ";
            s << "initial_state=" << initial_state_
                << " final_state=" << final_state_;
            if (type_reset != type_)
                s << " unblocked=" << unblocked_;
        }

    };

    virtual void set(debug_info_param info)
    {
        context& c = ctx();
        c.sched();
        sign_.check(info);

        bool initial_state = state_;
        thread_id_t unblocked = 0;

        if (state_)
        {
            //!!! probably can break if a thread waits in wfmo
            RL_VERIFY(false == ws_);
        }
        else
        {
            sync_.release(c.threadx_);
            state_ = true;

            if (manual_reset_)
            {
                unblocked = ws_.unpark_all(c, info);
            }
            else
            {
                if (ws_.unpark_one(c, info))
                    unblocked = 1;
            }
        }

        RL_HIST(state_event) {this, state_event::type_set, initial_state, state_, unblocked} RL_HIST_END();
    }

    virtual void reset(debug_info_param info)
    {
        context& c = ctx();
        c.sched();
        sign_.check(info);

        bool initial_state = state_;

        if (state_)
        {
            RL_VERIFY(false == ws_);
            sync_.release(c.threadx_);
            state_ = false;
        }

        RL_HIST(state_event) {this, state_event::type_reset, initial_state, state_, 0} RL_HIST_END();
    }

    virtual void pulse(debug_info_param info)
    {
        context& c = ctx();
        c.sched();
        sign_.check(info);

        //??? should I model nasty caveat described in MSDN
        thread_id_t unblocked = 0;

        if (state_)
        {
            //!!! probably can break if a thread waits in wfmo
            RL_VERIFY(false == ws_);
        }
        else
        {
            sync_.release(c.threadx_);
            state_ = true;
            unblocked = ws_.unpark_all(c, info);
            state_ = false;
        }

        RL_HIST(state_event) {this, state_event::type_pulse, state_, state_, unblocked} RL_HIST_END();
    }

    struct wait_event
    {
        event_data_impl* addr_;
        bool try_wait_;
        bool is_timed_;
        bool initial_state_;
        bool final_state_;
        sema_wakeup_reason reason_;

        void output(std::ostream& s) const
        {
            s << "<" << std::hex << addr_ << std::dec << "> event: ";
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

            s << "initial_state=" << initial_state_
                << " final_state=" << final_state_;
        }
    };

    virtual sema_wakeup_reason wait(bool try_wait, bool is_timed, debug_info_param info)
    {
        context& c = ctx();
        c.sched();
        sign_.check(info);

        bool initial_state = state_;
        sema_wakeup_reason reason = sema_wakeup_reason_success;

        for (;;)
        {
            if (state_)
            {
                if (manual_reset_)
                {
                    sync_.acquire(c.threadx_);
                }
                else
                {
                    state_ = false;
                    sync_.acq_rel(c.threadx_);
                }
                reason = sema_wakeup_reason_success;
                break;
            }

            if (try_wait)
            {
                sync_.acquire(c.threadx_);
                reason = sema_wakeup_reason_failed;
                break;
            }

            unpark_reason wr = ws_.park_current(c, is_timed, false, true, info);
            initial_state = state_;
            if (unpark_reason_timeout == wr)
            {
                sync_.acquire(c.threadx_);
                reason = sema_wakeup_reason_timeout;
                break;
            }
            else if (unpark_reason_normal == wr)
            {
                RL_VERIFY(state_ == true);
                if (manual_reset_)
                {
                    sync_.acquire(c.threadx_);
                }
                else
                {
                    state_ = false;
                    sync_.acq_rel(c.threadx_);
                }
                c.switch_back(info);
                reason = sema_wakeup_reason_success;
                break;
            }
            RL_VERIFY(false);
        }

        RL_HIST(wait_event) {this, try_wait, is_timed, initial_state, state_, reason} RL_HIST_END();
        return reason;
    }

    virtual bool is_signaled(debug_info_param info)
    {
        (void)info;
        return state_;
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

    RL_NOCOPY(event_data_impl);
};



class generic_event : public win_waitable_object
{
public:
    generic_event()
        : impl_()
    {
    }

    generic_event(generic_event const&)
        : impl_()
    {
    }

    generic_event& operator = (generic_event const&)
    {
        return *this;
    }

    void init(bool manual_reset, bool initial_state, debug_info_param info)
    {
        context& c = ctx();
        RL_ASSERT_IMPL(0 == impl_, test_result_double_initialization_of_event, "", info);
        sign_.check(info);
        impl_ = c.event_ctor(manual_reset, initial_state);
    }

    void deinit(debug_info_param info)
    {
        context& c = ctx();
        check(info);
        c.event_dtor(impl_);
        impl_ = 0;
    }

    void set(debug_info_param info)
    {
        check(info);
        impl_->set(info);
    }

    void reset(debug_info_param info)
    {
        check(info);
        impl_->reset(info);
    }

    void pulse(debug_info_param info)
    {
        check(info);
        impl_->pulse(info);
    }

    virtual sema_wakeup_reason wait(bool try_wait, bool is_timed, debug_info_param info)
    {
        check(info);
        return impl_->wait(try_wait, is_timed, info);
    }

    virtual bool signal(debug_info_param info)
    {
        set(info);
        return true;
    }

private:
    event_data* impl_;
    signature<0x3390eeaa> sign_;

    event_data* check(debug_info_param info)
    {
        RL_ASSERT_IMPL(impl_, test_result_usage_of_non_initialized_event, "", info);
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


}

#endif
