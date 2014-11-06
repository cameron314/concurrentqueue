/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_SYNC_VAR_HPP
#define RL_SYNC_VAR_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "foreach.hpp"


namespace rl
{


template<thread_id_t thread_count>
class sync_var : nocopy<>
{
public:
    sync_var()
    {
        iteration_begin();
    }

    void iteration_begin()
    {
        foreach<thread_count>(order_, &assign_zero);
    }

    void acquire(thread_info_base* th)
    {
        th->own_acq_rel_order_ += 1;
        foreach<thread_count>(th->acq_rel_order_, order_, &assign_max);
    }

    void release(thread_info_base* th)
    {
        th->own_acq_rel_order_ += 1;
        foreach<thread_count>(order_, th->acq_rel_order_, &assign_max);
    }

    void acq_rel(thread_info_base* th)
    {
        th->own_acq_rel_order_ += 1;
        timestamp_t* acq_rel_order = th->acq_rel_order_;
        timestamp_t* order = order_;
        foreach<thread_count>(acq_rel_order, order, &assign_max);
        foreach<thread_count>(order, acq_rel_order, &assign_max);
    }

private:
    timestamp_t order_ [thread_count];
};


}

#endif
