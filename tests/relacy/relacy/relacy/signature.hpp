/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_SIGNATURE_HPP
#define RL_SIGNATURE_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "test_result.hpp"
#include "context_base.hpp"


namespace rl
{


template<unsigned magic>
class signature
{
public:
    signature()
        : magic_(magic)
    {
    }

    signature(signature const&)
        : magic_(magic)
    {
    }

    ~signature()
    {
        check(RL_INFO);
        magic_ = 0;
    }

    void check(debug_info_param info) const
    {
        if (
            ((uintptr_t)this <= (uintptr_t)-1 - 4096) && 
            ((uintptr_t)this >= 4096) &&
            ((uintptr_t)this % sizeof(unsigned) == 0) && (magic == magic_))
        {
            return;
        }
        else
        {
            fail(info);
        }
    }

private:
    unsigned magic_;

    struct fault_event
    {
        void const* addr_;
        void output(std::ostream& s) const
        {
            s << "<" << std::hex << addr_ << std::dec << ">"
                << " access to freed memory";
        }
    };

    RL_NOINLINE void fail(debug_info_param info) const
    {
        context& c = ctx();
        RL_HIST(fault_event) {this} RL_HIST_END();
        rl::ctx().fail_test("access to freed memory", test_result_access_to_freed_memory, info);
    }
};


}

#endif
