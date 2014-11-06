/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_VAR_HPP
#define RL_VAR_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "context.hpp"
#include "signature.hpp"
#include "atomic_events.hpp"


namespace rl
{

template<typename T>
class var;



template<typename T>
class var_proxy_const
{
public:
    var_proxy_const(var<T> const& v, debug_info_param info)
        : var_(const_cast<var<T>&>(v))
        , info_(info)
    {
    }

    T load() const
    {
        return var_.load(info_);
    }

    operator T () const
    {
        return this->load();
    }

    T const operator -> () const
    {
        return this->load();
    }

protected:
    var<T>& var_;
    debug_info info_;

private:
    var_proxy_const& operator = (var_proxy_const const&);
};




template<typename T>
class var_proxy : public var_proxy_const<T>
{
public:
    typedef typename atomic_add_type<T>::type add_type;

    var_proxy(var<T>& v, debug_info_param info)
        : var_proxy_const<T>(v, info)
    {
    }

    void store(T value)
    {
        this->var_.store(value, this->info_);
    }

    template<typename Y>
    T operator = (var_proxy_const<Y> const& v)
    {
        Y y = v.load();
        T t = y;
        store(t);
        return t;
    }

    T operator = (var_proxy<T> const& v)
    {
        T t = v.load();
        store(t);
        return t;
    }

    T operator = (T value)
    {
        store(value);
        return value;
    }

    T operator -> ()
    {
        return this->load();
    }

    T operator ++ (int)
    {
        T v = this->load();
        T y = ++v;
        this->store(y);
        return v;
    }

    T operator -- (int)
    {
        T v = this->load();
        T y = --v;
        this->store(y);
        return v;
    }

    T operator ++ ()
    {
        T v = this->load();
        this->store(++v);
        return v;
    }

    T operator -- ()
    {
        T v = this->load();
        this->store(--v);
        return v;
    }

    T operator += (add_type value)
    {
        T v = this->load();
        v += value;
        this->store(v);
        return v;
    }

    T operator -= (add_type value)
    {
        T v = this->load();
        v -= value;
        this->store(v);
        return v;
    }

    T operator &= (T value)
    {
        T v = this->load();
        v &= value;
        this->store(v);
        return v;
    }

    T operator |= (T value)
    {
        T v = this->load();
        v |= value;
        this->store(v);
        return v;
    }

    T operator ^= (T value)
    {
        T v = this->load();
        v ^= value;
        this->store(v);
        return v;
    }
};




template<typename T>
struct var_event
{
    debug_info var_info_;
    var<T> const* var_addr_;
    T value_;
    bool load_;

    template<typename Y>
    struct map_type
    {
        typedef T result;
    };

    template<typename Y>
    struct map_type<Y*>
    {
        typedef void* result;
    };

    void output(std::ostream& s) const
    {
        s << "<" << std::hex << var_addr_ << std::dec << "> "
            << (load_ ? "load" : "store") << ", value=" << (typename map_type<T>::result)value_;
    }
};




template<typename T>
class var
{
public:
    var()
    {
        value_ = 0;
        initialized_ = false;
        data_ = ctx().var_ctor();
    }

    var(T value)
    {
        init(value);
    }

    var(var const& r)
    {
        init(r.load($));
    }

    ~var()
    {
        sign_.check($);
        ctx().var_dtor(data_);
    }

    var_proxy_const<T> operator () (debug_info_param info) const
    {
        return var_proxy_const<T>(*this, info);
    }

    var_proxy<T> operator () (debug_info_param info)
    {
        return var_proxy<T>(*this, info);
    }

private:
    T value_;
    bool initialized_;

    var_data* data_;

    signature<123456789> sign_;
    friend class var_proxy<T>;
    friend class var_proxy_const<T>;

    void init(T value)
    {
        context& c = ctx();
        initialized_ = true;
        value_ = value;
        data_ = ctx().var_ctor();
        data_->init(*c.threadx_);
    }

    T load(debug_info_param info) const
    {
        context& c = ctx();
        sign_.check(info);

        if (false == initialized_)
        {
            RL_HIST(var_event<T>) {RL_INFO, this, T(), true} RL_HIST_END();
            RL_ASSERT_IMPL(false, test_result_unitialized_access, "", info);
        }

        if (false == c.invariant_executing)
        {
            if (false == data_->load(*c.threadx_))
            {
                RL_HIST(var_event<T>) {RL_INFO, this, T(), true} RL_HIST_END();
                RL_ASSERT_IMPL(false, test_result_data_race, "data race detected", info);
            }

            T const v = value_;

            RL_HIST(var_event<T>) {RL_INFO, this, v, true} RL_HIST_END();

            return v;
        }
        else
        {
            return value_;
        }
    }

    void store(T v, debug_info_param info)
    {
        context& c = ctx();
        RL_VERIFY(false == c.invariant_executing);
        sign_.check(info);

        if (initialized_)
        {
            if (false == data_->store(*c.threadx_))
            {
                RL_HIST(var_event<T>) {RL_INFO, this, T(), false} RL_HIST_END();
                RL_ASSERT_IMPL(false, test_result_data_race, "data race detected", info);
            }
        }
        else
        {
            initialized_ = true;
            data_->init(*c.threadx_);
        }

        value_ = v;

        RL_HIST(var_event<T>) {RL_INFO, this, v, false} RL_HIST_END();
    }

    var& operator = (var const& r);
};




template<thread_id_t thread_count>
struct var_data_impl : var_data
{
    typedef thread_info<thread_count> thread_info_t;

    timestamp_t load_acq_rel_timestamp_ [thread_count];
    timestamp_t store_acq_rel_timestamp_ [thread_count];

    var_data_impl()
    {
        foreach<thread_count>(load_acq_rel_timestamp_, assign_zero);
        foreach<thread_count>(store_acq_rel_timestamp_, assign_zero);
    }

    virtual void init(thread_info_base& th)
    {
        th.own_acq_rel_order_ += 1;
        store_acq_rel_timestamp_[th.index_] = th.own_acq_rel_order_;
    }

    virtual bool store(thread_info_base& th)
    {
        for (thread_id_t i = 0; i != thread_count; ++i)
        {
            if (th.acq_rel_order_[i] < store_acq_rel_timestamp_[i])
                return false;
            if (th.acq_rel_order_[i] < load_acq_rel_timestamp_[i])
                return false;
        }

        th.own_acq_rel_order_ += 1;
        store_acq_rel_timestamp_[th.index_] = th.own_acq_rel_order_;
        return true;
    }

    virtual bool load(thread_info_base& th)
    {
        for (thread_id_t i = 0; i != thread_count; ++i)
        {
            if (th.acq_rel_order_[i] < store_acq_rel_timestamp_[i])
                return false;
        }

        th.own_acq_rel_order_ += 1;
        load_acq_rel_timestamp_[th.index_] = th.own_acq_rel_order_;
        return true;
    }

    virtual ~var_data_impl() {} // just to calm down gcc
};



}



#endif
