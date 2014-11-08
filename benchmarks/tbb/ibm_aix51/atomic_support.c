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

#include <stdint.h>
#include <sys/atomic_op.h>

/* This file must be compiled with gcc.  The IBM compiler doesn't seem to
   support inline assembly statements (October 2007). */

#ifdef __GNUC__

int32_t __TBB_machine_cas_32 (volatile void* ptr, int32_t value, int32_t comparand) { 
    __asm__ __volatile__ ("sync\n");  /* memory release operation */
    compare_and_swap ((atomic_p) ptr, &comparand, value);
    __asm__ __volatile__ ("isync\n");  /* memory acquire operation */
    return comparand;
}

int64_t __TBB_machine_cas_64 (volatile void* ptr, int64_t value, int64_t comparand) { 
    __asm__ __volatile__ ("sync\n");  /* memory release operation */
    compare_and_swaplp ((atomic_l) ptr, &comparand, value);
    __asm__ __volatile__ ("isync\n");  /* memory acquire operation */
    return comparand;
}

void __TBB_machine_flush () { 
    __asm__ __volatile__ ("sync\n");
}

void __TBB_machine_lwsync () { 
    __asm__ __volatile__ ("lwsync\n");
}

void __TBB_machine_isync () { 
    __asm__ __volatile__ ("isync\n");
}

#endif /* __GNUC__ */
