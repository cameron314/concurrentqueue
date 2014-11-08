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

.686
.model flat,c
.code 
	ALIGN 4
	PUBLIC c __TBB_machine_fetchadd1
__TBB_machine_fetchadd1:
	mov edx,4[esp]
	mov eax,8[esp]
	lock xadd [edx],al
	ret
.code 
	ALIGN 4
	PUBLIC c __TBB_machine_fetchstore1
__TBB_machine_fetchstore1:
	mov edx,4[esp]
	mov eax,8[esp]
	lock xchg [edx],al
	ret
.code 
	ALIGN 4
	PUBLIC c __TBB_machine_cmpswp1
__TBB_machine_cmpswp1:
	mov edx,4[esp]
	mov ecx,8[esp]
	mov eax,12[esp]
	lock cmpxchg [edx],cl
	ret
.code 
	ALIGN 4
	PUBLIC c __TBB_machine_fetchadd2
__TBB_machine_fetchadd2:
	mov edx,4[esp]
	mov eax,8[esp]
	lock xadd [edx],ax
	ret
.code 
	ALIGN 4
	PUBLIC c __TBB_machine_fetchstore2
__TBB_machine_fetchstore2:
	mov edx,4[esp]
	mov eax,8[esp]
	lock xchg [edx],ax
	ret
.code 
	ALIGN 4
	PUBLIC c __TBB_machine_cmpswp2
__TBB_machine_cmpswp2:
	mov edx,4[esp]
	mov ecx,8[esp]
	mov eax,12[esp]
	lock cmpxchg [edx],cx
	ret
.code 
	ALIGN 4
	PUBLIC c __TBB_machine_fetchadd4
__TBB_machine_fetchadd4:
	mov edx,4[esp]
	mov eax,8[esp]
	lock xadd [edx],eax
	ret
.code 
	ALIGN 4
	PUBLIC c __TBB_machine_fetchstore4
__TBB_machine_fetchstore4:
	mov edx,4[esp]
	mov eax,8[esp]
	lock xchg [edx],eax
	ret
.code 
	ALIGN 4
	PUBLIC c __TBB_machine_cmpswp4
__TBB_machine_cmpswp4:
	mov edx,4[esp]
	mov ecx,8[esp]
	mov eax,12[esp]
	lock cmpxchg [edx],ecx
	ret
.code 
	ALIGN 4
	PUBLIC c __TBB_machine_fetchadd8
__TBB_machine_fetchadd8:
	push ebx
	push edi
	mov edi,12[esp]
	mov eax,[edi]
	mov edx,4[edi]
__TBB_machine_fetchadd8_loop:
	mov ebx,16[esp]
	mov ecx,20[esp]
	add ebx,eax
	adc ecx,edx
	lock cmpxchg8b qword ptr [edi]
	jnz __TBB_machine_fetchadd8_loop
	pop edi
	pop ebx
	ret
.code 
	ALIGN 4
	PUBLIC c __TBB_machine_fetchstore8
__TBB_machine_fetchstore8:
	push ebx
	push edi
	mov edi,12[esp]
	mov ebx,16[esp]
	mov ecx,20[esp]
	mov eax,[edi]
	mov edx,4[edi]
__TBB_machine_fetchstore8_loop:
	lock cmpxchg8b qword ptr [edi]
	jnz __TBB_machine_fetchstore8_loop
	pop edi
	pop ebx
	ret
.code 
	ALIGN 4
	PUBLIC c __TBB_machine_cmpswp8
__TBB_machine_cmpswp8:
	push ebx
	push edi
	mov edi,12[esp]
	mov ebx,16[esp]
	mov ecx,20[esp]
	mov eax,24[esp]
	mov edx,28[esp]
	lock cmpxchg8b qword ptr [edi]
	pop edi
	pop ebx
	ret
.code 
	ALIGN 4
	PUBLIC c __TBB_machine_load8
__TBB_machine_Load8:
	; If location is on stack, compiler may have failed to align it correctly, so we do dynamic check.
	mov ecx,4[esp]
	test ecx,7
	jne load_slow
	; Load within a cache line
	sub esp,12
	fild qword ptr [ecx]
	fistp qword ptr [esp]
	mov eax,[esp]
	mov edx,4[esp]
	add esp,12
	ret
load_slow:
	; Load is misaligned. Use cmpxchg8b.
	push ebx
	push edi
	mov edi,ecx
	xor eax,eax
	xor ebx,ebx
	xor ecx,ecx
	xor edx,edx
	lock cmpxchg8b qword ptr [edi]
	pop edi
	pop ebx
	ret
EXTRN __TBB_machine_store8_slow:PROC
.code 
	ALIGN 4
	PUBLIC c __TBB_machine_store8
__TBB_machine_Store8:
	; If location is on stack, compiler may have failed to align it correctly, so we do dynamic check.
	mov ecx,4[esp]
	test ecx,7
	jne __TBB_machine_store8_slow ;; tail call to tbb_misc.cpp
	fild qword ptr 8[esp]
	fistp qword ptr [ecx]
	ret
end
