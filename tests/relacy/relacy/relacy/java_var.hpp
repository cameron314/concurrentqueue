/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_JAVA_VAR_HPP
#define RL_JAVA_VAR_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "atomic.hpp"


namespace rl
{

template<typename T> class jvar;


template<typename T>
class jvar_proxy
{
public:
    typedef typename atomic_add_type<T>::type add_type;
    template<typename Y> friend class jvar;

    operator T () const
    {
        return load();
    }

    T operator = (T value)
    {
        store(value);
        return value;
    }

    T operator = (jvar_proxy const& r)
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
    jvar<T>& var_;
    debug_info info_;

    jvar_proxy(jvar<T>& var, debug_info_param info)
        : var_(var)
        , info_(info)
    {
    }

    T load() const
    {
        return var_.load(mo_relaxed, info_);
    }

    void store(T value)
    {
        var_.store(value, mo_relaxed, info_);
    }
};




template<typename T>
class jvar : generic_atomic<T, true>
{
public:
    typedef jvar_proxy<T> proxy_t;
    friend class jvar_proxy<T>;

    jvar()
    {
    }

    jvar(T value)
    {
        this->store(value, mo_relaxed, $);
    }

    jvar(jvar const& r)
    {
        T const value = r.load(mo_relaxed, $);
        this->store(value, mo_relaxed, $);
    }

    jvar(proxy_t const& r)
    {
        T const value = r.load();
        this->store(value, mo_relaxed, r.info_);
    }

    proxy_t operator () (debug_info_param info)
    {
        return proxy_t(*this, info);
    }

private:
    jvar& operator = (jvar const&);
};


}

#endif
