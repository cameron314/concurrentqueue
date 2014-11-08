/*
    Copyright 2005-2014 Intel Corporation.  All Rights Reserved.

    This file is part of Threading Building Blocks. Threading Building Blocks is free software;
    you can redistribute it and/or modify it under the terms of the GNU General Public License
    version 2  as  published  by  the  Free Software Foundation.  Threading Building Blocks is
    distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
    implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See  the GNU General Public License for more details.   You should have received a copy of
    the  GNU General Public License along with Threading Building Blocks; if not, write to the
    Free Software Foundation, Inc.,  51 Franklin St,  Fifth Floor,  Boston,  MA 02110-1301 USA

    As a special exception,  you may use this file  as part of a free software library without
    restriction.  Specifically,  if other files instantiate templates  or use macros or inline
    functions from this file, or you compile this file and link it with other files to produce
    an executable,  this file does not by itself cause the resulting executable to be covered
    by the GNU General Public License. This exception does not however invalidate any other
    reasons why the executable file might be covered by the GNU General Public License.
*/

#ifndef __TBB_machine_msvc_ia32_common_H
#define __TBB_machine_msvc_ia32_common_H

#include <intrin.h>

//TODO: consider moving this macro to tbb_config.h and used there MSVC asm is used
#if  !_M_X64 || __INTEL_COMPILER
    #define __TBB_X86_MSVC_INLINE_ASM_AVAILABLE 1

    #if _M_X64
        #define __TBB_r(reg_name) r##reg_name
    #else
        #define __TBB_r(reg_name) e##reg_name
    #endif
#else
    //MSVC in x64 mode does not accept inline assembler
    #define __TBB_X86_MSVC_INLINE_ASM_AVAILABLE 0
#endif

#define __TBB_NO_X86_MSVC_INLINE_ASM_MSG "The compiler being used is not supported (outdated?)"

#if (_MSC_VER >= 1300) || (__INTEL_COMPILER) //Use compiler intrinsic when available
    #define __TBB_PAUSE_USE_INTRINSIC 1
    #pragma intrinsic(_mm_pause)
    namespace tbb { namespace internal { namespace intrinsics { namespace msvc {
        static inline void __TBB_machine_pause (uintptr_t delay ) {
            for (;delay>0; --delay )
                _mm_pause();
        }
    }}}}
#else
    #if !__TBB_X86_MSVC_INLINE_ASM_AVAILABLE
        #error __TBB_NO_X86_MSVC_INLINE_ASM_MSG
    #endif

    namespace tbb { namespace internal { namespace inline_asm { namespace msvc {
        static inline void __TBB_machine_pause (uintptr_t delay ) {
            _asm
            {
                mov __TBB_r(ax), delay
              __TBB_L1:
                pause
                add __TBB_r(ax), -1
                jne __TBB_L1
            }
            return;
        }
    }}}}
#endif

static inline void __TBB_machine_pause (uintptr_t delay ){
    #if __TBB_PAUSE_USE_INTRINSIC
        tbb::internal::intrinsics::msvc::__TBB_machine_pause(delay);
    #else
        tbb::internal::inline_asm::msvc::__TBB_machine_pause(delay);
    #endif
}

//TODO: move this function to windows_api.h or to place where it is used
#if (_MSC_VER<1400) && (!_WIN64) && (__TBB_X86_MSVC_INLINE_ASM_AVAILABLE)
    static inline void* __TBB_machine_get_current_teb () {
        void* pteb;
        __asm mov eax, fs:[0x18]
        __asm mov pteb, eax
        return pteb;
    }
#endif

#if ( _MSC_VER>=1400 && !defined(__INTEL_COMPILER) ) ||  (__INTEL_COMPILER>=1200)
// MSVC did not have this intrinsic prior to VC8.
// ICL 11.1 fails to compile a TBB example if __TBB_Log2 uses the intrinsic.
    #define __TBB_LOG2_USE_BSR_INTRINSIC 1
    #if _M_X64
        #define __TBB_BSR_INTRINSIC _BitScanReverse64
    #else
        #define __TBB_BSR_INTRINSIC _BitScanReverse
    #endif
    #pragma intrinsic(__TBB_BSR_INTRINSIC)

    namespace tbb { namespace internal { namespace intrinsics { namespace msvc {
        inline uintptr_t __TBB_machine_lg( uintptr_t i ){
            unsigned long j;
            __TBB_BSR_INTRINSIC( &j, i );
            return j;
        }
    }}}}
