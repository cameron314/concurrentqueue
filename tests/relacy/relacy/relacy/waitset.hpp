/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_WAITSET_HPP
#define RL_WAITSET_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "thread_base.hpp"
#include "context_base.hpp"


namespace rl
{


template<thread_id_t thread_count>
class waitset
{
public:
    waitset()
    {
        size_ = 0;
    }

    unpark_reason park_current(context& c,
                               bool is_timed,
                               bool allow_spurious_wakeup,
                               bool do_switch,
                               debug_info_param info)
    {
        RL_VERIFY(size_ < thread_count);
        thread_info_base* th = c.threadx_;
        thread_desc desc = {th, 0, 0, 0, false, do_switch};
        set_[size_] = desc;
        size_ += 1;
        unpark_reason reason = c.park_current_thread(is_timed, allow_spurious_wakeup, do_switch, info);
        if (reason == unpark_reason_normal)
        {
            if (do_switch)
                RL_VERIFY(c.threadx_->temp_switch_from_ != -1);
            else
                RL_VERIFY(c.threadx_->temp_switch_from_ == -1);
        }
        else
        {
            remove(th);
        }
        return reason;
    }

    static unpark_reason park_current(context& c,
                                      waitset** ws,
                                      win_waitable_object** wo,
                                      size_t count,
                                      bool wait_all,
                                      bool is_timed,
                                      bool do_switch,
                                      debug_info_param info)
    {
        thread_info_base* th = c.threadx_;
        thread_desc desc = {th, (unsigned)count, ws, wo, wait_all, do_switch};
        for (unsigned wsi = 0; wsi != count; ++wsi)
        {
            RL_VERIFY(ws[wsi]->size_ < thread_count);
            ws[wsi]->set_[ws[wsi]->size_] = desc;
            ws[wsi]->size_ += 1;
        }
        unpark_reason reason = c.park_current_thread(is_timed, false, do_switch, info);
        if (reason == unpark_reason_normal)
        {
            if (do_switch)
                RL_VERIFY(c.threadx_->temp_switch_from_ != -1);
            else
                RL_VERIFY(c.threadx_->temp_switch_from_ == -1);
        }
        else
        {
            remove(th, ws, (unsigned)count);
        }
        return reason;
    }

    bool unpark_one(context& c, debug_info_param info)
    {
        if (0 == size_)
            return false;
        //!!! too high preassure on full sched
        thread_id_t idx = c.rand(size_, sched_type_user);
        if (try_remove(c, idx, info))
            return true;
        for (idx = 0; idx != size_; idx += 1)
        {
            if (try_remove(c, idx, info))
                return true;
        }
        return false;
    }

    thread_id_t unpark_all(context& c, debug_info_param info)
    {
        thread_id_t cnt = 0;
        for (thread_id_t idx = 0; idx != size_; idx += 1)
        {
            if (try_remove(c, idx, info))
            {
                cnt += 1;
                idx -= 1;
            }
        }
        return cnt;
    }

    thread_id_t size() const
    {
        return size_;
    }

    operator bool () const
    {
        return 0 != size_;
    }

private:
    struct thread_desc
    {
        thread_info_base*       th_;
        unsigned                count_;     // 0 - wfso, !0 - wfmo
        waitset**               ws_;        // 0 - wfso, !0 - wfmo
        win_waitable_object**   wo_;        // 0 - wfso, !0 - wfmo
        bool                    wait_all_;
        bool                    do_switch_;
    };

    thread_desc                 set_ [thread_count];
    thread_id_t                 size_;

    bool try_remove(context& c, thread_id_t const idx, debug_info_param info)
    {
        RL_VERIFY(idx < size_);
        thread_desc const& d = set_[idx];
        if (d.count_ != 0 && d.wait_all_ == true)
        {
            for (size_t i = 0; i != d.count_; i += 1)
            {
                if (d.wo_[i]->is_signaled(info) == false)
                    return false;
            }
        }
        size_t const tid = d.th_->index_;
        bool const do_switch = d.do_switch_;
        if (d.ws_)
            remove(d.th_, d.ws_, d.count_);
        else
            remove(d.th_);
        c.unpark_thread(tid, do_switch, info);
        return true;
    }

    void remove(thread_info_base* th)
    {
        thread_id_t size = size_;
        thread_id_t i = 0;
        for (; i != size; ++i)
        {
            if (set_[i].th_ == th)
                break;
        }
        RL_VERIFY(i != size);
        for (thread_id_t j = i + 1; j != size; ++j)
        {
            set_[j - 1] = set_[j];
        }
        size_ -= 1;
    }

    static void remove(thread_info_base* th, waitset** ws, unsigned count)
    {
        for (unsigned wsi = 0; wsi != count; ++wsi)
        {
            ws[wsi]->remove(th);
        }
    }
};


}


#endif
