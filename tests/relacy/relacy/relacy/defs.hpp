/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_DEFS_HPP
#define RL_DEFS_HPP
#ifdef _MSC_VER
#   pragma once
#endif


namespace rl
{

typedef int thread_id_t;
typedef size_t timestamp_t;
typedef uint64_t iteration_t;

size_t const atomic_history_size = 3;
iteration_t const progress_probe_period = 4 * 1024;

size_t const alignment = 16;

class context;
class thread_base;
struct win_waitable_object;

enum sched_type
{
    sched_type_sched,
    sched_type_atomic_load,
    sched_type_cas_fail,
    sched_type_mem_realloc,
    sched_type_user,
};

enum unpark_reason
{
    unpark_reason_normal,
    unpark_reason_timeout,
    unpark_reason_spurious,
};

struct debug_info
{
    char const* func_;
    char const* file_;
    unsigned line_;

    debug_info(char const* func = "", char const* file = "", unsigned line = 0)
        : func_(func)
        , file_(file)
        , line_(line)
    {
    }
};

typedef debug_info const& debug_info_param;

inline void assert_failed(char const* cond, debug_info_param info)
{
    std::cout << "RELACY INTERNAL ASSERT FAILED: '" << cond
    << "' at " << info.file_ << ":" << info.line_ << " (" << info.func_ << ")" << std::endl;
}

template<typename T>
struct raw_allocator : std::allocator<T>
{
    template<class Y>
    struct rebind
    {
        typedef raw_allocator<Y> other;
    };

    template<typename Y>
    raw_allocator(raw_allocator<Y> const&)
    {
    }

    raw_allocator(raw_allocator const& rhs)
        : std::allocator<T>(rhs)
    {
    }

    raw_allocator()
        : std::allocator<T>()
    {
    }

    T* allocate(size_t count, void* = 0)
    {
        return (T*)(::malloc)(count * sizeof(T));
    }

    void deallocate(T* p, size_t)
    {
        (::free)(p);
    }
};


template<typename T>
struct vector
{
    typedef std::vector<T, raw_allocator<T> > type;
};

template<typename T>
struct queue
{
    typedef std::queue<T, std::deque<T, raw_allocator<T> > > type;
};

template<typename T>
struct stack
{
    typedef std::stack<T, std::vector<T, raw_allocator<T> > > type;
};

template<typename T>
struct set
{
    typedef std::set<T, std::less<T>, raw_allocator<T> > type;
};

template<typename T, typename Y>
struct map
{
    typedef std::map<T, Y, std::less<T>, raw_allocator<std::pair<T, Y> > > type;
};

typedef std::basic_string<char, std::char_traits<char>, raw_allocator<char> > string;
typedef std::basic_ostringstream<char, std::char_traits<char>, raw_allocator<char> > ostringstream;
typedef std::basic_istringstream<char, std::char_traits<char>, raw_allocator<char> > istringstream;

}


#endif
