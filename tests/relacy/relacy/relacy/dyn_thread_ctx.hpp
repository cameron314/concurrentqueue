/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_DYN_THREAD_CTX_HPP
#define RL_DYN_THREAD_CTX_HPP
#ifdef _MSC_VER
#   pragma once
#endif


#include "base.hpp"
#include "waitset.hpp"
#include "sync_var.hpp"
#include "stdlib/semaphore.hpp"


namespace rl
{


template<thread_id_t thread_count>
class thread_sync_object : public win_waitable_object
{
public:
    thread_sync_object()
    {
    }

    void iteration_begin()
    {
        finished_ = false;
        sync_.iteration_begin();
        RL_VERIFY(!ws_);
    }

    void on_create()
    {
        sync_.release(ctx().threadx_);
    }

    void on_start()
    {
        RL_VERIFY(finished_ == false);
        context& c = ctx();
        sync_.acquire(c.threadx_);
    }
    
    void on_finish()
    {
        RL_VERIFY(finished_ == false);
        context& c = ctx();
        finished_ = true;
        sync_.release(c.threadx_);
        ws_.unpark_all(c, $);
    }

private:
    bool finished_;
    waitset<thread_count> ws_;
    sync_var<thread_count> sync_;

    virtual void deinit(debug_info_param info)
    {
        (void)info;
    }

    virtual sema_wakeup_reason wait(bool try_wait, bool is_timed, debug_info_param info)
    {
        context& c = ctx();
        if (finished_)
        {
            sync_.acquire(c.threadx_);
            return sema_wakeup_reason_success;
        }
        else if (try_wait)
        {
            sync_.acquire(c.threadx_);
            return sema_wakeup_reason_failed;
        }
        else
        {
            unpark_reason reason = ws_.park_current(c, is_timed, false, false, info);
            sync_.acquire(c.threadx_);
            if (reason == unpark_reason_normal)
                return sema_wakeup_reason_success;
            else if (reason == unpark_reason_timeout)
                return sema_wakeup_reason_timeout;
            RL_VERIFY(false);
            return sema_wakeup_reason_failed;
        }
    }

    virtual bool signal(debug_info_param info)
    {
        RL_ASSERT_IMPL(false, test_result_thread_signal, "trying to signal a thread", info);
        return false;
    }

    virtual bool is_signaled(debug_info_param info)
    {
        (void)info;
        return finished_;
    }

    virtual void memory_acquire(debug_info_param info)
    {
        (void)info;
        sync_.acquire(ctx().threadx_);
    }

    virtual void* prepare_wait(debug_info_param info)
    {
        (void)info;
        return &ws_;
    }
};


}

#endif
