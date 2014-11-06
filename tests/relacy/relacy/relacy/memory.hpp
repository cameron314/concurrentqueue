/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_MEMORY_HPP
#define RL_MEMORY_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"


namespace rl
{


class memory_mgr : nocopy<>
{
public:
    memory_mgr()
    {
        memset(deferred_free_, 0, sizeof(deferred_free_));
        memset(deferred_free_size_, 0, sizeof(deferred_free_size_));
        deferred_index_ = 0;
    }

    ~memory_mgr()
    {
        /*
        while (allocs_.size())
        {
            size_t* p = (size_t*)(allocs_.begin()->first);
            free(p - 1, false);
            allocs_.erase(allocs_.begin());
        }
        */
    }

#ifndef RL_GC
    void* alloc(size_t size)
#else
    void* alloc(size_t size, void (*dtor)(void*))
#endif
    {
        void* pp = 0;
        for (size_t i = 0; i != alloc_cache_.size(); ++i)
        {
            if (alloc_cache_[i].first == size)
            {
                if (alloc_cache_[i].second.size())
                {
                    pp = alloc_cache_[i].second.top();
                    alloc_cache_[i].second.pop();
                }
                break;
            }
        }
        if (0 == pp)
            pp = (::malloc)(size + alignment);

        if (pp)
        {
            RL_VERIFY(alignment >= sizeof(void*));
            *(size_t*)pp = size;
            void* p = (char*)pp + alignment;
#ifndef RL_GC
            allocs_.insert(std::make_pair(p, size));
#else
            alloc_desc_t desc = {p, size, dtor};
            gc_allocs_.push_back(desc);
#endif
            return p;
        }
        else
        {
            throw std::bad_alloc();
        }
    }

    bool free(void* pp, bool defer)
    {
        if (0 == pp)
            return true;

#ifndef RL_GC
        map<void*, size_t>::type::iterator iter = allocs_.find(pp);
        if (allocs_.end() == iter)
            return false;

        allocs_.erase(iter);

        void* p = (char*)pp - alignment;
        size_t size = *(size_t*)p;

        if (defer)
        {
            deferred_free_[deferred_index_ % deferred_count] = p;
            deferred_free_size_[deferred_index_ % deferred_count] = size;
            deferred_index_ += 1;
            p = deferred_free_[deferred_index_ % deferred_count];
            size = deferred_free_size_[deferred_index_ % deferred_count];
            if (p)
                rl_free_impl(p, size);
        }
        else
        {
            rl_free_impl(p, size);
        }
        return true;
#else
        (void)defer;
        for (size_t i = 0; i != gc_allocs_.size(); ++i)
        {
            alloc_desc_t const& desc = gc_allocs_[i];
            if (desc.addr == pp)
            {
                void* p = (char*)desc.addr - alignment;
                rl_free_impl(p, desc.size);
                gc_allocs_.erase(gc_allocs_.begin() + i);
                return true;
            }
        }
        return false;
#endif
    }

    bool iteration_end()
    {
#ifndef RL_GC
        return allocs_.empty();
#else
        for (size_t i = 0; i != gc_allocs_.size(); ++i)
        {
            alloc_desc_t const& desc = gc_allocs_[i];
            if (desc.dtor)
                desc.dtor(desc.addr);
            void* p = (char*)desc.addr - alignment;
            rl_free_impl(p, desc.size);
        }
        gc_allocs_.clear();
        return true;
#endif
    }

#ifndef RL_GC
    void output_allocs(std::ostream& stream)
    {
        stream << "memory allocations:" << std::endl;
        map<void*, size_t>::type::iterator iter = allocs_.begin();
        map<void*, size_t>::type::iterator end = allocs_.end();
        for (; iter != end; ++iter)
        {
            stream << iter->first << " [" << (unsigned)iter->second << "]" << std::endl;
        }
        stream << std::endl;
    }
#endif

private:
    typedef stack<void*>::type              freelist_t;
    typedef std::pair<size_t, freelist_t>   alloc_entry_t;
    typedef vector<alloc_entry_t>::type     alloc_t;

    static size_t const deferred_count      = 64;

    alloc_t alloc_cache_;
    size_t deferred_index_;
    void* deferred_free_ [deferred_count];
    size_t deferred_free_size_ [deferred_count];

#ifndef RL_GC
    map<void*, size_t>::type allocs_;
#else
    struct alloc_desc_t
    {
        void*       addr;
        size_t      size;
        void        (*dtor)(void*);
    };
    vector<alloc_desc_t>::type gc_allocs_;
#endif

    void rl_free_impl(void* p, size_t size)
    {
        bool found = false;
        for (size_t i = 0; i != alloc_cache_.size(); ++i)
        {
            if (alloc_cache_[i].first == size)
            {
                found = true;
                alloc_cache_[i].second.push(p);
                break;
            }
        }
        if (!found)
        {
            alloc_cache_.push_back(std::make_pair(size, freelist_t()));
            alloc_cache_.back().second.push(p);
        }
    }
};




struct memory_alloc_event
{
    void*                       addr_;
    size_t                      size_;
    bool                        is_array_;

    void output(std::ostream& s) const
    {
        s << "memory allocation: addr=" << std::hex << (void*)((char*)addr_ + (is_array_ ? alignment : 0)) << std::dec
            << ", size=" << (unsigned)size_;
    }
};


struct memory_free_event
{
    void*                       addr_;
    bool                        is_array_;

    void output(std::ostream& s) const
    {
        s << "memory deallocation: addr=" << std::hex << (void*)((char*)addr_ + (is_array_ ? alignment : 0)) << std::dec;
    }
};



}

#endif
