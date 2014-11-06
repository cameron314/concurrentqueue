/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_SLAB_ALLOCATOR_HPP
#define RL_SLAB_ALLOCATOR_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"


namespace rl
{


template<typename type>
class slab_allocator : nocopy<>
{
public:
    slab_allocator()
        : freelist_()
        , blocks_()
        , alloc_count_()
    {
    }

    ~slab_allocator()
    {
        char* pos = blocks_;
        while (pos)
        {
            char* const next = *reinterpret_cast<char**>(pos);
            ::free(pos);
            pos = next;
        }
    }

    type* alloc(void* ctx = 0)
    {
        if (freelist_)
        {
            type* p = freelist_;
            freelist_ = *reinterpret_cast<type**>(p);
            alloc_count_ += 1;
            *(void**)p = ctx;
            type* pp = reinterpret_cast<type*>((reinterpret_cast<void**>(p) + 1));
            return pp;
        }
        else
        {
            return alloc_batch();
        }
    }

    void free(type* p)
    {
        type** pos = reinterpret_cast<type**>((reinterpret_cast<void**>(p) - 1));
        pos[0] = freelist_;
        freelist_ = reinterpret_cast<type*>(pos);
        alloc_count_ -= 1;
    }

    bool iteration_end()
    {
#ifndef RL_GC
        return alloc_count_ == 0;
#else
        freelist_ = 0;
        size_t elem_size = sizeof(void*) + sizeof(type);
        elem_size = (elem_size + 15) & ~15;
        char* pos = blocks_;
        while (pos)
        {
            char* p = pos;
            p += elem_size;
            for (size_t i = 0; i != batch_size; ++i)
            {
                *reinterpret_cast<type**>(p) = freelist_;
                freelist_ = reinterpret_cast<type*>(p);
                p += elem_size;
            }
            pos = *reinterpret_cast<char**>(pos);
        }
        return true;
#endif
    }

    void output_allocs(std::ostream& stream)
    {
        size_t elem_size = sizeof(void*) + sizeof(type);
        elem_size = (elem_size + 15) & ~15;
        set<void*>::type allocs;
        char* pos = blocks_;
        while (pos)
        {
            char* p = pos;
            p += elem_size;
            for (size_t i = 0; i != batch_size; ++i)
            {
                allocs.insert(p);
                p += elem_size;
            }
            pos = *reinterpret_cast<char**>(pos);
        }
        set<void*>::type avail;
        type* pos2 = freelist_;
        while (pos2)
        {
            avail.insert(pos2);
            pos2 = *reinterpret_cast<type**>(pos2);
        }
        vector<void*>::type diff;
        std::set_difference(allocs.begin(), allocs.end(), avail.begin(), avail.end(), std::back_inserter(diff));
        for (size_t i = 0; i != diff.size(); ++i)
        {
            stream << *(void**)diff[i] << std::endl;
        }
    }

private:
    static size_t const batch_size = 128;
    type* freelist_;
    char* blocks_;
    size_t alloc_count_;

    RL_NOINLINE type* alloc_batch()
    {
        size_t elem_size = sizeof(void*) + sizeof(type);
        elem_size = (elem_size + 15) & ~15;
        char* const batch = (char*)(::malloc)(elem_size * (batch_size + 1));
        if (0 == batch)
            throw std::bad_alloc();
        *reinterpret_cast<char**>(batch) = blocks_;
        blocks_ = batch;
        char* p = batch;
        p += elem_size;
        for (size_t i = 0; i != batch_size; ++i)
        {
            *reinterpret_cast<type**>(p) = freelist_;
            freelist_ = reinterpret_cast<type*>(p);
            p += elem_size;
        }
        return alloc();
    }
};


}

#endif
