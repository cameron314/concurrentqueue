/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_JAVA_ATOMIC_HPP
#define RL_JAVA_ATOMIC_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "atomic.hpp"


namespace rl
{


template<typename T> class jatomic;


template<typename T>
class jatomic_proxy
{
public:
    T get() const
    {
        return var_.load(mo_seq_cst, info_);
    }

    void set(T value)
    {
        var_.store(value, mo_seq_cst, info_);
    }

    T addAndGet(T delta)
    {
        return getAndAdd(delta) + delta;
    }

    bool compareAndSet(T expect, T update)
    {
        bool result = var_.compare_exchange(bool_t<false>(), expect, update, mo_seq_cst, info_);
        return result;
    }

    bool weakCompareAndSet(T expect, T update)
    {
        bool result = var_.compare_exchange(bool_t<true>(), expect, update, mo_seq_cst, info_);
        return result;
    }

    T decrementAndGet()
    {
        return getAndAdd(-1) - 1;
    }

    T getAndAdd(T delta)
    {
        T result = var_.rmw(rmw_type_t<rmw_type_add>(), delta, mo_seq_cst, info_);
        return result;
    }

    T getAndDecrement()
    {
        return getAndAdd(-1);
    }

    T getAndIncrement()
    {
        return getAndAdd(+1);
    }

    T getAndSet(T newValue)
    {
        T result = var_.rmw(rmw_type_t<rmw_type_swap>(), newValue, mo_seq_cst, info_);
        return result;
    }

    T incrementAndGet()
    {
        return getAndAdd(1) + 1;
    }

private:
    jatomic<T>& var_;
    debug_info info_;

    //typedef typename atomic_add_type<T>::type add_type;
    template<typename Y> friend class jatomic;

    jatomic_proxy(jatomic<T>& var, debug_info_param info)
        : var_(var)
        , info_(info)
    {
    }

    jatomic_proxy& operator = (jatomic_proxy const&);
};


template<typename T>
class jatomic : generic_atomic<T, true>
{
public:
    typedef jatomic_proxy<T> proxy_t;
    friend class jatomic_proxy<T>;

    jatomic()
    {
    }

    jatomic(T value)
    {
        //??? whether here must be mo_relaxed or mo_release?
        this->store(value, mo_seq_cst, $);
    }

    jatomic(jatomic const& r)
    {
        T const value = r.load(mo_seq_cst, $);
        //??? whether here must be mo_relaxed or mo_release?
        this->store(value, mo_seq_cst, $);
    }

    jatomic(proxy_t const& r)
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


typedef jatomic<int> AtomicInteger;
typedef jatomic<long> AtomicLong;






}

#endif
