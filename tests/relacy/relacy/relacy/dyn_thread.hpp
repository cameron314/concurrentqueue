/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_DYN_THREAD_HPP
#define RL_DYN_THREAD_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "context_base.hpp"
#include "stdlib/semaphore.hpp"


namespace rl
{


class dyn_thread : nocopy<>
{
public:
    dyn_thread()
    {
        handle_ = 0;
    }

    void start(void*(*fn)(void*), void* arg)
    {
        RL_VERIFY(handle_ == 0);
        handle_ = ctx().create_thread(fn, arg);
    }

    void join()
    {
        RL_VERIFY(handle_);
        handle_->wait(false, false, $);
        handle_ = 0;
    }

private:
    win_waitable_object* handle_;
};


}

#endif
