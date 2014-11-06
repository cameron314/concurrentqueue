/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_RANDOM_HPP
#define RL_RANDOM_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"


namespace rl
{


unsigned const primes[16] = {1, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53};

struct random_generator
{
    unsigned k;
    unsigned c;
    unsigned x;

    void seed(iteration_t s)
    {
        k = ((unsigned)(s >> 32) & 0xf) + 8;
        c = primes[((unsigned)(s >> 36) & 0xf)];
        x = (unsigned)((s + 1) * 0x95949347 + c);
    }

    unsigned rand()
    {
        return ((x = x + c + (x << k)) >> 16);
    }

    template<typename T, T max>
    RL_INLINE
    T get()
    {
        return static_cast<T>(rand() % max);
    }
};



}

#endif
