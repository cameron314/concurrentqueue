/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_MUTEX_HPP
#define RL_MUTEX_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "../base.hpp"
#include "../context.hpp"
#include "../thread.hpp"
#include "../atomic.hpp"
#include "../waitset.hpp"
#include "../signature.hpp"
#include "../sync_var.hpp"
#include "../foreach.hpp"
#include "semaphore.hpp"



namespace rl
{

struct generic_mutex_data : nocopy<>
{
    virtual bool lock_exclusive(bool is_timed, debug_info_param info) = 0;
    virtual bool try_lock_exclusive(debug_info_param info) = 0;
    virtual void unlock_exclusive(debug_info_param info) = 0;
    virtual void lock_shared(debug_info_param info) = 0;
    virtual bool try_lock_shared(debug_info_param info) = 0;
    virtual void unlock_shared(debug_info_param info) = 0;
    virtual void unlock_exclusive_or_shared(debug_info_param info) = 0;
    virtual bool is_signaled(debug_info_param info) = 0;
    virtual void memory_acquire(debug_info_param info) = 0;
    virtual void* prepare_wait(debug_info_param info) = 0;
    virtual ~generic_mutex_data() {} // just to calm down gcc
};


template<thread_id_t thread_count>
class generic_mutex_data_impl : public generic_mutex_data
{
public:
    struct event_t
    {
        enum type_e
        {
            type_lock,
            type_unlock,
            type_recursive_lock,
            type_recursive_unlock,
            type_failed_try_lock,
            type_spuriously_failed_try_lock,
            type_lock_shared,
            type_unlock_shared,
            type_recursive_lock_shared,
            type_recursive_unlock_shared,
            type_failed_try_lock_shared,
            type_spuriously_failed_try_lock_shared,
            type_wait,
            type_destroying_owned_mutex,
        };

        generic_mutex_data_impl const* var_addr_;
        type_e type_;

        void output(std::ostream& s) const
        {
            s << "<" << std::hex << var_addr_ << std::dec << "> mutex: ";
            switch (type_)
            {
            case type_lock: s << "exclusive lock"; break;
            case type_unlock: s << "exclusive unlock"; break;
            case type_recursive_lock: s << "recursive exclusive lock"; break;
            case type_recursive_unlock: s << "recursive exclusive unlock"; break;
            case type_failed_try_lock: s << "failed exclusive try lock"; break;
            case type_spuriously_failed_try_lock: s << "spuriously failed exclusive try lock"; break;
            case type_lock_shared: s << "shared lock"; break;
            case type_unlock_shared: s << "shared unlock"; break;
            case type_recursive_lock_shared: s << "recursive shared lock"; break;
            case type_recursive_unlock_shared: s << "recursive shared unlock"; break;
            case type_failed_try_lock_shared: s << "failed shared try lock"; break;
            case type_spuriously_failed_try_lock_shared: s << "spuriously failed shared try lock"; break;
            case type_wait: s << "blocking"; break;
            case type_destroying_owned_mutex: s << "destroying owned mutex"; break;
            }
        }
    };

    generic_mutex_data_impl(bool is_rw, bool is_exclusive_recursive, bool is_shared_recursive, bool failing_try_lock)
        : is_rw_(is_rw)
        , is_exclusive_recursive_(is_exclusive_recursive)
        , is_shared_recursive_(is_shared_recursive)
        , failing_try_lock_(failing_try_lock)
        , exclusive_owner_(state_free)
        , exclusive_recursion_count_(0)
        , shared_lock_count_(0)
        , try_lock_failed_()
    {
        context& c = ctx();
        (void)c;
        RL_VERIFY(false == c.invariant_executing);
        foreach<thread_count>(shared_owner_, &assign_zero);
    }

