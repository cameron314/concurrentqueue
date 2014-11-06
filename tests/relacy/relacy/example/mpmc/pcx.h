#pragma once

#include <intrin.h>
#pragma intrinsic (_InterlockedExchangeAdd)
#pragma intrinsic (_InterlockedCompareExchange)

//#define PCX_DEBUG

#ifdef PCX_DEBUG
#include <sstream>
#include <windows.h>
#endif


namespace rl
{

size_t const cacheline_size = 64;

struct pcx_node
{
    typedef void            (*pcx_dtor_t)(pcx_node*);
    ATOMIC(pcx_node*)       pcx_next_;
    ATOMIC(pcx_dtor_t)      pcx_dtor_;
};

namespace pcx_int
{
    unsigned const word_bits = 32;
    unsigned const collector_bits = 4;
    unsigned const collector_count = 1 << collector_bits;
    unsigned const counter_inc = 1 << (collector_bits * 2);
    unsigned const is_current_inc = 1;
    unsigned const back_link_inc = 2;

    struct master;
    struct collector;

    struct local_collector
    {
        pcx_node*               defer_head_;
        pcx_node                defer_tail_;
        unsigned                defer_size_;
    };

    struct thread_int
    {
        pcx_int::master*        master_;
        pcx_int::collector*     collectors_;
        unsigned                recursion_count_;
        unsigned                is_acquired_;
        unsigned                collector_index_;
        unsigned                last_seen_collector_index_;
        unsigned                flush_tail_;
        pcx_node*               defer_head_;
        pcx_node                defer_tail_;
        unsigned                defer_size_;
        unsigned                promote_;
        local_collector         local_collectors_ [collector_count];
    };
}

class pcx_thread : private pcx_int::thread_int
{
public:
    static pcx_thread& get();

    void acquire();
    void release();
    void defer(pcx_node* node, pcx_node::pcx_dtor_t dtor);
    void flush();
    void promote();
    void quiescent();

    void init();
    void deinit();

private:
    unsigned acquire_impl();
    void release_impl(unsigned, unsigned);
    void flush_impl();
    void local_flush();
    void quiescent_impl();
    friend void init();
    friend void deinit();
    friend void thread_callback(bool);
};

namespace pcx_int
{
    struct master
    {
        char                pad0_ [64];

        unsigned            garbage_threshold_;

        char                pad1_ [64];

        struct state_part
        {
            unsigned        current_collector_ : collector_bits;
            unsigned        collector_tail_ : collector_bits;
            unsigned        outer_counter_ : word_bits - 2 * collector_bits;
        };

        union state
        {
             long           whole_;
             state_part     part_;
        };

        state               state_;

        char                pad2_ [64];

        state               state_copy_;

        char                pad3_ [64];
    };

    struct collector
    {
        char                pad0_ [64];

        pcx_node*           defer_list_head_;
        unsigned            defer_list_size_;

        char                pad1_ [64];

        struct state_part
        {
            unsigned        is_current_ : 1;
            unsigned        back_link_ : 1;
            unsigned        pad_ : collector_bits * 2 - 2;
            unsigned        inner_counter_ : word_bits - 2 * collector_bits;
        };

        union state
        {
             long           whole_;
             state_part     part_;
        };

        state               state_;

        char                pad2_ [64];
    };

    __declspec(selectany)
    master                  g_master;
    __declspec(selectany)
    collector               g_collectors [collector_count];
    __declspec(selectany, thread)
    thread_int*             g_thread_instance;

    typedef void (__stdcall nt_tls_cb_t)(void*, unsigned long, void*);
    nt_tls_cb_t on_tls_callback;

    #pragma data_seg(push, old_seg)
    #pragma data_seg(".CRT$XLB")
    __declspec(selectany, dllexport)
    nt_tls_cb_t* volatile p_thread_callback = on_tls_callback;
    #pragma data_seg(pop, old_seg)

