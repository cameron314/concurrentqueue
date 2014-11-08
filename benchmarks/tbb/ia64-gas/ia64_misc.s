// Copyright 2005-2014 Intel Corporation.  All Rights Reserved.
//
// This file is part of Threading Building Blocks. Threading Building Blocks is free software;
// you can redistribute it and/or modify it under the terms of the GNU General Public License
// version 2  as  published  by  the  Free Software Foundation.  Threading Building Blocks is
// distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See  the GNU General Public License for more details.   You should have received a copy of
// the  GNU General Public License along with Threading Building Blocks; if not, write to the
// Free Software Foundation, Inc.,  51 Franklin St,  Fifth Floor,  Boston,  MA 02110-1301 USA
//
// As a special exception,  you may use this file  as part of a free software library without
// restriction.  Specifically,  if other files instantiate templates  or use macros or inline
// functions from this file, or you compile this file and link it with other files to produce
// an executable,  this file does not by itself cause the resulting executable to be covered
// by the GNU General Public License. This exception does not however invalidate any other
// reasons why the executable file might be covered by the GNU General Public License.

	// RSE backing store pointer retrieval
    .section .text
    .align 16
    .proc __TBB_get_bsp#
    .global __TBB_get_bsp#
__TBB_get_bsp:
        mov r8=ar.bsp
        br.ret.sptk.many b0
    .endp __TBB_get_bsp#

    .section .text
    .align 16
    .proc __TBB_machine_load8_relaxed#
    .global __TBB_machine_load8_relaxed#
__TBB_machine_load8_relaxed:
        ld8 r8=[r32]
        br.ret.sptk.many b0
    .endp __TBB_machine_load8_relaxed#

    .section .text
    .align 16
    .proc __TBB_machine_store8_relaxed#
    .global __TBB_machine_store8_relaxed#
__TBB_machine_store8_relaxed:
        st8 [r32]=r33
        br.ret.sptk.many b0
    .endp __TBB_machine_store8_relaxed#

    .section .text
    .align 16
    .proc __TBB_machine_load4_relaxed#
    .global __TBB_machine_load4_relaxed#
__TBB_machine_load4_relaxed:
        ld4 r8=[r32]
        br.ret.sptk.many b0
    .endp __TBB_machine_load4_relaxed#

    .section .text
    .align 16
    .proc __TBB_machine_store4_relaxed#
    .global __TBB_machine_store4_relaxed#
__TBB_machine_store4_relaxed:
        st4 [r32]=r33
        br.ret.sptk.many b0
    .endp __TBB_machine_store4_relaxed#

    .section .text
    .align 16
    .proc __TBB_machine_load2_relaxed#
    .global __TBB_machine_load2_relaxed#
__TBB_machine_load2_relaxed:
        ld2 r8=[r32]
        br.ret.sptk.many b0
    .endp __TBB_machine_load2_relaxed#

    .section .text
    .align 16
    .proc __TBB_machine_store2_relaxed#
    .global __TBB_machine_store2_relaxed#
__TBB_machine_store2_relaxed:
        st2 [r32]=r33
        br.ret.sptk.many b0
    .endp __TBB_machine_store2_relaxed#

    .section .text
    .align 16
    .proc __TBB_machine_load1_relaxed#
    .global __TBB_machine_load1_relaxed#
__TBB_machine_load1_relaxed:
        ld1 r8=[r32]
        br.ret.sptk.many b0
    .endp __TBB_machine_load1_relaxed#

    .section .text
    .align 16
    .proc __TBB_machine_store1_relaxed#
    .global __TBB_machine_store1_relaxed#
__TBB_machine_store1_relaxed:
        st1 [r32]=r33
        br.ret.sptk.many b0
    .endp __TBB_machine_store1_relaxed#
