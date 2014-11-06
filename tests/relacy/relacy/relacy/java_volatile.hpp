/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_JAVA_VOLATILE_HPP
#define RL_JAVA_VOLATILE_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "atomic.hpp"


namespace rl
{

template<typename T> class jvolatile;


template<typename T>
class jvolatile_proxy
{
public:
    typedef typename atomic_add_type<T>::type add_type;
    template<typename Y> friend class jvolatile;

    operator T () const
    {
        return load();
    }

    T operator = (T value)
    {
        store(value);
        return value;
    }

    T operator = (jvolatile_proxy const& r)
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
    jvolatile<T>& var_;
    debug_info info_;

    jvolatile_proxy(jvolatile<T>& var, debug_info_param info)
        : var_(var)
        , info_(info)
    {
    }

    T load() const
    {
        return var_.load(mo_seq_cst, info_);
    }

    void store(T value)
    {
        var_.store(value, mo_seq_cst, info_);
    }
};




template<typename T>
class jvolatile : generic_atomic<T, true>
{
public:
    typedef jvolatile_proxy<T> proxy_t;
    friend class jvolatile_proxy<T>;

    jvolatile()
    {
    }

    explicit jvolatile(T value)
    {
        //??? whether here must be mo_relaxed or mo_release?
        this->store(value, mo_seq_cst, $);
    }

    jvolatile(jvolatile const& r)
    {
        T const value = r.load(mo_seq_cst, $);
        //??? whether here must be mo_relaxed or mo_release?
        this->store(value, mo_seq_cst, $);
    }

    jvolatile(proxy_t const& r)
    {
        T const value = r.var_.load(mo_seq_cst, r.info_);
        //??? whether here must be mo_relaxed or mo_release?
        this->store(value, mo_seq_cst, r.info_);
    }

    proxy_t operator () (debug_info_param info)
    {
        return proxy_t(*this, info);
    }
};



}

#endif
