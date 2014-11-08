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

	.section .text
	.align 16
	// void __TBB_machine_pause( long count );
	// r32 = count
	.proc  __TBB_machine_pause#
	.global __TBB_machine_pause#
count = r32
__TBB_machine_pause:
        hint.m 0
	add count=-1,count
;;
	cmp.eq p6,p7=0,count
(p7)	br.cond.dpnt __TBB_machine_pause
(p6)   	br.ret.sptk.many b0	
	.endp __TBB_machine_pause#