    ~generic_mutex_data_impl()
    {
        context& c = ctx();
        RL_VERIFY(false == c.invariant_executing);
        if (exclusive_owner_ != state_free
            || exclusive_waitset_
            || shared_waitset_)
        {
            debug_info info = $;
            RL_HIST(event_t) {this, event_t::type_destroying_owned_mutex} RL_HIST_END();
            RL_ASSERT_IMPL(false, test_result_destroying_owned_mutex, "", $);
        }
    }

    virtual bool lock_exclusive(bool is_timed, debug_info_param info)
    {
        context& c = ctx();
        c.sched();
        sign_.check(info);
        RL_VERIFY(false == c.invariant_executing);

        thread_id_t const my_id = c.threadx_->index_;

        if (exclusive_owner_ == state_shared && shared_owner_[my_id])
        {
            RL_HIST(event_t) {this, event_t::type_lock} RL_HIST_END();
            RL_ASSERT_IMPL(false, test_result_mutex_read_to_write_upgrade, "", info);
        }

        if (exclusive_owner_ == my_id)
        {
            RL_HIST(event_t) {this, event_t::type_recursive_lock} RL_HIST_END();
            if (is_exclusive_recursive_)
            {
                exclusive_recursion_count_ += 1;
                return true;
            }
            else
            {
                RL_ASSERT_IMPL(false, test_result_recursion_on_nonrecursive_mutex, "", info);
            }
        }

        for (;;)
        {
            if (exclusive_owner_ == state_free)
            {
                RL_VERIFY(exclusive_recursion_count_ == 0);
                //!!! in some implementation here must be acq_rel
                sync_.acquire(c.threadx_);
                exclusive_recursion_count_ = 1;
                exclusive_owner_ = my_id;
                RL_HIST(event_t) {this, event_t::type_lock} RL_HIST_END();
                return true;
            }
            else
            {
                RL_VERIFY(my_id != exclusive_owner_);
                RL_HIST(event_t) {this, event_t::type_wait} RL_HIST_END();
                unpark_reason reason = exclusive_waitset_.park_current(c, is_timed, false, false, info);
                RL_VERIFY(reason != unpark_reason_spurious);
                if (reason == unpark_reason_timeout)
                {
                    sync_.acquire(c.threadx_);
                    return false;
                }
            }

            //??? c.sched();
            //sign_.check(info);
        }
    }

    virtual bool try_lock_exclusive(debug_info_param info)
    {
        context& c = ctx();
        c.sched();
        sign_.check(info);
        RL_VERIFY(false == c.invariant_executing);

        thread_id_t const my_id = c.threadx_->index_;

        if (exclusive_owner_ == state_shared && shared_owner_[my_id])
        {
            RL_HIST(event_t) {this, event_t::type_lock} RL_HIST_END();
            RL_ASSERT_IMPL(false, test_result_mutex_read_to_write_upgrade, "", info);
        }

        if (exclusive_owner_ == my_id)
        {
            RL_HIST(event_t) {this, event_t::type_recursive_lock} RL_HIST_END();
            if (is_exclusive_recursive_)
            {
                exclusive_recursion_count_ += 1;
                return true;
            }
            else
            {
                RL_ASSERT_IMPL(false, test_result_recursion_on_nonrecursive_mutex, "", info);
            }
        }

        if (exclusive_owner_ == state_free)
        {
            RL_VERIFY(exclusive_recursion_count_ == 0);
            //!!! probability rand
            if (true == failing_try_lock_
                && false == try_lock_failed_
                && c.rand(2, sched_type_user))
            {
                try_lock_failed_ = true;
                RL_HIST(event_t) {this, event_t::type_spuriously_failed_try_lock} RL_HIST_END();
                return false;
            }
            else
            {
                sync_.acquire(c.threadx_);
                exclusive_recursion_count_ = 1;
                exclusive_owner_ = my_id;
                RL_HIST(event_t) {this, event_t::type_lock} RL_HIST_END();
                return true;
            }
        }
        else
        {
            //!!! in some implementation here must be acquire
            //sync_.acquire(c.threadx_);

            RL_VERIFY(my_id != exclusive_owner_);
            RL_HIST(event_t) {this, event_t::type_failed_try_lock} RL_HIST_END();
            return false;
        }
    }

