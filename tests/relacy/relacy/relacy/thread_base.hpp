/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_THREAD_BASE_HPP
#define RL_THREAD_BASE_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "context_base.hpp"
//#include "test_suite.hpp"
//#include "memory_order.hpp"
//#include "foreach.hpp"


namespace rl
{



struct atomic_data;
struct var_data;
template<thread_id_t thread_count> struct atomic_data_impl;
template<thread_id_t thread_count> struct var_data_impl;


class thread_info_base
{
public:
    virtual void on_start() = 0;
    virtual void on_finish() = 0;

    virtual unsigned atomic_init(atomic_data* RL_RESTRICT data) = 0;

    virtual unsigned atomic_load_relaxed(atomic_data* RL_RESTRICT data) = 0;
    virtual unsigned atomic_load_acquire(atomic_data* RL_RESTRICT data) = 0;
    virtual unsigned atomic_load_seq_cst(atomic_data* RL_RESTRICT data) = 0;
    virtual unsigned atomic_load_relaxed_rmw(atomic_data* RL_RESTRICT data) = 0;
    virtual unsigned atomic_load_acquire_rmw(atomic_data* RL_RESTRICT data) = 0;
    virtual unsigned atomic_load_seq_cst_rmw(atomic_data* RL_RESTRICT data) = 0;

    virtual unsigned atomic_store_relaxed(atomic_data* RL_RESTRICT data) = 0;
    virtual unsigned atomic_store_release(atomic_data* RL_RESTRICT data) = 0;
    virtual unsigned atomic_store_seq_cst(atomic_data* RL_RESTRICT data) = 0;

    virtual unsigned atomic_rmw_relaxed(atomic_data* RL_RESTRICT data, bool& aba) = 0;
    virtual unsigned atomic_rmw_acquire(atomic_data* RL_RESTRICT data, bool& aba) = 0;
    virtual unsigned atomic_rmw_release(atomic_data* RL_RESTRICT data, bool& aba) = 0;
    virtual unsigned atomic_rmw_acq_rel(atomic_data* RL_RESTRICT data, bool& aba) = 0;
    virtual unsigned atomic_rmw_seq_cst(atomic_data* RL_RESTRICT data, bool& aba) = 0;

    virtual unpark_reason atomic_wait(atomic_data* RL_RESTRICT data, bool is_timed, bool allow_spurious_wakeup, debug_info_param info) = 0;
    virtual thread_id_t atomic_wake(atomic_data* RL_RESTRICT data, thread_id_t count, debug_info_param info) = 0;

    virtual ~thread_info_base() {} // just to calm down gcc

    fiber_t fiber_;
    thread_id_t const index_;
    context* ctx_;
    timestamp_t* const acq_rel_order_;
    timestamp_t last_yield_;
    timestamp_t& own_acq_rel_order_;
    unpark_reason unpark_reason_;
    thread_id_t temp_switch_from_;
    int saved_disable_preemption_;
    int errno_;
    void* (*dynamic_thread_func_)(void*);
    void* dynamic_thread_param_;
    //unsigned disable_history_;

    thread_info_base(thread_id_t index, timestamp_t* acq_rel_order)
        : index_(index)
        , acq_rel_order_(acq_rel_order)
        , own_acq_rel_order_(acq_rel_order[index])
    {
    }

private:
    thread_info_base(thread_info_base const&);
    thread_info_base& operator = (thread_info_base const&);
};




}

#endif
