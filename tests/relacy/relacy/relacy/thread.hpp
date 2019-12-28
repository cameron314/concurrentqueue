/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_THREAD_HPP
#define RL_THREAD_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "context_base.hpp"
#include "dyn_thread_ctx.hpp"
#include "thread_base.hpp"
#include "test_suite.hpp"
#include "memory_order.hpp"
#include "foreach.hpp"


namespace rl
{



struct atomic_data;
struct var_data;
template<thread_id_t thread_count> struct atomic_data_impl;
template<thread_id_t thread_count> struct var_data_impl;


template<thread_id_t thread_count>
struct thread_info : thread_info_base
{
    thread_info(thread_id_t index = 0)
        : thread_info_base(index, acq_rel_order_)
    {
    }

    void iteration_begin()
    {
        sync_object_.iteration_begin();
        last_yield_ = 0;
        dynamic_thread_func_ = 0;
        dynamic_thread_param_ = 0;
        for (thread_id_t j = 0; j != thread_count; ++j)
        {
            acq_rel_order_[j] = 0;
        }
        acq_rel_order_[index_] = 1;
        temp_switch_from_ = -1;
        saved_disable_preemption_ = -1;
    }

    thread_sync_object<thread_count> sync_object_;

    timestamp_t acq_rel_order_ [thread_count];
    timestamp_t acquire_fence_order_ [thread_count];
    timestamp_t release_fence_order_ [thread_count];

#ifdef RL_IMPROVED_SEQ_CST_FENCE
    timestamp_t imp_seq_cst_order_ [thread_count];
#endif

    virtual void on_start()
    {
        RL_VERIFY(temp_switch_from_ == -1);
        RL_VERIFY(saved_disable_preemption_ == -1);
        sync_object_.on_start();
    }

    virtual void on_finish()
    {
        RL_VERIFY(temp_switch_from_ == -1);
        RL_VERIFY(saved_disable_preemption_ == -1);
        sync_object_.on_finish();
    }

    void atomic_thread_fence_acquire()
    {
        foreach<thread_count>(
            acq_rel_order_,
            acquire_fence_order_,
            &assign_max);
    }

    void atomic_thread_fence_release()
    {
        foreach<thread_count>(
            release_fence_order_,
            acq_rel_order_,
            &assign);
    }

    void atomic_thread_fence_acq_rel()
    {
        atomic_thread_fence_acquire();
        atomic_thread_fence_release();
    }

    void atomic_thread_fence_seq_cst(timestamp_t* seq_cst_fence_order)
    {
#ifdef RL_IMPROVED_SEQ_CST_FENCE
        foreach<thread_count>(acq_rel_order_, imp_seq_cst_order_, assign_max);
#endif

        atomic_thread_fence_acquire();

        foreach<thread_count>(
            acq_rel_order_,
            seq_cst_fence_order,
            &assign_max);

        foreach<thread_count>(
            seq_cst_fence_order,
            acq_rel_order_,
            &assign);

        atomic_thread_fence_release();
    }

    virtual ~thread_info() {} // just to calm down gcc

private:
    thread_info(thread_info const&);
    thread_info& operator = (thread_info const&);

    virtual unsigned atomic_load_relaxed(atomic_data* RL_RESTRICT data)
    {
        return atomic_load<mo_relaxed, false>(data);
    }

    virtual unsigned atomic_load_acquire(atomic_data* RL_RESTRICT data)
    {
        return atomic_load<mo_acquire, false>(data);
    }

    virtual unsigned atomic_load_seq_cst(atomic_data* RL_RESTRICT data)
    {
        return atomic_load<mo_seq_cst, false>(data);
    }

    virtual unsigned atomic_load_relaxed_rmw(atomic_data* RL_RESTRICT data)
    {
        return atomic_load<mo_relaxed, true>(data);
    }

    virtual unsigned atomic_load_acquire_rmw(atomic_data* RL_RESTRICT data)
    {
        return atomic_load<mo_acquire, true>(data);
    }

    virtual unsigned atomic_load_seq_cst_rmw(atomic_data* RL_RESTRICT data)
    {
        return atomic_load<mo_seq_cst, true>(data);
    }

    virtual unsigned atomic_store_relaxed(atomic_data* RL_RESTRICT data)
    {
        return atomic_store<mo_relaxed, false>(data);
    }

    virtual unsigned atomic_store_release(atomic_data* RL_RESTRICT data)
    {
        return atomic_store<mo_release, false>(data);
    }

