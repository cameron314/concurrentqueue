/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_MEMORY_ORDER_HPP
#define RL_MEMORY_ORDER_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"


namespace rl
{


enum memory_order
{
    mo_relaxed,
    mo_consume,
    mo_acquire,
    mo_release,
    mo_acq_rel,
    mo_seq_cst,
};




inline char const* format(memory_order mo)
{
    switch (mo)
    {
    case mo_relaxed: return "relaxed";
    case mo_consume: return "consume";
    case mo_acquire: return "acquire";
    case mo_release: return "release";
    case mo_acq_rel: return "acq_rel";
    case mo_seq_cst: return "seq_cst";
    }
    RL_VERIFY(!"invalid value of memory order");
    throw std::logic_error("invalid value of memory order");
}


}

#endif