    virtual void unlock_exclusive(debug_info_param info)
    {
        context& c = ctx();
        c.sched();
        sign_.check(info);
        RL_VERIFY(false == c.invariant_executing);

        thread_id_t const my_id = c.threadx_->index_;

        if (exclusive_owner_ != my_id)
        {
            RL_HIST(event_t) {this, event_t::type_unlock} RL_HIST_END();
            RL_ASSERT_IMPL(false, test_result_unlocking_mutex_wo_ownership, "", info);
        }

        exclusive_recursion_count_ -= 1;
        if (exclusive_recursion_count_)
        {
            RL_VERIFY(is_exclusive_recursive_);
            RL_HIST(event_t) {this, event_t::type_recursive_unlock} RL_HIST_END();
            return;
        }

        sync_.release(c.threadx_);
        exclusive_owner_ = state_free;
        RL_VERIFY(exclusive_recursion_count_ == 0);

        if (false == exclusive_waitset_.unpark_one(c, info))
            shared_waitset_.unpark_all(c, info);

        RL_HIST(event_t) {this, event_t::type_unlock} RL_HIST_END();
    }

    virtual void lock_shared(debug_info_param info)
    {
        RL_VERIFY(is_rw_);
        context& c = ctx();
        c.sched();
        sign_.check(info);
        RL_VERIFY(false == c.invariant_executing);

        thread_id_t const my_id = c.threadx_->index_;

        if (exclusive_owner_ == my_id)
        {
            RL_HIST(event_t) {this, event_t::type_lock_shared} RL_HIST_END();
            RL_ASSERT_IMPL(false, test_result_mutex_write_to_read_upgrade, "", info);
        }

        if (exclusive_owner_ == state_shared && shared_owner_[my_id])
        {
            RL_HIST(event_t) {this, event_t::type_recursive_lock_shared} RL_HIST_END();
            if (is_shared_recursive_)
            {
                shared_owner_[my_id] += 1;
                shared_lock_count_ += 1;
                return;
            }
            else
            {
                RL_ASSERT_IMPL(false, test_result_recursion_on_nonrecursive_mutex, "", info);
            }
        }

        for (;;)
        {
            if ((exclusive_owner_ == state_free)
                || (exclusive_owner_ == state_shared
                    && false == exclusive_waitset_))
            {
                sync_.acquire(c.threadx_);
                shared_owner_[my_id] += 1;
                shared_lock_count_ += 1;
                exclusive_owner_ = state_shared;
                RL_HIST(event_t) {this, event_t::type_lock_shared} RL_HIST_END();
                break;
            }
            else
            {
                RL_VERIFY(my_id != exclusive_owner_);
                RL_HIST(event_t) {this, event_t::type_wait} RL_HIST_END();
                shared_waitset_.park_current(c, false, false, false, info);
            }

            //??? c.sched();
            //sign_.check(info);
        }
    }

