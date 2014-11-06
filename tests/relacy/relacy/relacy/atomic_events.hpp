/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_ATOMIC_EVENTS_HPP
#define RL_ATOMIC_EVENTS_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "memory_order.hpp"
#include "rmw.hpp"


namespace rl
{

template<typename T> class atomic;
template<typename T, bool strong_init> class generic_atomic;

template<typename T>
struct atomic_add_type
{
    typedef T type;
    typedef T output_type;
};

template<typename T>
struct atomic_add_type<T*>
{
    typedef ptrdiff_t type;
    typedef void* output_type;
};




template<typename T>
struct atomic_cas_event
{
    typedef typename atomic_add_type<T>::output_type type;

    debug_info var_info_;
    void const* var_addr_;
    type cur_value_;
    type cmp_value_;
    type xchg_value_;
    memory_order mo_;
    bool success_;
    bool spurious_failure_;
    bool aba_;

    void output(std::ostream& s) const
    {
        s << "<" << std::hex << var_addr_ << std::dec << ">"
            << " CAS "
            << (success_ ? "succ " : "fail ")
            << (spurious_failure_ ? "[SPURIOUSLY] " : "")
            << (aba_ ? "[ABA] " : "")
            << "orig=" << cur_value_
            << ", cmp=" << cmp_value_
            << ", xchg=" << xchg_value_
            << ", order=" << format(mo_);
    }
};




template<typename T>
struct atomic_load_event
{
    typedef typename atomic_add_type<T>::output_type type;

    void const* var_addr_;
    type value_;
    memory_order mo_;
    bool not_current_;

    void output(std::ostream& s) const
    {
        s << "<" << std::hex << var_addr_ << std::dec << ">"
            << " atomic load, value=" << value_
            << (not_current_ ? " [NOT CURRENT]" : "")
            << ", order=" << format(mo_);
    }
};




template<typename T>
struct atomic_store_event
{
    typedef typename atomic_add_type<T>::output_type type;

    void const* var_addr_;
    type prev_value_;
    type value_;
    memory_order mo_;

    void output(std::ostream& s) const
    {
        s << "<" << std::hex << var_addr_ << std::dec << ">"
            << " atomic store, value=" << value_
            << ", (prev value=" << prev_value_ << ")"
            << ", order=" << format(mo_);
    }
};




template<typename T, typename Y>
struct atomic_rmw_event
{
    typedef typename atomic_add_type<T>::output_type type;

    debug_info var_info_;
    void const* var_addr_;
    type prev_value_;
    Y op_value_;
    type new_value_;
    memory_order mo_;
    rmw_type_e type_;

    void output(std::ostream& s) const
    {
        s << "<" << std::hex << var_addr_ << std::dec << ">"
            << " " << format(type_) << " "
            << ", prev=" << prev_value_
            << ", arg=" << op_value_
            << ", new=" << new_value_
            << ", order=" << format(mo_);
    }
};


}


#endif
