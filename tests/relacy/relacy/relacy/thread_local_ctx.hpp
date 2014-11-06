/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_THREAD_LOCAL_CTX_HPP
#define RL_THREAD_LOCAL_CTX_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "test_params.hpp"


namespace rl
{


struct thread_local_context_iface
{
    virtual int         thread_local_alloc          (void (*dtor)(intptr_t)) = 0;
    virtual void        thread_local_free           (int index) = 0;
    virtual void        thread_local_set            (int index, intptr_t value) = 0;
    virtual intptr_t    thread_local_get            (int index) = 0;
    virtual             ~thread_local_context_iface () {} // to calm down g++
};




template<typename base_t, thread_id_t thread_count>
class thread_local_contxt_impl : protected base_t
{
public:
    thread_local_contxt_impl(thread_id_t thread_count_param, test_params& params)
        : base_t(thread_count_param, params)
    {
    }

    void iteration_begin()
    {
        base_t::iteration_begin();

        for (size_t ent = 0; ent != entries_.size(); ent += 1)
        {
            for (size_t th = 0; th != thread_count; th += 1)
            {
                entries_[ent].value_[th] = 0;
            }
        }
    }

private:
    struct entry
    {
        bool            alive_;
        intptr_t        value_ [thread_count];
        void            (*dtor_) (intptr_t);
    };

    typename vector<entry>::type            entries_;
    using base_t::current_thread;

    virtual int         thread_local_alloc          (void (*dtor)(intptr_t))
    {
        int index = (int)entries_.size();
        entries_.resize(index + 1);
        entry& ent = entries_[index];
        ent.alive_ = true;
        ent.dtor_ = dtor;
        for (size_t i = 0; i != thread_count; ++i)
        {
            ent.value_[i] = 0;
        }
        return index;
    }

    virtual void        thread_local_free           (int index)
    {
        RL_VERIFY(index >= 0 && (size_t)index < entries_.size());
        entry& ent = entries_[index];
        RL_VERIFY(ent.alive_);
        ent.alive_ = false;
        if (ent.dtor_)
        {
            for (size_t i = 0; i != thread_count; ++i)
            {
                if (ent.value_[i])
                {
                    ent.dtor_(ent.value_[i]);
                }
            }
        }
    }

    virtual void        thread_local_set            (int index, intptr_t value)
    {
        RL_VERIFY(index >= 0 && (size_t)index < entries_.size());
        entry& ent = entries_[index];
        RL_VERIFY(ent.alive_);
        ent.value_[current_thread()] = value;
    }

    virtual intptr_t    thread_local_get            (int index)
    {
        RL_VERIFY(index >= 0 && (size_t)index < entries_.size());
        entry& ent = entries_[index];
        RL_VERIFY(ent.alive_);
        return ent.value_[current_thread()];
    }
};



}

#endif