    virtual bool try_lock_shared(debug_info_param info)
    {
        RL_VERIFY(is_rw_);
        context& c = ctx();
        c.sched();
        sign_.check(info);
        RL_VERIFY(false == c.invariant_executing);

        thread_id_t const my_id = c.threadx_->index_;

        if (exclusive_owner_ == my_id)
        {
            RL_HIST(event_t) {this, event_t::type_lock_shared} RL_HIST_END();
            RL_ASSERT_IMPL(false, test_result_mutex_write_to_read_upgrade, "", info);
        }

        if (exclusive_owner_ == state_shared && shared_owner_[my_id])
        {
            RL_HIST(event_t) {this, event_t::type_recursive_lock_shared} RL_HIST_END();
            if (is_shared_recursive_)
            {
                shared_owner_[my_id] += 1;
                shared_lock_count_ += 1;
                return true;
            }
            else
            {
                RL_ASSERT_IMPL(false, test_result_recursion_on_nonrecursive_mutex, "", info);
            }
        }

        if ((exclusive_owner_ == state_free)
            || (exclusive_owner_ == state_shared
                && false == exclusive_waitset_))
        {
            //!!! probability rand
            if (true == failing_try_lock_
                && false == try_lock_failed_
                && c.rand(2, sched_type_user))
            {
                try_lock_failed_ = true;
                RL_HIST(event_t) {this, event_t::type_spuriously_failed_try_lock_shared} RL_HIST_END();
                return false;
            }
            else
            {
                sync_.acquire(c.threadx_);
                shared_owner_[my_id] += 1;
                shared_lock_count_ += 1;
                exclusive_owner_ = state_shared;
                RL_HIST(event_t) {this, event_t::type_lock_shared} RL_HIST_END();
                return true;
            }
        }
        else
        {
            RL_VERIFY(my_id != exclusive_owner_);
            RL_HIST(event_t) {this, event_t::type_failed_try_lock_shared} RL_HIST_END();
            return false;
        }
    }

    virtual void unlock_shared(debug_info_param info)
    {
        RL_VERIFY(is_rw_);
        context& c = ctx();
        c.sched();
        sign_.check(info);
        RL_VERIFY(false == c.invariant_executing);

        thread_id_t const my_id = c.threadx_->index_;

        if (exclusive_owner_ != state_shared || 0 == shared_owner_[my_id])
        {
            RL_HIST(event_t) {this, event_t::type_unlock_shared} RL_HIST_END();
            RL_ASSERT_IMPL(false, test_result_unlocking_mutex_wo_ownership, "", info);
        }

        RL_VERIFY(shared_lock_count_);
        shared_owner_[my_id] -= 1;
        shared_lock_count_ -= 1;
        if (shared_lock_count_ != 0)
        {
            if (shared_owner_[my_id])
            {
                RL_VERIFY(is_shared_recursive_);
                RL_HIST(event_t) {this, event_t::type_recursive_unlock_shared} RL_HIST_END();
            }
            else
            {
                sync_.release(c.threadx_);
                RL_HIST(event_t) {this, event_t::type_unlock_shared} RL_HIST_END();
            }
            return;
        }

        sync_.release(c.threadx_);
        exclusive_owner_ = state_free;

        exclusive_waitset_.unpark_one(c, info);

        RL_HIST(event_t) {this, event_t::type_unlock_shared} RL_HIST_END();
    }

    virtual void unlock_exclusive_or_shared(debug_info_param info)
    {
        if (exclusive_owner_ == ctx().threadx_->index_)
            unlock_exclusive(info);
        else
            unlock_shared(info);
    }

    virtual bool is_signaled(debug_info_param info)
    {
        (void)info;
        return (exclusive_owner_ == state_free);
    }

    virtual void memory_acquire(debug_info_param info)
    {
        (void)info;
        sync_.acquire(ctx().threadx_);
    }

    virtual void* prepare_wait(debug_info_param info)
    {
        (void)info;
        return &exclusive_waitset_;
    }

private:
    static thread_id_t const state_shared = (thread_id_t)-1;
    static thread_id_t const state_free = (thread_id_t)-2;

    signature<0xbabaf1f1> sign_;
    bool is_rw_;
    bool is_exclusive_recursive_;
    bool is_shared_recursive_;
    bool failing_try_lock_;
    sync_var<thread_count> sync_;
    thread_id_t exclusive_owner_;
    unsigned exclusive_recursion_count_;
    waitset<thread_count> exclusive_waitset_;
    waitset<thread_count> shared_waitset_;
    timestamp_t shared_owner_ [thread_count];
    unsigned shared_lock_count_;
    bool try_lock_failed_;

