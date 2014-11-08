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

.code
        ALIGN 8
        PUBLIC __TBB_machine_try_lock_elided
__TBB_machine_try_lock_elided:
        xor  rax, rax
        mov  al, 1
        BYTE 0F2H
        xchg al, byte ptr [rcx]
        xor  al, 1
        ret
.code
        ALIGN 8
        PUBLIC __TBB_machine_unlock_elided
__TBB_machine_unlock_elided:
        BYTE 0F3H
        mov  byte ptr [rcx], 0
        ret
.code 
	ALIGN 8
	PUBLIC __TBB_machine_begin_transaction
__TBB_machine_begin_transaction:
        mov  eax, -1
        BYTE 0C7H
        BYTE 0F8H
        BYTE 000H
        BYTE 000H
        BYTE 000H
        BYTE 000H
        ret
.code 
	ALIGN 8
	PUBLIC __TBB_machine_end_transaction
__TBB_machine_end_transaction:
        BYTE 00FH
        BYTE 001H
        BYTE 0D5H
        ret
.code 
	ALIGN 8
	PUBLIC __TBB_machine_transaction_conflict_abort
__TBB_machine_transaction_conflict_abort:
        BYTE 0C6H
        BYTE 0F8H
        BYTE 0FFH  ; 12.4.5 Abort argument: lock not free when tested
        ret
.code 
        ALIGN 8
	PUBLIC __TBB_machine_is_in_transaction
__TBB_machine_is_in_transaction:
        xor eax, eax
        BYTE 00FH  ; _xtest sets or clears ZF
        BYTE 001H
        BYTE 0D6H
        jz   rset
        mov  al,1
rset:
        ret
end
