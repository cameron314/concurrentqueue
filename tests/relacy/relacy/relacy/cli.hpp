/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_CLI_HPP
#define RL_CLI_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "context_base.hpp"
#include "atomic_fence.hpp"


namespace rl
{


struct Thread
{
    static void MemoryBarrier(debug_info_param info)
    {
        atomic_thread_fence(mo_seq_cst, info);
    }

    template<typename T>
    static T VolatileRead(generic_atomic<T, true> const& v, debug_info_param info)
    {
        return v.load(mo_acquire, info);
    }

    template<typename T>
    static void VolatileWrite(generic_atomic<T, true>& v, T x, debug_info_param info)
    {
        v.store(x, mo_release, info);
    }

    static void SpinWait(int iterations, debug_info_param info)
    {
        ctx().yield(iterations, info);
    }
};

}

#endif