    RL_NOCOPY(generic_mutex_data_impl);
};




template<typename type>
class generic_mutex : public win_waitable_object
{
public:
    generic_mutex()
        : impl_()
    {
    }

    generic_mutex(generic_mutex const&)
        : impl_()
    {
    }

    generic_mutex& operator = (generic_mutex const&)
    {
        return *this;
    }

    ~generic_mutex()
    {
    }

    void init(bool is_rw, bool is_exclusive_recursive, bool is_shared_recursive, bool failing_try_lock, debug_info_param info)
    {
        context& c = ctx();
        RL_ASSERT_IMPL(0 == impl_, test_result_double_initialization_of_mutex, "", info);
        sign_.check(info);
        impl_ = c.mutex_ctor(is_rw, is_exclusive_recursive, is_shared_recursive, failing_try_lock);
    }

    void deinit(debug_info_param info)
    {
        context& c = ctx();
        check(info);
        c.mutex_dtor(impl_);
        impl_ = 0;
    }

    void lock(debug_info_param info)
    {
        lock_exclusive(info);
    }

    bool lock_exclusive_timed(debug_info_param info)
    {
        return check(info)->lock_exclusive(true, info);
    }

    void unlock(debug_info_param info)
    {
        unlock_exclusive(info);
    }

    void lock_exclusive(debug_info_param info)
    {
        check(info)->lock_exclusive(false, info);
    }

    bool try_lock_exclusive(debug_info_param info)
    {
        return check(info)->try_lock_exclusive(info);
    }

    void unlock_exclusive(debug_info_param info)
    {
        check(info)->unlock_exclusive(info);
    }

    void lock_shared(debug_info_param info)
    {
        check(info)->lock_shared(info);
    }

    bool try_lock_shared(debug_info_param info)
    {
        return check(info)->try_lock_shared(info);
    }

    void unlock_shared(debug_info_param info)
    {
        check(info)->unlock_shared(info);
    }

    void unlock_exclusive_or_shared(debug_info_param info)
    {
        check(info)->unlock_exclusive_or_shared(info);
    }

private:
    generic_mutex_data* impl_;
    signature<0x6A6cB03A> sign_;

    generic_mutex_data* check(debug_info_param info)
    {
        RL_ASSERT_IMPL(impl_, test_result_usage_of_non_initialized_mutex, "", info);
        sign_.check(info);
        return impl_;
    }

    virtual sema_wakeup_reason wait(bool try_wait, bool is_timed, debug_info_param info)
    {
        if (try_wait)
        {
            if (check(info)->try_lock_exclusive(info))
                return sema_wakeup_reason_success;
            else
                return sema_wakeup_reason_failed;
        }
        else
        {
            if (check(info)->lock_exclusive(is_timed, info))
                return sema_wakeup_reason_success;
            else
                return sema_wakeup_reason_timeout;

        }
    }

    virtual bool signal(debug_info_param info)
    {
        check(info)->unlock_exclusive(info);
        return true;
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




template<typename tag, bool is_recursive>
class std_generic_mutex : generic_mutex<tag>, nocopy<>
{
public:
    std_generic_mutex()
    {
        generic_mutex<tag>::init(false, is_recursive, false, true, $);
    }

    ~std_generic_mutex()
    {
        generic_mutex<tag>::deinit($);
    }

    void lock(debug_info_param info)
    {
        generic_mutex<tag>::lock_exclusive(info);
    }

    bool try_lock(debug_info_param info)
    {
        return generic_mutex<tag>::try_lock_exclusive(info);
    }

    void unlock(debug_info_param info)
    {
        generic_mutex<tag>::unlock_exclusive(info);
    }
};


struct mutex_tag_std;
typedef std_generic_mutex<mutex_tag_std, false> mutex;

struct mutex_tag_std_recursive;
typedef std_generic_mutex<mutex_tag_std_recursive, true> recursive_mutex;


}

#endif
