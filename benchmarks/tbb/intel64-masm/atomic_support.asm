; Copyright 2005-2014 Intel Corporation.  All Rights Reserved.
;
; This file is part of Threading Building Blocks. Threading Building Blocks is free software;
; you can redistribute it and/or modify it under the terms of the GNU General Public License
; version 2  as  published  by  the  Free Software Foundation.  Threading Building Blocks is
; distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
; implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
; See  the GNU General Public License for more details.   You should have received a copy of
; the  GNU General Public License along with Threading Building Blocks; if not, write to the
; Free Software Foundation, Inc.,  51 Franklin St,  Fifth Floor,  Boston,  MA 02110-1301 USA
;
; As a special exception,  you may use this file  as part of a free software library without
; restriction.  Specifically,  if other files instantiate templates  or use macros or inline
; functions from this file, or you compile this file and link it with other files to produce
; an executable,  this file does not by itself cause the resulting executable to be covered
; by the GNU General Public License. This exception does not however invalidate any other
; reasons why the executable file might be covered by the GNU General Public License.

; DO NOT EDIT - AUTOMATICALLY GENERATED FROM .s FILE
.code 
	ALIGN 8
	PUBLIC __TBB_machine_fetchadd1
__TBB_machine_fetchadd1:
	mov rax,rdx
	lock xadd [rcx],al
	ret
.code 
	ALIGN 8
	PUBLIC __TBB_machine_fetchstore1
__TBB_machine_fetchstore1:
	mov rax,rdx
	lock xchg [rcx],al
	ret
.code 
	ALIGN 8
	PUBLIC __TBB_machine_cmpswp1
__TBB_machine_cmpswp1:
	mov rax,r8
	lock cmpxchg [rcx],dl
	ret
.code 
	ALIGN 8
	PUBLIC __TBB_machine_fetchadd2
__TBB_machine_fetchadd2:
	mov rax,rdx
	lock xadd [rcx],ax
	ret
.code 
	ALIGN 8
	PUBLIC __TBB_machine_fetchstore2
__TBB_machine_fetchstore2:
	mov rax,rdx
	lock xchg [rcx],ax
	ret
.code 
	ALIGN 8
	PUBLIC __TBB_machine_cmpswp2
__TBB_machine_cmpswp2:
	mov rax,r8
	lock cmpxchg [rcx],dx
	ret
.code
        ALIGN 8
        PUBLIC __TBB_machine_pause
__TBB_machine_pause:
L1:
        dw 090f3H; pause
        add ecx,-1
        jne L1
        ret
end