#else
    #if !__TBB_X86_MSVC_INLINE_ASM_AVAILABLE
        #error __TBB_NO_X86_MSVC_INLINE_ASM_MSG
    #endif

    namespace tbb { namespace internal { namespace inline_asm { namespace msvc {
        inline uintptr_t __TBB_machine_lg( uintptr_t i ){
            uintptr_t j;
            __asm
            {
                bsr __TBB_r(ax), i
                mov j, __TBB_r(ax)
            }
            return j;
        }
    }}}}
#endif

static inline intptr_t __TBB_machine_lg( uintptr_t i ) {
#if __TBB_LOG2_USE_BSR_INTRINSIC
    return tbb::internal::intrinsics::msvc::__TBB_machine_lg(i);
#else
    return tbb::internal::inline_asm::msvc::__TBB_machine_lg(i);
#endif
}

// API to retrieve/update FPU control setting
#define __TBB_CPU_CTL_ENV_PRESENT 1

namespace tbb { namespace internal { class cpu_ctl_env; } }
#if __TBB_X86_MSVC_INLINE_ASM_AVAILABLE
    inline void __TBB_get_cpu_ctl_env ( tbb::internal::cpu_ctl_env* ctl ) {
        __asm {
            __asm mov     __TBB_r(ax), ctl
            __asm stmxcsr [__TBB_r(ax)]
            __asm fstcw   [__TBB_r(ax)+4]
        }
    }
    inline void __TBB_set_cpu_ctl_env ( const tbb::internal::cpu_ctl_env* ctl ) {
        __asm {
            __asm mov     __TBB_r(ax), ctl
            __asm ldmxcsr [__TBB_r(ax)]
            __asm fldcw   [__TBB_r(ax)+4]
        }
    }
#else
    extern "C" {
        void __TBB_EXPORTED_FUNC __TBB_get_cpu_ctl_env ( tbb::internal::cpu_ctl_env* );
        void __TBB_EXPORTED_FUNC __TBB_set_cpu_ctl_env ( const tbb::internal::cpu_ctl_env* );
    }
#endif

namespace tbb {
namespace internal {
class cpu_ctl_env {
private:
    int         mxcsr;
    short       x87cw;
    static const int MXCSR_CONTROL_MASK = ~0x3f; /* all except last six status bits */
public:
    bool operator!=( const cpu_ctl_env& ctl ) const { return mxcsr != ctl.mxcsr || x87cw != ctl.x87cw; }
    void get_env() {
        __TBB_get_cpu_ctl_env( this );
        mxcsr &= MXCSR_CONTROL_MASK;
    }
    void set_env() const { __TBB_set_cpu_ctl_env( this ); }
};
} // namespace internal
} // namespace tbb

#if !__TBB_WIN8UI_SUPPORT
extern "C" __declspec(dllimport) int __stdcall SwitchToThread( void );
#define __TBB_Yield()  SwitchToThread()
#else
#include<thread>
#define __TBB_Yield()  std::this_thread::yield()
#endif

#define __TBB_Pause(V) __TBB_machine_pause(V)
#define __TBB_Log2(V)  __TBB_machine_lg(V)

#undef __TBB_r

extern "C" {
    __int8 __TBB_EXPORTED_FUNC __TBB_machine_try_lock_elided (volatile void* ptr);
    void   __TBB_EXPORTED_FUNC __TBB_machine_unlock_elided (volatile void* ptr);

    // 'pause' instruction aborts HLE/RTM transactions
#if __TBB_PAUSE_USE_INTRINSIC
    inline static void __TBB_machine_try_lock_elided_cancel() { _mm_pause(); }
#else
    inline static void __TBB_machine_try_lock_elided_cancel() { _asm pause; }
#endif

#if __TBB_TSX_INTRINSICS_PRESENT
    #define __TBB_machine_is_in_transaction _xtest
    #define __TBB_machine_begin_transaction _xbegin
    #define __TBB_machine_end_transaction   _xend
    // The value (0xFF) below comes from the
    // Intel(R) 64 and IA-32 Architectures Optimization Reference Manual 12.4.5 lock not free
    #define __TBB_machine_transaction_conflict_abort() _xabort(0xFF)
#else
    __int8           __TBB_EXPORTED_FUNC __TBB_machine_is_in_transaction();
    unsigned __int32 __TBB_EXPORTED_FUNC __TBB_machine_begin_transaction();
    void             __TBB_EXPORTED_FUNC __TBB_machine_end_transaction();
    void             __TBB_EXPORTED_FUNC __TBB_machine_transaction_conflict_abort();
#endif /* __TBB_TSX_INTRINSICS_PRESENT */
}

#endif /* __TBB_machine_msvc_ia32_common_H */
