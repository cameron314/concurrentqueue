/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_CONDITION_VARIABLE_HPP
#define RL_CONDITION_VARIABLE_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "../base.hpp"
#include "../context_base.hpp"
#include "../waitset.hpp"
#include "../signature.hpp"


namespace rl
{

struct mutex_wrapper
{
    virtual void lock(debug_info_param info) const = 0;
    virtual void unlock(debug_info_param info) const = 0;
    virtual ~mutex_wrapper() {}
};

template<typename mutex_t>
class mutex_wrapper_impl : public mutex_wrapper
{
public:
    mutex_wrapper_impl(mutex_t& m)
        : m_(m)
    {
    }

private:
    mutex_t& m_;

    virtual void lock(debug_info_param info) const
    {
        m_.lock(info);
    }

    virtual void unlock(debug_info_param info) const
    {
        m_.unlock(info);
    }

    RL_NOCOPY(mutex_wrapper_impl);
};

struct pred_wrapper
{
    virtual bool exec() const = 0;
    virtual ~pred_wrapper() {}
};

template<typename pred_t>
class pred_wrapper_impl : public pred_wrapper
{
public:
    pred_wrapper_impl(pred_t p)
        : p_(p)
    {
    }

private:
    mutable pred_t p_;

    virtual bool exec() const
    {
        return p_();
    }

    RL_NOCOPY(pred_wrapper_impl);
};


struct condvar_data
{
    virtual void notify_one(debug_info_param info) = 0;
    virtual void notify_all(debug_info_param info) = 0;
    virtual sema_wakeup_reason wait(mutex_wrapper const& lock, bool is_timed, debug_info_param info) = 0;
    virtual bool wait(mutex_wrapper const& lock, pred_wrapper const& pred, bool is_timed, debug_info_param info) = 0;
    virtual ~condvar_data() {} // just to calm down gcc
};

template<thread_id_t thread_count>
class condvar_data_impl : public condvar_data
{
public:
    condvar_data_impl(bool allow_spurious_wakeups)
    {
        spurious_wakeup_limit_ = 0;
        if (allow_spurious_wakeups && ctx().is_random_sched())
            spurious_wakeup_limit_ = 10;
    }

    ~condvar_data_impl()
    {
        //!!! detect destoy when there are blocked threads
    }

private:
    waitset<thread_count>           ws_;
    signature<0xc0ffe3ad>           sign_;
    int                             spurious_wakeup_limit_;

    struct event_t
    {
        enum type_e
        {
            type_notify_one,
            type_notify_all,
            type_wait_enter,
            type_wait_exit,
            type_wait_pred_enter,
            type_wait_pred_exit,
        };

        condvar_data_impl const*    var_addr_;
        type_e                      type_;
        thread_id_t                 thread_count_;
        unpark_reason               reason_;

        void output(std::ostream& s) const
        {
            s << "<" << std::hex << var_addr_ << std::dec << "> cond_var: ";
            switch (type_)
            {
            case type_notify_one:
                s << "notify one total_blocked=" << thread_count_ << " unblocked=" << (thread_count_ ? 1 : 0);
                break;
            case type_notify_all:
                s << "notify all unblocked=" << thread_count_;
                break;
            case type_wait_enter: s << "wait enter"; break;
            case type_wait_exit:
                s << "wait exit";
                if (unpark_reason_normal == reason_)
                    s << " due to notified";
                else if (unpark_reason_timeout == reason_)
                    s << " due to timeout";
                else if (unpark_reason_spurious == reason_)
                    s << " spuriously";
                break;
            case type_wait_pred_enter: s << "wait pred enter"; break;
            case type_wait_pred_exit: s << "wait pred exit"; break;
            }
        }
    };

    virtual void notify_one(debug_info_param info)
    {
        context& c = ctx();
        //??? do I need this scheduler call?
        c.sched();
        sign_.check(info);
        RL_HIST(event_t) {this, event_t::type_notify_one, ws_.size()} RL_HIST_END();
        ws_.unpark_one(c, info);
    }

    virtual void notify_all(debug_info_param info)
    {
        context& c = ctx();
        //??? do I need this scheduler call?
        c.sched();
        sign_.check(info);
        RL_HIST(event_t) {this, event_t::type_notify_all, ws_.size()} RL_HIST_END();
        ws_.unpark_all(c, info);
    }

    virtual sema_wakeup_reason wait(mutex_wrapper const& lock, bool is_timed, debug_info_param info)
    {
        //!!! detect whether mutex is the same
        context& c = ctx();
        sign_.check(info);
        RL_HIST(event_t) {this, event_t::type_wait_enter} RL_HIST_END();
        lock.unlock(info);
        sign_.check(info);
        bool allow_spurious_wakeup = (spurious_wakeup_limit_ > 0);
        unpark_reason reason = ws_.park_current(c, is_timed, allow_spurious_wakeup, false, info);
        if (reason == unpark_reason_spurious)
            spurious_wakeup_limit_ -= 1;
        RL_HIST(event_t) {this, event_t::type_wait_exit, 0, reason} RL_HIST_END();
        lock.lock(info);
        sign_.check(info);
        if (reason == unpark_reason_normal)
            return sema_wakeup_reason_success;
        else if (reason == unpark_reason_spurious)
            return sema_wakeup_reason_spurious;
        else //if (reason == unpark_reason_timeout)
            return sema_wakeup_reason_timeout;
    }