    virtual unsigned atomic_store_seq_cst(atomic_data* RL_RESTRICT data)
    {
        return atomic_store<mo_seq_cst, false>(data);
    }

    virtual unsigned atomic_rmw_relaxed(atomic_data* RL_RESTRICT data, bool& aba)
    {
        return atomic_rmw<mo_relaxed>(data, aba);
    }

    virtual unsigned atomic_rmw_acquire(atomic_data* RL_RESTRICT data, bool& aba)
    {
        return atomic_rmw<mo_acquire>(data, aba);
    }

    virtual unsigned atomic_rmw_release(atomic_data* RL_RESTRICT data, bool& aba)
    {
        return atomic_rmw<mo_release>(data, aba);
    }

    virtual unsigned atomic_rmw_acq_rel(atomic_data* RL_RESTRICT data, bool& aba)
    {
        return atomic_rmw<mo_acq_rel>(data, aba);
    }

    virtual unsigned atomic_rmw_seq_cst(atomic_data* RL_RESTRICT data, bool& aba)
    {
        return atomic_rmw<mo_seq_cst>(data, aba);
    }

    template<memory_order mo, bool rmw>
    unsigned get_load_index(atomic_data_impl<thread_count>& var)
    {
        typedef typename atomic_data_impl<thread_count>::history_record history_t;

        unsigned index = var.current_index_;
        context& c = ctx();

        if (false == val(rmw))
        {
            size_t const limit = c.is_random_sched() ? atomic_history_size  - 1: 1;
            for (size_t i = 0; i != limit; ++i, --index)
            {
                history_t const& rec = var.history_[index % atomic_history_size];
                if (false == rec.busy_)
                    return (unsigned)-1; // access to unitialized var

                history_t const& prev = var.history_[(index - 1) % atomic_history_size];
                if (prev.busy_ && prev.first_seen_order_[index_] <= last_yield_)
                    break;

                if (mo_seq_cst == val(mo) && rec.seq_cst_)
                    break;

                timestamp_t acq_rel_order =
                    acq_rel_order_[rec.thread_id_];

                if (acq_rel_order >= rec.acq_rel_timestamp_)
                    break;

                //  This check ensures read-read coherence, 1.10/16:
                //  If a value computation A of an atomic object M happens before a value
                //  computation B of M, and A takes its value from a side effect X on M,
                //  then the value computed by B shall either be the value stored by X or
                //  the value stored by a side effect Y on M, where Y follows X in the
                //  modification order of M. 
                bool stop = false;
                for (thread_id_t i = 0; i != thread_count; ++i)
                {
                    timestamp_t acq_rel_order2 = acq_rel_order_[i];
                    if (acq_rel_order2 >= rec.first_seen_order_[i])
                    {
                        stop = true;
                        break;
                    }
                }
                if (stop)
                    break;

                if (0 == c.rand(2, sched_type_atomic_load))
                    break;
            }
        }

        if (false == var.history_[index % atomic_history_size].busy_)
            return (unsigned)-1;

        return index;
    }

    template<memory_order mo, bool rmw>
    unsigned atomic_load(atomic_data* RL_RESTRICT data)
    {
        RL_VERIFY(mo_release != mo || rmw);
        RL_VERIFY(mo_acq_rel != mo || rmw);

        atomic_data_impl<thread_count>& var = 
            *static_cast<atomic_data_impl<thread_count>*>(data);

        typedef typename atomic_data_impl<thread_count>::history_record history_t;

        unsigned index = get_load_index<mo, rmw>(var);
        if ((unsigned)-1 == index)
            return (unsigned)-1;

        index %= atomic_history_size;
        history_t& rec = var.history_[index];
        RL_VERIFY(rec.busy_);

        own_acq_rel_order_ += 1;
        if ((timestamp_t)-1 == rec.first_seen_order_[index_])
            rec.first_seen_order_[index_] = own_acq_rel_order_;

        bool const synch =
            (mo_acquire == mo
            || mo_acq_rel == mo
            || mo_seq_cst == mo);

        timestamp_t* acq_rel_order = (synch ? acq_rel_order_ : acquire_fence_order_);

        foreach<thread_count>(acq_rel_order, rec.acq_rel_order_, assign_max);

        return index;
    }

