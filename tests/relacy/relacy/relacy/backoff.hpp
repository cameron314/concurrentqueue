/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_BACKOFF_HPP
#define RL_BACKOFF_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "context_base.hpp"


namespace rl
{


inline void yield(unsigned count, debug_info_param info)
{
    ctx().yield(count, info);
}


template<unsigned factor_t, unsigned add_t>
class backoff_t
{
public:
    backoff_t()
        : count_(1)
    {
    }

    void yield(debug_info_param info)
    {
        rl::yield(count_, info);
        count_ = count_ * factor_t + add_t;
    }

private:
    unsigned count_;
};


typedef backoff_t<1, 0> backoff;
typedef backoff_t<1, 1> linear_backoff;
typedef backoff_t<2, 0> exp_backoff;


}

#endif
