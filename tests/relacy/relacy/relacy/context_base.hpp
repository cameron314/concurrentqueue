/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_CONTEXT_BASE_HPP
#define RL_CONTEXT_BASE_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "history.hpp"
#include "memory.hpp"
#include "test_result.hpp"
#include "slab_allocator.hpp"
#include "test_params.hpp"
#include "random.hpp"
#include "foreach.hpp"
#include "thread_base.hpp"
#include "context_addr_hash.hpp"


#ifdef RL_DEBUGBREAK_ON_ASSERT
#    ifdef _MSC_VER
#        define RL_DEBUGBREAK_ON_ASSERT_IMPL {if (IsDebuggerPresent()) __debugbreak();}
#    else
#        define RL_DEBUGBREAK_ON_ASSERT_IMPL {__asm("int3");}
#    endif
#else
#   define RL_DEBUGBREAK_ON_ASSERT_IMPL
#endif

#ifdef RL_DEBUGBREAK_ON_FAILURE
#    ifdef _MSC_VER
#        define RL_DEBUGBREAK_ON_FAILURE_IMPL {if (IsDebuggerPresent()) __debugbreak();}
#    else
#        define RL_DEBUGBREAK_ON_FAILURE_IMPL {__asm("int3");}
#    endif
#else
#   define RL_DEBUGBREAK_ON_FAILURE_IMPL
#endif



namespace rl
{

class thread_info_base;

struct atomic_data {};
struct var_data
{
    virtual void init(thread_info_base& th) = 0;
    virtual bool store(thread_info_base& th) = 0;
    virtual bool load(thread_info_base& th) = 0;
    virtual ~var_data() {} // just to calm down gcc
};

struct generic_mutex_data;
struct condvar_data;
struct sema_data;
struct event_data;


struct user_msg_event
{
    string msg_;

    void output(std::ostream& s) const
    {
        s << msg_;
    }            
};

class context;

template<int fake = 0>
struct context_holder
{
    static context* instance_;