    virtual unsigned atomic_init(atomic_data* RL_RESTRICT data)
    {
        atomic_data_impl<thread_count>& var = 
            *static_cast<atomic_data_impl<thread_count>*>(data);

        typedef typename atomic_data_impl<thread_count>::history_record history_t;

        unsigned const idx = ++var.current_index_ % atomic_history_size;
        history_t& rec = var.history_[idx];

        rec.busy_ = true;
        rec.thread_id_ = index_;
        rec.seq_cst_ = false;
        rec.acq_rel_timestamp_ = 0;

        foreach<thread_count>(rec.acq_rel_order_, assign_zero);

        return idx;
    }

    template<memory_order mo, bool rmw>
    unsigned atomic_store(atomic_data* RL_RESTRICT data)
    {
        RL_VERIFY(mo_consume != mo || rmw);
        RL_VERIFY(mo_acquire != mo || rmw);
        RL_VERIFY(mo_acq_rel != mo || rmw);

        atomic_data_impl<thread_count>& var = 
            *static_cast<atomic_data_impl<thread_count>*>(data);

        typedef typename atomic_data_impl<thread_count>::history_record history_t;

        unsigned const idx = ++var.current_index_ % atomic_history_size;
        history_t& rec = var.history_[idx];

        rec.busy_ = true;
        rec.thread_id_ = index_;
        rec.seq_cst_ = (mo_seq_cst == mo);

        own_acq_rel_order_ += 1;
        rec.acq_rel_timestamp_ = own_acq_rel_order_;

        foreach<thread_count>(rec.first_seen_order_, assign<(timestamp_t)-1>);

        rec.first_seen_order_[index_] = own_acq_rel_order_;

        unsigned const prev_idx = (var.current_index_ - 1) % atomic_history_size;
        history_t& prev = var.history_[prev_idx];

#ifdef RL_IMPROVED_SEQ_CST_FENCE
        if (val(mo) == mo_release && val(rmw) == false)
            foreach<thread_count>(imp_seq_cst_order_, prev.acq_rel_order_, assign_max);
#endif

        bool const synch = 
            (mo_release == mo
            || mo_acq_rel == mo
            || mo_seq_cst == mo);

        bool const preserve = 
            prev.busy_ && (rmw || (index_ == prev.thread_id_));

        timestamp_t* acq_rel_order = (synch ? acq_rel_order_ : release_fence_order_);

        if (preserve)
        {
            foreach<thread_count>(rec.acq_rel_order_, prev.acq_rel_order_, assign);
            foreach<thread_count>(rec.acq_rel_order_, acq_rel_order, assign_max);
        }
        else
        {
            foreach<thread_count>(rec.acq_rel_order_, acq_rel_order, assign);
        }

        return idx;
    }

    template<memory_order mo>
    unsigned atomic_rmw(atomic_data* RL_RESTRICT data, bool& aba)
    {
        atomic_data_impl<thread_count>& var = 
            *static_cast<atomic_data_impl<thread_count>*>(data);
        timestamp_t const first_seen = var.history_[var.current_index_ % atomic_history_size].first_seen_order_[index_];
        aba = ((timestamp_t)-1 == first_seen);
        atomic_load<mo, true>(data);
        unsigned result = atomic_store<mo, true>(data);

#ifdef RL_IMPROVED_SEQ_CST_RMW
        atomic_thread_fence_seq_cst(ctx_->seq_cst_fence_order_);
#endif

        return result;
    }

    virtual unpark_reason atomic_wait(atomic_data* RL_RESTRICT data, bool is_timed, bool allow_spurious_wakeup, debug_info_param info)
    {
        context& c = ctx();
        atomic_data_impl<thread_count>& var = 
            *static_cast<atomic_data_impl<thread_count>*>(data);
        unpark_reason const res = var.futex_ws_.park_current(c, is_timed, allow_spurious_wakeup, false, info);
        if (res == unpark_reason_normal)
            var.futex_sync_.acquire(this);
        return res;
    }

    virtual thread_id_t atomic_wake(atomic_data* RL_RESTRICT data, thread_id_t count, debug_info_param info)
    {
        context& c = ctx();
        atomic_data_impl<thread_count>& var = 
            *static_cast<atomic_data_impl<thread_count>*>(data);
        thread_id_t unblocked = 0;
        for (; count != 0; count -= 1, unblocked += 1)
        {
            if (var.futex_ws_.unpark_one(c, info) == false)
                break;
        }
        if (unblocked != 0)
            var.futex_sync_.release(this);
        return unblocked;
    }
};


}

#endif
