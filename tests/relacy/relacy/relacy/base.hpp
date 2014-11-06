/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_BASE_HPP
#define RL_BASE_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "pch.hpp"
#include "platform.hpp"

namespace rl
{
size_t const subsequent_timed_wait_limit = 4;
}

#define RL_TEST

#ifdef RL_JAVA_MODE
#   define RL_GC
#   define RL_NO_MALLOC
#   define RL_JAVA_API
#   define RL_JAVA_MM
#endif

#ifdef RL_CLI_MODE
#   define RL_GC
#   define RL_NO_MALLOC
#   define RL_CLI_API
#   define RL_CLI_MM
#endif

#ifdef RL_POSIX_MODE
#   define RL_POSIX_API
#endif

#ifdef RL_WIN_MODE
#   define RL_WIN_API
#endif

#ifdef RL_CPP_MODE
#   define RL_CPP_API
#   define RL_CPP_MM
#endif

#if defined(RL_JAVA_MM) || defined(RL_CLI_MM)
#   define RL_IMPROVED_SEQ_CST_FENCE
#   define RL_IMPROVED_SEQ_CST_RMW
#endif

namespace rl
{

#define RL_NOCOPY(CLASS) \
    private: \
    CLASS(CLASS const&); \
    CLASS& operator = (CLASS const&);
/**/


template<typename T = void>
class nocopy
{
    nocopy(nocopy const&);
    nocopy& operator = (nocopy const&);

protected:
    nocopy() {}
};


template<size_t sz, size_t base = 4>
struct align_pad
{
    template<bool perfect, bool fit, int fake> struct helper
    {
        struct type { char pad [base - sz]; };
    };

    template<int fake> struct helper<true, true, fake>
    {
        struct type {};
    };

    template<bool perfect, int fake> struct helper<perfect, false, fake>
    {
        typedef typename align_pad<sz, base * 2>::type type;
    };

    typedef typename helper<sz == base, sz <= base, 0>::type type;
};


template<typename T>
struct aligned : T, align_pad<sizeof(T)>::type
{};

template<typename T>
T val(T x)
{
    return x;
}

}


#include "defs.hpp"


#define RL_INFO ::rl::debug_info(__FUNCTION__, __FILE__, __LINE__)
#define $ RL_INFO


#ifdef RL_DO_ASSERT
#   if RL_DO_ASSERT
#       define RL_DO_ASSERT_IMPL
#   endif
#else
#   ifdef _DEBUG
#       define RL_DO_ASSERT_IMPL
#   endif
#endif

#ifdef _MSC_VER
#   define RL_INT3() __debugbreak(); abort()
#else
#   define RL_INT3() abort()
#endif

#ifdef RL_DO_ASSERT_IMPL
#   define RL_VERIFY(x) do { if (!((void)0, (x))) { \
        ::rl::assert_failed(#x, $); RL_INT3(); } } while ((void)0, 0)
#else
#   define RL_VERIFY(x) (void)0
#endif

#endif
