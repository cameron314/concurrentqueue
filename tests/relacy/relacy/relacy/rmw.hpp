/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_RMW_HPP
#define RL_RMW_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"


namespace rl
{


enum rmw_type_e
{
    rmw_type_swap,
    rmw_type_add,
    rmw_type_sub,
    rmw_type_and,
    rmw_type_or,
    rmw_type_xor,
};




inline char const* format(rmw_type_e t)
{
    switch (t)
    {
    case rmw_type_swap: return "exchange";
    case rmw_type_add: return "fetch_add";
    case rmw_type_sub: return "fetch_sub";
    case rmw_type_and: return "fetch_and";
    case rmw_type_or: return "fetch_or";
    case rmw_type_xor: return "fetch_xor";
    }
    RL_VERIFY(!"invalid rmw type");
    throw std::logic_error("invalid rmw type");
}




template<rmw_type_e type> struct rmw_type_t {};




template<typename T, typename Y>
T perform_rmw(rmw_type_t<rmw_type_swap>, T v, Y op)
{
    (void)v;
    return op;
}

template<typename T, typename Y>
T perform_rmw(rmw_type_t<rmw_type_add>, T v, Y op)
{
    return v + op;
}

template<typename T, typename Y>
T perform_rmw(rmw_type_t<rmw_type_sub>, T v, Y op)
{
    return v - op;
}

template<typename T, typename Y>
T perform_rmw(rmw_type_t<rmw_type_and>, T v, Y op)
{
    return v & op;
}

template<typename T, typename Y>
T perform_rmw(rmw_type_t<rmw_type_or>, T v, Y op)
{
    return v | op;
}

template<typename T, typename Y>
T perform_rmw(rmw_type_t<rmw_type_xor>, T v, Y op)
{
    return v ^ op;
}



}


#endif
