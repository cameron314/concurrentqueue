/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_FOREACH_HPP
#define RL_FOREACH_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"

namespace rl
{


template<typename T, thread_id_t i, thread_id_t index>
struct foreach_thread_impl
{
    template<typename F>
    RL_INLINE static void exec(
        T* v1,
        F func)
    {
        (*func)(v1[i]);
        foreach_thread_impl<T, i + 1, index - 1>::exec(v1, func);
    }

    RL_INLINE static void exec(
        T* v1, T* v2,
        void (*func)(T& e1, T& e2))
    {
        (*func)(v1[i], v2[i]);
        foreach_thread_impl<T, i + 1, index - 1>::exec(v1, v2, func);
    }

    RL_INLINE static void exec(
        T* v1, T* v2, T* v3,
        void (*func)(T& e1, T& e2, T& e3))
    {
        (*func)(v1[i], v2[i], v3[i]);
        foreach_thread_impl<T, i + 1, index - 1>::exec(v1, v2, v3, func);
    }
};

template<typename T, thread_id_t i>
struct foreach_thread_impl<T, i, 0>
{
    template<typename F>
    RL_INLINE static void exec(
        T*,
        F)
    {
    }

    RL_INLINE static void exec(
        T*, T*,
        void (*)(T&, T&))
    {
    }

    RL_INLINE static void exec(
        T*, T*, T*,
        void (*)(T&, T&, T&))
    {
    }
};

template<thread_id_t count, typename T, typename F>
RL_INLINE void foreach(
    T* v1,
    F func)
{
    foreach_thread_impl<T, 0, count>::exec(v1, func);
}

template<thread_id_t count, typename T>
RL_INLINE void foreach(
    T* v1, T* v2,
    void (*func)(T& e1, T& e2))
{
    foreach_thread_impl<T, 0, count>::exec(v1, v2, func);
}

template<thread_id_t count, typename T>
RL_INLINE void foreach(
    T* v1, T* v2, T* v3,
    void (*func)(T& e1, T& e2, T& e3))
{
    foreach_thread_impl<T, 0, count>::exec(v1, v2, v3, func);
}

RL_INLINE void assign_zero(timestamp_t& elem)
{
    elem = 0;
}

RL_INLINE void assign_zero_u(unsigned& elem)
{
    elem = 0;
}

template<timestamp_t value>
RL_INLINE void assign(timestamp_t& elem)
{
    elem = value;
}

RL_INLINE void assign(timestamp_t& elem1, timestamp_t& elem2)
{
    elem1 = elem2;
}

RL_INLINE void assign_max(timestamp_t& elem1, timestamp_t& elem2)
{
    if (elem2 > elem1)
        elem1 = elem2;
}

RL_INLINE void plus_one(timestamp_t& elem)
{
    elem += 1;
}

}


#endif