    static long volatile ctx_seq;
};

template<int fake>
long volatile context_holder<fake>::ctx_seq = 0;

class context
    : public thread_local_context_iface
    , public context_addr_hash_iface
    , nocopy<>
{
public:
    static context& instance()
    {
        //!!! disabled for check in operator new RL_VERIFY(context_holder<>::instance_);
        return *context_holder<>::instance_;
    }

    static bool is_instance()
    {
        return context_holder<>::instance_;
    }

    virtual atomic_data* atomic_ctor(void* ctx) = 0;
    virtual void atomic_dtor(atomic_data* data) = 0;

    virtual var_data* var_ctor() = 0;
    virtual void var_dtor(var_data* data) = 0;

    virtual generic_mutex_data* mutex_ctor(bool is_rw, bool is_exclusive_recursive, bool is_shared_recursive, bool failing_try_lock) = 0;
    virtual void mutex_dtor(generic_mutex_data* m) = 0;

    virtual condvar_data* condvar_ctor(bool allow_spurious_wakeups) = 0;
    virtual void condvar_dtor(condvar_data* cv) = 0;

    virtual sema_data* sema_ctor(bool spurious_wakeups, unsigned initial_count, unsigned max_count) = 0;
    virtual void sema_dtor(sema_data* cv) = 0;

    virtual event_data* event_ctor(bool manual_reset, bool initial_state) = 0;
    virtual void event_dtor(event_data* cv) = 0;

    virtual void rl_global_fence() = 0;
    virtual void sched() = 0;
    virtual void yield(unsigned count, debug_info_param info) = 0;
    virtual void fail_test(char const* desc, test_result_e res, debug_info_param info) = 0;
    virtual void rl_until(char const* desc, debug_info_param info) = 0;

    virtual void* alloc(size_t size, bool is_array, debug_info_param info) = 0;
#ifdef RL_GC
    virtual void* alloc(size_t size, bool is_array, void(*dtor)(void*), debug_info_param info) = 0;
#endif
    virtual void free(void* p, bool is_array, debug_info_param info) = 0;

    virtual void* alloc(size_t size) = 0;
    virtual void free(void* p) = 0;
    virtual size_t prev_alloc_size() = 0;
    virtual void set_debug_info(debug_info_param info) = 0;

    virtual void fiber_proc_impl(int thread_index) = 0;

    virtual unpark_reason park_current_thread(bool is_timed,
                                              bool allow_spurious_wakeup,
                                              bool do_switch,
                                              debug_info_param info) = 0;
    virtual void unpark_thread(thread_id_t th, bool do_switch, debug_info_param info) = 0;
    virtual void switch_back(debug_info_param info) = 0;

    virtual void atomic_thread_fence_acquire() = 0;
    virtual void atomic_thread_fence_release() = 0;
    virtual void atomic_thread_fence_acq_rel() = 0;
    virtual void atomic_thread_fence_seq_cst() = 0;

    virtual unsigned rand(unsigned limit, sched_type t) = 0;

    virtual win_waitable_object* create_thread(void*(*fn)(void*), void* ctx) = 0;

    virtual unpark_reason wfmo_park(void** ws,
                                    win_waitable_object** wo,
                                    size_t count,
                                    bool wait_all,
                                    bool is_timed,
                                    debug_info_param info) = 0;
	
    int get_errno();
    void set_errno(int value);

    thread_info_base* threadx_;
    timestamp_t* seq_cst_fence_order_;

    bool invariant_executing;

    RL_INLINE bool collecting_history() const
    {
        return params_.collect_history && false == invariant_executing;
    }

    template<typename event_t>
    void exec_log(debug_info_param info, event_t const& ev);

    void exec_log_msg(debug_info_param info, char const* msg)
    {
        user_msg_event ev = {msg};
        exec_log(info, ev);
    }

    bool is_random_sched() const
    {
        return is_random_sched_;
    }

    unsigned get_ctx_seq() const
    {
        return ctx_seq_;
    }

    void disable_preemption();
    void enable_preemption();

    virtual thread_id_t get_thread_count() const = 0;

    thread_id_t current_thread() const
    {
        return threadx_->index_;
    }

    void iteration_begin()
    {
    }

protected:
    history_mgr history_;
    test_params& params_;
    unsigned disable_preemption_;
    int                         disable_alloc_;

    context(thread_id_t thread_count, test_params& params)
        : history_(*params.output_stream, thread_count)
        , params_(params)
        , disable_alloc_(1)
    {
        RL_VERIFY(0 == context_holder<>::instance_);
        context_holder<>::instance_ = this;

        is_random_sched_ = params_.search_type == random_scheduler_type;

#ifdef _MSC_VER
        ctx_seq_ = _InterlockedExchangeAdd(&context_holder<>::ctx_seq, 1) + 1;
#else
        ctx_seq_ = __sync_fetch_and_add(&context_holder<>::ctx_seq, 1) + 1;
#endif
    }

    virtual ~context()
    {
        RL_VERIFY(this == context_holder<>::instance_);
        context_holder<>::instance_ = 0;
    }
    
private:
    bool is_random_sched_;
    unsigned ctx_seq_;
};


template<int fake>
context* context_holder<fake>::instance_ = 0;




inline context& ctx()
{
    return context::instance();
}

inline bool is_ctx()
{
    return context::is_instance();
}


inline int get_errno()
{
    return ctx().get_errno();
}

inline void set_errno(int value)
{
    return ctx().set_errno(value);
}

class preemption_disabler : nocopy<>
{
public:
    preemption_disabler(context& c)
        : c_(c)
    {
        c_.disable_preemption();
    }

    ~preemption_disabler()
    {
        c_.enable_preemption();
    }

private:
    context& c_;
};


}


#define RL_HIST_IMPL(C, INFO, TYPE) \
    do { \
        if (C.collecting_history()) { \
            rl::debug_info const& rl_info_c = INFO; \
            rl::context& rl_hist_c = C; \
            TYPE ev = \
/**/

#define RL_HIST_END() \
                        ; \
            rl_hist_c.exec_log(rl_info_c, ev); \
        } \
    } while ((void)0, 0) \
/**/

#define RL_HIST_CTX(TYPE) RL_HIST_IMPL((*this), info, TYPE)

#define RL_HIST(TYPE) RL_HIST_IMPL(c, info, TYPE)

#define RL_LOG(desc) rl::ctx().exec_log_msg(RL_INFO, desc)



#ifdef _MSC_VER
#   define RL_ASSERT_IMPL(x, res, str, info) do {if (!((void)0, (x))) {{RL_DEBUGBREAK_ON_ASSERT_IMPL} rl::ctx().fail_test(str, res, info);}} while ((void)0, 0)
#else
#   define RL_ASSERT_IMPL(x, res, str, info) do {if (!((void)0, (x))) rl::ctx().fail_test(str, res, info);} while ((void)0, 0)
#endif
#define RL_ASSERT(x) RL_ASSERT_IMPL(x, rl::test_result_user_assert_failed, "assertion: " #x, RL_INFO)
#define RL_UNTIL(x) do {if ((x)) rl::ctx().rl_until(#x, RL_INFO);} while ((void)0, 0)


#endif