    virtual bool wait(mutex_wrapper const& lock, pred_wrapper const& pred, bool is_timed, debug_info_param info)
    {
        context& c = ctx();
        sign_.check(info);
        RL_HIST(event_t) {this, event_t::type_wait_pred_enter} RL_HIST_END();
        while (!pred.exec())
        {
            sema_wakeup_reason reason = wait(lock, is_timed, info);
            if (reason == sema_wakeup_reason_timeout)
            {
                RL_HIST(event_t) {this, event_t::type_wait_pred_exit} RL_HIST_END();
                return pred.exec();
            }
        }
        RL_HIST(event_t) {this, event_t::type_wait_pred_exit} RL_HIST_END();
        return true;
    }
};


template<typename tag_t>
class condvar
{
public:
    condvar()
        : impl_()
    {
    }

    condvar(condvar const&)
        : impl_()
    {
    }

    condvar& operator = (condvar const&)
    {
        return *this;
    }

    ~condvar()
    {
    }

    void init(bool allow_spurious_wakeups, debug_info_param info)
    {
        context& c = ctx();
        RL_ASSERT_IMPL(0 == impl_, test_result_double_initialization_of_condvar, "", info);
        sign_.check(info);
        impl_ = c.condvar_ctor(allow_spurious_wakeups);
    }

    void deinit(debug_info_param info)
    {
        context& c = ctx();
        check(info);
        c.condvar_dtor(impl_);
        impl_ = 0;
    }

    void notify_one(debug_info_param info)
    {
        check(info);
        impl_->notify_one(info);
    }

    void notify_all(debug_info_param info)
    {
        check(info);
        impl_->notify_all(info);
    }

    template<typename lock_t>
    sema_wakeup_reason wait(lock_t& lock, bool is_timed, debug_info_param info)
    {
        check(info);
        mutex_wrapper_impl<lock_t> w (lock);
        return impl_->wait(w, is_timed, info);
    }

    template<typename lock_t, typename pred_t>
    bool wait(mutex_wrapper const& lock, pred_wrapper const& pred, bool is_timed, debug_info_param info)
    {
        check(info);
        return impl_->wait(mutex_wrapper_impl<lock_t>(lock), pred_wrapper_impl<pred_t>(pred), is_timed, info);
    }

private:
    condvar_data* impl_;
    signature<0xbadc0ffe> sign_;

    void check(debug_info_param info)
    {
        RL_ASSERT_IMPL(impl_, test_result_usage_of_non_initialized_condvar, "", info);
        sign_.check(info);
    }
};



template<typename tag_t>
class condition_variable_std : condvar<tag_t>
{
public:
    condition_variable_std()
    {
        condvar<tag_t>::init(true, $);
    }

    ~condition_variable_std()
    {
        condvar<tag_t>::deinit($);
    }

    void notify_one(debug_info_param info)
    {
        condvar<tag_t>::notify_one(info);
    }

    void notify_all(debug_info_param info)
    {
        condvar<tag_t>::notify_all(info);
    }

    template<typename lock_t>
    void wait(lock_t& lock, debug_info_param info)
    {
        condvar<tag_t>::wait(lock, false, info);
    }

    template<typename lock_t, typename pred_t>
    void wait(lock_t& lock, pred_t pred, debug_info_param info)
    {
        condvar<tag_t>::wait(lock, pred, false, info);
    }

    template<typename lock_t, typename abs_time_t>
    bool wait_until(lock_t& lock, abs_time_t const&, debug_info_param info)
    {
        return condvar<tag_t>::wait(lock, true, info);
    }

    template<typename lock_t, typename abs_time_t, typename pred_t>
    bool wait_until(lock_t& lock, abs_time_t const&, pred_t pred, debug_info_param info)
    {
        return condvar<tag_t>::wait(lock, pred, true, info);
    }
    
    template<typename lock_t, typename rel_time_t>
    bool wait_for(lock_t& lock, rel_time_t const&, debug_info_param info)
    {
        sema_wakeup_reason reason = condvar<tag_t>::wait(lock, true, info);
        return reason == sema_wakeup_reason_success;
    }

    template<typename lock_t, typename rel_time_t, typename pred_t>
    bool wait_for(lock_t& lock, rel_time_t const&, pred_t pred, debug_info_param info)
    {
        return condvar<tag_t>::wait(lock, pred, true, info);
    }

    RL_NOCOPY(condition_variable_std);
};


struct condvar_tag_std;
typedef condition_variable_std<condvar_tag_std> condition_variable;
struct condvar_tag_std_any;
typedef condition_variable_std<condvar_tag_std_any> condition_variable_any;

}

#endif
