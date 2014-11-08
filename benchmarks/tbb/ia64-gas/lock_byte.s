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

	// Support for class TinyLock
	.section .text
	.align 16
	// unsigned int __TBB_machine_trylockbyte( byte& flag );
	// r32 = address of flag 
	.proc  __TBB_machine_trylockbyte#
	.global __TBB_machine_trylockbyte#
ADDRESS_OF_FLAG=r32
RETCODE=r8
FLAG=r9
BUSY=r10
SCRATCH=r11
__TBB_machine_trylockbyte:
        ld1.acq FLAG=[ADDRESS_OF_FLAG]
        mov BUSY=1
        mov RETCODE=0
;;
        cmp.ne p6,p0=0,FLAG
        mov ar.ccv=r0
(p6)    br.ret.sptk.many b0
;;
        cmpxchg1.acq SCRATCH=[ADDRESS_OF_FLAG],BUSY,ar.ccv  // Try to acquire lock
;;
        cmp.eq p6,p0=0,SCRATCH
;;
(p6)    mov RETCODE=1
   	br.ret.sptk.many b0	
	.endp __TBB_machine_trylockbyte#
