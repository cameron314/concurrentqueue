/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_THREAD_LOCAL_HPP
#define RL_THREAD_LOCAL_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "signature.hpp"
#include "context.hpp"


namespace rl
{


class generic_thread_local : nocopy<>
{
public:
    generic_thread_local()
        : index_(-1)
    {
    }

    ~generic_thread_local()
    {
    }

    void init(void (*dtor)(intptr_t), debug_info_param info)
    {
        sign_.check(info);
        //RL_ASSERT(index_ == -1);
        index_ = ctx().thread_local_alloc(dtor);
    }

    void deinit(debug_info_param info)
    {
        sign_.check(info);
        RL_ASSERT(index_ != -1);
        ctx().thread_local_free(index_);
        index_ = -1;
    }

    void set(intptr_t value, debug_info_param info)
    {
        sign_.check(info);
        ctx().thread_local_set(index_, value);
    }

    intptr_t get(debug_info_param info)
    {
        sign_.check(info);
        return ctx().thread_local_get(index_);
    }

private:
    signature<0xf1724ae2> sign_;
    int index_;
};


template<typename T>
class thread_local_var;


template<typename T>
class thread_local_proxy
{
public:
    thread_local_proxy(thread_local_var<T>& var, debug_info_param info)
        : var_(var)
        , info_(info)
    {}

    operator T () const
    {
        return var_.get(info_);
    }

    T operator -> () const
    {
        return var_.get(info_);
    }

    thread_local_proxy operator = (T value)
    {
        var_.set(value, info_);
        return *this;
    }

private:
    thread_local_var<T>& var_;
    debug_info info_;
    thread_local_proxy& operator = (thread_local_proxy const&);
};


template<typename T>
class thread_local_var : generic_thread_local
{
public:
    thread_local_var()
        : ctx_seq_()
    {
    }

    ~thread_local_var()
    {
        if (is_ctx() && ctx_seq_ == ctx().get_ctx_seq())
        {
            generic_thread_local::deinit($);
        }
    }

    thread_local_proxy<T> operator () (debug_info_param info)
    {
        return thread_local_proxy<T>(*this, info);
    }

    void set(T value, debug_info_param info)
    {
        if (ctx_seq_ != ctx().get_ctx_seq())
        {
            ctx_seq_ = ctx().get_ctx_seq();
            generic_thread_local::init(0, info);
        }
        generic_thread_local::set((intptr_t)value, info);
    }

    T get(debug_info_param info)
    {
        if (ctx_seq_ != ctx().get_ctx_seq())
        {
            ctx_seq_ = ctx().get_ctx_seq();
            generic_thread_local::init(0, info);
        }
        return (T)generic_thread_local::get(info);
    }

private:
    unsigned ctx_seq_;
};


inline unsigned long rl_TlsAlloc(debug_info_param info)
{
#ifndef RL_GC
    //!!! may break on x64 platform
    // TLS index is exactly DWORD (not DWORD_PTR), so one has to use indirection
    return (unsigned long)new (info) thread_local_var<void*> ();
#else
    void* p = ctx().alloc(sizeof(thread_local_var<void*>), false, info);
    new (p) thread_local_var<void*> ();
    return (unsigned long)p;
#endif
}

inline void rl_TlsFree(unsigned long slot, debug_info_param info)
{
#ifndef RL_GC
    delete_impl((thread_local_var<void*>*)slot, info);
#else
    thread_local_var<void*>* t = (thread_local_var<void*>*)slot;
    t->~thread_local_var<void*>();
    ctx().free(t, false, info);
#endif
}

inline void* rl_TlsGetValue(unsigned long slot, debug_info_param info)
{
    return ((thread_local_var<void*>*)slot)->get(info);
}

inline int rl_TlsSetValue(unsigned long slot, void* value, debug_info_param info)
{
    ((thread_local_var<void*>*)slot)->set(value, info);
    return 1;
}


#define TlsAlloc() rl::rl_TlsAlloc($)
#define TlsFree(slot) rl::rl_TlsFree((slot), $)
#define TlsGetValue(slot) rl::rl_TlsGetValue((slot), $)
#define TlsSetValue(slot, value) rl::rl_TlsSetValue((slot), (value), $)

}

#endif