    inline void __stdcall on_tls_callback(void*, unsigned long reason, void*)
    {
        if (1 == reason)
        {
            init();
            thread_callback(true);
        }
        else if (0 == reason)
        {
            thread_callback(false);
            deinit();
        }
        if (2 == reason)
        {
            thread_callback(true);
        }
        else if (3 == reason)
        {
            thread_callback(false);
        }
    }
}

inline void init()
{
    using namespace pcx_int;
    master& m = g_master;
    m.garbage_threshold_ = 128;
    m.state_.part_.current_collector_ = 0;
    m.state_.part_.collector_tail_ = 0;
    m.state_.part_.outer_counter_ = 0;
    m.state_copy_.part_.current_collector_ = 0;
    m.state_copy_.part_.collector_tail_ = 0;
    m.state_copy_.part_.outer_counter_ = 0;
    for (unsigned i = 0; i != collector_count; ++i)
    {
        collector& c = g_collectors[i];
        c.defer_list_head_ = 0;
        c.defer_list_size_ = 0;
        c.state_.part_.is_current_ = 1;
        c.state_.part_.back_link_ = 1;
        c.state_.part_.inner_counter_ = 0;
    }
    g_collectors[0].state_.part_.back_link_ = 0;
}

inline void deinit()
{
    using namespace pcx_int;
    pcx_thread::get().release_impl(g_master.state_.part_.current_collector_, is_current_inc);
}

inline void thread_callback(bool init)
{
    if (init)
    {
        g_thread_instance = RL_NEW pcx_thread ();
        pcx_thread::get().init();
    }
    else
    {
        pcx_thread::get().deinit();
        RL_DELETE(g_thread_instance);
        g_thread_instance = 0;
    }
}

inline pcx_thread& pcx_thread::get()
{
    return static_cast<pcx_thread&>(*pcx_int::g_thread_instance);
}

inline unsigned pcx_thread::acquire_impl()
{
    using namespace pcx_int;
    long const prev =
        _InterlockedExchangeAdd(
            &master_->state_.whole_, counter_inc);
    master::state_part u = {prev};

#ifdef PCX_DEBUG
    std::ostringstream ss;
    ss << "[PCX] thread " << this << " acquire " << u.current_collector_ << "\n";
    OutputDebugStringA(ss.str().c_str());
#endif

    if (u.current_collector_ == flush_tail_
        && local_collectors_[flush_tail_].defer_size_)
    {
        local_flush();
    }

    return u.current_collector_;
}

inline void pcx_thread::release_impl(unsigned index, unsigned count)
{
    using namespace pcx_int;
    collector& c = collectors_[index];
    unsigned const prev =
        _InterlockedExchangeAdd(
            &c.state_.whole_, (unsigned)-(int)count);

#ifdef PCX_DEBUG
    std::ostringstream ss;
    ss << "[PCX] thread " << this << " release " << index << "\n";
    OutputDebugStringA(ss.str().c_str());
#endif

    if (0 == prev - count)
    {
        pcx_node* curr = c.defer_list_head_;
        while (curr)
        {
            pcx_node* next = curr->pcx_next_;
            curr->pcx_dtor_(curr);
            curr = next;
        }
        c.defer_list_head_ = 0;
        c.defer_list_size_ = 0;
        c.state_.part_.back_link_ = 1;
        c.state_.part_.is_current_ = 1;

        long u;
        if (index != collector_count - 1)
            u = collector_count;
        else
            u = -(long)(collector_count * (collector_count - 1));
        _InterlockedExchangeAdd(&master_->state_.whole_, u);

        release_impl((index + 1) % collector_count, back_link_inc);
    }
}

inline void pcx_thread::flush_impl()
{
    using namespace pcx_int;
    _mm_mfence();
    master::state state = master_->state_;
    last_seen_collector_index_ = state.part_.current_collector_;
    collector& gc = collectors_[state.part_.current_collector_];
    local_collector& lc = local_collectors_[state.part_.current_collector_];
    lc.defer_head_->pcx_next_ = defer_tail_.pcx_next_;
    lc.defer_head_ = defer_tail_.pcx_next_;
    lc.defer_size_ += defer_size_;
    defer_head_ = &defer_tail_;
    defer_tail_.pcx_next_ = 0;
    defer_size_ = 0;
    if (master_->garbage_threshold_ < lc.defer_size_ || promote_)
    {
        master::state cmp;
        master::state val;
        do
        {
            cmp = master_->state_;
            if (cmp.part_.current_collector_ != last_seen_collector_index_)
            {
                promote_ = 0;
                return;
            }
            unsigned next_index = (last_seen_collector_index_ + 1) % collector_count;
            if (cmp.part_.collector_tail_ == next_index)
                return;
            val = cmp;
            val.part_.current_collector_ += 1;
            val.part_.outer_counter_ = 0;
        }
        while (cmp.whole_ != _InterlockedCompareExchange(
          (long*)&master_->state_.whole_, val.whole_, cmp.whole_));
        last_seen_collector_index_ = val.part_.current_collector_;
        promote_ = 0;
        _InterlockedIncrement((long*)&master_->state_copy_.whole_);
        _InterlockedExchangeAdd((long*)&gc.state_.whole_,
            cmp.part_.outer_counter_ * counter_inc - is_current_inc);
    }
}

__declspec(noinline)
inline void pcx_thread::local_flush()
{
    using namespace pcx_int;
    if (flush_tail_ == master_->state_.part_.collector_tail_)
        return;

#ifdef PCX_DEBUG
    std::ostringstream ss;
    ss << "[PCX] thread " << this << " flush   " << flush_tail_ << "\n";
    OutputDebugStringA(ss.str().c_str());
#endif

    local_collector& lc = local_collectors_[flush_tail_];
    pcx_node* curr = lc.defer_tail_.pcx_next_;
    while (curr)
    {
#ifdef PCX_DEBUG
    std::ostringstream ss;
    ss << "[PCX] thread " << this << " destroy " << curr << "\n";
    OutputDebugStringA(ss.str().c_str());
#endif

        pcx_node* next = curr->pcx_next_;
        curr->pcx_dtor_(curr);
        curr = next;
    }
    lc.defer_head_ = &lc.defer_tail_;
    lc.defer_tail_.pcx_next_ = 0;
    lc.defer_size_ = 0;
    flush_tail_ = (flush_tail_ + 1) % collector_count;
}

__declspec(noinline)
inline void pcx_thread::quiescent_impl()
{
    using namespace pcx_int;
    if (defer_size_)
        flush_impl();
    release_impl(collector_index_, counter_inc);
    collector_index_ = acquire_impl();
}

inline void pcx_thread::acquire()
{
    using namespace pcx_int;
    recursion_count_ += 1;
    if (1 != recursion_count_)
        return;
    if (is_acquired_)
        return;
    collector_index_ = acquire_impl();
    last_seen_collector_index_ = collector_index_;
    is_acquired_ = 1;
}

inline void pcx_thread::release()
{
    using namespace pcx_int;
    recursion_count_ -= 1;
    if (0 == recursion_count_)
    {
        if (master_->state_copy_.part_.current_collector_ != collector_index_
            || promote_)
        {
            if (defer_size_)
                flush_impl();
            release_impl(collector_index_, counter_inc);
            is_acquired_ = 0;
        }
    }
    if (flush_tail_ != last_seen_collector_index_)
    {
        local_flush();
    }
}

inline void pcx_thread::quiescent()
{
    if (master_->state_copy_.part_.current_collector_ != collector_index_
        || promote_)
    {
        quiescent_impl();
    }
    if (flush_tail_ != last_seen_collector_index_)
    {
        local_flush();
    }
}

inline void pcx_thread::defer(pcx_node* node, pcx_node::pcx_dtor_t dtor)
{
    using namespace pcx_int;
    node->pcx_next_ = 0;
    node->pcx_dtor_ = dtor;
    defer_head_->pcx_next_ = node;
    defer_head_ = node;
    defer_size_ += 1;
}

inline void pcx_thread::flush()
{
    using namespace pcx_int;
    if (recursion_count_)
        return;
    if (0 == is_acquired_)
        return;
    if (defer_size_)
        flush_impl();
    release_impl(collector_index_, counter_inc);
    is_acquired_ = 0;
}

inline void pcx_thread::promote()
{
    promote_ = 1;
}

inline void pcx_thread::init()
{
    using namespace pcx_int;
    master_ = &g_master;
    collectors_ = g_collectors;
    defer_head_ = &defer_tail_;
    defer_tail_.pcx_next_ = 0;
    for (unsigned i = 0; i != collector_count; ++i)
    {
        local_collectors_[i].defer_head_ = &local_collectors_[i].defer_tail_;
    }
}

inline void pcx_thread::deinit()
{
    flush();
}

}



