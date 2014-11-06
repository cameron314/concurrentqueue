/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_PLATFORM_HPP
#define RL_PLATFORM_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "pch.hpp"


#if defined(RL_WIN) || defined(_CYGWIN)

typedef void* fiber_t;

inline unsigned get_tick_count()
{
    return GetTickCount();
}

inline void set_low_thread_prio()
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
}

inline void create_main_fiber(fiber_t& fib)
{
    fib = ConvertThreadToFiber(0);
    if (0 == fib)
    {
        unsigned long err = ::GetLastError(); (void)err;
        throw std::logic_error("you must start simulation inside a thread (not a fiber)");
    }
}

inline void delete_main_fiber(fiber_t& fib)
{
    (void)fib;
    HMODULE lib = LoadLibraryW(L"kernel32.dll");
    if (lib)
    {
        void* proc = (void*)GetProcAddress(lib, "ConvertFiberToThread");
        if (proc)
        {
            typedef BOOL (WINAPI * ConvertFiberToThreadT)();
            ConvertFiberToThreadT ConvertFiberToThread = (ConvertFiberToThreadT)proc;
            ConvertFiberToThread();
        }
        FreeLibrary(lib);
    }
}

inline void create_fiber(fiber_t& fib, void(*fiber_proc)(void*), void* ctx)
{
    size_t const stack_size = 64*1024;
    fib = CreateFiberEx(4*1024, stack_size, 0, (LPFIBER_START_ROUTINE)fiber_proc, ctx);
    if (fib == 0)
        throw std::runtime_error("error creating fiber");
}

inline void delete_fiber(fiber_t& fib)
{
    DeleteFiber(fib);
}

inline void switch_to_fiber(fiber_t fib, fiber_t)
{
    SwitchToFiber(fib);
}

// work-around for some versions of cygwin
extern "C" inline int __gxx_personality_v0()
{
    return 0;
}

#ifdef RL_WIN
#else

/*
inline unsigned get_tick_count()
{
    return GetTickCount();
}

typedef void* fiber_t;

struct ucontext_t
{
    struct stack_t
    {
        void* ss_sp;
        size_t ss_size;
    };
    stack_t uc_stack;
    void* uc_link;

};
void getcontext(void*) {}
void makecontext(void*, void(*)(), int, void*) {}
void swapcontext(void*, void*) {}

*/

#endif

#else

inline unsigned get_tick_count()
{
    struct tms tms;
    return ((unsigned)(times (&tms) * (1000 / sysconf(_SC_CLK_TCK))));
}

inline void set_low_thread_prio()
{
}

#if 0

typedef ucontext_t fiber_t;

inline void create_main_fiber(fiber_t& fib)
{
    ucontext_t f = {};
    fib = f;
}

inline void delete_main_fiber(fiber_t& fib)
{
    (void)fib;
}

inline void create_fiber(fiber_t& fib, void(*fiber_proc)(void*), void* ctx)
{
    size_t const stack_size = 64*1024;
    getcontext(&fib);
    fib.uc_stack.ss_sp = (::malloc)(stack_size);
    fib.uc_stack.ss_size = stack_size;
    fib.uc_link = 0;
    typedef void(*fn_t)();
    fn_t fn = (fn_t)fiber_proc;
    makecontext(&fib, fn, 1, ctx);
}

inline void delete_fiber(fiber_t& fib)
{
    //(::free)(fib.uc_stack.ss_sp);
}

inline void switch_to_fiber(fiber_t& fib, fiber_t& prev)
{
    swapcontext(&prev, &fib);
}

#else

struct fiber_t
{
    ucontext_t  fib;
    jmp_buf     jmp;
};

struct fiber_ctx_t
{
    void(*      fnc)(void*);
    void*       ctx;
    jmp_buf*    cur;
    ucontext_t* prv;
};

static void fiber_start_fnc(void* p)
{
    fiber_ctx_t* ctx = (fiber_ctx_t*)p;
    void (*volatile ufnc)(void*) = ctx->fnc;
    void* volatile uctx = ctx->ctx;
    if (_setjmp(*ctx->cur) == 0)
    {
        ucontext_t tmp;
        swapcontext(&tmp, ctx->prv);
    }
    ufnc(uctx);
}

inline void create_main_fiber(fiber_t& fib)
{
    memset(&fib, 0, sizeof(fib));
}

inline void delete_main_fiber(fiber_t& fib)
{
    (void)fib;
}

inline void create_fiber(fiber_t& fib, void(*ufnc)(void*), void* uctx)
{
    size_t const stack_size = 64*1024;
    getcontext(&fib.fib);
    fib.fib.uc_stack.ss_sp = (::malloc)(stack_size);
    fib.fib.uc_stack.ss_size = stack_size;
    fib.fib.uc_link = 0;
    ucontext_t tmp;
    fiber_ctx_t ctx = {ufnc, uctx, &fib.jmp, &tmp};
    makecontext(&fib.fib, (void(*)())fiber_start_fnc, 1, &ctx);
    swapcontext(&tmp, &fib.fib);
}

inline void delete_fiber(fiber_t& fib)
{
    //(::free)(fib.uc_stack.ss_sp);
}

inline void switch_to_fiber(fiber_t& fib, fiber_t& prv)
{
    if (_setjmp(prv.jmp) == 0)
        _longjmp(fib.jmp, 1);
}

#endif

#endif



#ifdef _MSC_VER
    typedef unsigned __int64 uint64_t;
#   define RL_INLINE __forceinline
#   define RL_NOINLINE __declspec(noinline)
#   define RL_STRINGIZE(text) RL_STRINGIZE_A((text))
#   define RL_STRINGIZE_I(text) #text
#   define RL_STRINGIZE_A(arg) RL_STRINGIZE_I arg
#   define RL_STDCALL __stdcall
#   define RL_THROW_SPEC(ex)
#else
#   define RL_INLINE inline
#   define RL_NOINLINE
#   define RL_STRINGIZE_I(text) #text
#   define RL_STRINGIZE(text) RL_STRINGIZE_I(text)
#   define RL_STDCALL
#   define RL_THROW_SPEC(ex) throw(ex)
#endif


#if defined (_MSC_VER) && (_MSC_VER >= 1400)
#   define RL_RESTRICT __restrict
#else
#   define RL_RESTRICT
#endif



#endif
