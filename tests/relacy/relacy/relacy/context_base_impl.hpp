/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_CONTEXT_BASE_IMPL_HPP
#define RL_CONTEXT_BASE_IMPL_HPP
#ifdef _MSC_VER
#   pragma once
#endif


namespace rl
{

/*
inline void context::disable_history()
{
    RL_VERIFY(threadx_);
    threadx_->disable_history_ += 1;
}

inline void context::enable_history()
{
    RL_VERIFY(threadx_);
    RL_VERIFY(threadx_->disable_history_);
    threadx_->disable_history_ -= 1;
}
*/

inline void context::disable_preemption()
{
    disable_preemption_ += 1;
}

inline void context::enable_preemption()
{
    disable_preemption_ -= 1;
}

inline int context::get_errno()
{
    RL_VERIFY(threadx_);
    return threadx_->errno_;
}

inline void context::set_errno(int value)
{
    RL_VERIFY(threadx_);
    threadx_->errno_ = value;
}

template<typename event_t>
void context::exec_log(debug_info_param info, event_t const& ev)
{
    RL_VERIFY(collecting_history());
    disable_alloc_ += 1;
    history_.exec_log(threadx_ ? threadx_->index_ : -1, info, ev, params_.output_history);
    disable_alloc_ -= 1;
}



}



#endif
