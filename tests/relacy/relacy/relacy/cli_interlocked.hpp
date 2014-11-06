/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_CLI_INTERLOCKED_HPP
#define RL_CLI_INTERLOCKED_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "atomic.hpp"


namespace rl
{

    struct Interlocked
    {
        template<typename T>
        static T Add(generic_atomic<T, true>& v, T x, debug_info_param info)
        {
            T result = v.rmw(rmw_type_t<rmw_type_add>(), x, mo_seq_cst, info) + x;
            return result;
        }

        template<typename T>
        static T CompareExchange(generic_atomic<T, true>& v, T xchg, T cmp, debug_info_param info)
        {
            v.compare_exchange(bool_t<false>(), cmp, xchg, mo_seq_cst, mo_seq_cst, info);
            return cmp;
        }

        template<typename T>
        static T Increment(generic_atomic<T, true>& v, debug_info_param info)
        {
            return Add(v, (T)1, info);
        }

        template<typename T>
        static T Decrement(generic_atomic<T, true>& v, debug_info_param info)
        {
            return Add(v, (T)-1, info);
        }

        template<typename T>
        static T Exchange(generic_atomic<T, true>& v, T x, debug_info_param info)
        {
            T result = v.rmw(rmw_type_t<rmw_type_swap>(), x, mo_seq_cst, info);
            return result;
        }

        template<typename T>
        static T Read(generic_atomic<T, true> const& v, debug_info_param info)
        {
            return v.load(mo_acquire, info);
        }
    };

}

#endif
