/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_CLI_VOLATILE_HPP
#define RL_CLI_VOLATILE_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "atomic.hpp"


//!!! fix Java volatiles!
// they must be modeled as seq_cst stores/loads

namespace rl
{

template<typename T> class nvolatile;


template<typename T>
class nvolatile_proxy
{
public:
    typedef typename atomic_add_type<T>::type add_type;
    template<typename Y> friend class nvolatile;

    operator T () const
    {
        return load();
    }

    T operator = (T value)
    {
        store(value);
        return value;
    }

    T operator = (nvolatile_proxy const& r)
    {
        T const value = r.load();
        store(value);
        return *this;
    }

    T operator ++ (int)
    {
        T tmp = load();
        store(tmp + 1);
        return tmp;
    }

    T operator -- (int)
    {
        T tmp = load();
        store(tmp - 1);
        return tmp;
    }

    T operator ++ ()
    {
        T tmp = load();
        store(tmp + 1);
        return tmp + 1;
    }

    T operator -- ()
    {
        T tmp = load();
        store(tmp - 1);
        return tmp - 1;
    }

    T operator += (add_type value)
    {
        T tmp = load();
        store(tmp + value);
        return tmp + value;
    }

    T operator -= (add_type value)
    {
        T tmp = load();
        store(tmp - value);
        return tmp - value;
    }

private:
    nvolatile<T>& var_;
    debug_info info_;

    nvolatile_proxy(nvolatile<T>& var, debug_info_param info)
        : var_(var)
        , info_(info)
    {
    }

    T load() const
    {
        return var_.load(mo_acquire, info_);
    }

    void store(T value)
    {
        var_.store(value, mo_release, info_);
    }
};




template<typename T>
class nvolatile : public generic_atomic<T, true>
{
public:
    typedef nvolatile_proxy<T> proxy_t;
    friend class nvolatile_proxy<T>;

    nvolatile()
    {
    }

    explicit nvolatile(T value)
    {
        //??? whether here must be mo_relaxed or mo_release?
        this->store(value, mo_release, $);
    }

    nvolatile(nvolatile const& r)
    {
        T const value = r.load(mo_acquire, $);
        //??? whether here must be mo_relaxed or mo_release?
        this->store(value, mo_release, $);
    }

    nvolatile(proxy_t const& r)
    {
        T const value = r.var_.load(mo_acquire, r.info_);
        //??? whether here must be mo_relaxed or mo_release?
        this->store(value, mo_release, r.info_);
    }

    proxy_t operator () (debug_info_param info)
    {
        return proxy_t(*this, info);
    }
};



}

#endif
