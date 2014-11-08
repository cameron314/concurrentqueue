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

#ifndef _ITTNOTIFY_TYPES_H_
#define _ITTNOTIFY_TYPES_H_

typedef enum ___itt_group_id
{
    __itt_group_none      = 0,
    __itt_group_legacy    = 1<<0,
    __itt_group_control   = 1<<1,
    __itt_group_thread    = 1<<2,
    __itt_group_mark      = 1<<3,
    __itt_group_sync      = 1<<4,
    __itt_group_fsync     = 1<<5,
    __itt_group_jit       = 1<<6,
    __itt_group_model     = 1<<7,
    __itt_group_splitter_min = 1<<7,
    __itt_group_counter   = 1<<8,
    __itt_group_frame     = 1<<9,
    __itt_group_stitch    = 1<<10,
    __itt_group_heap      = 1<<11,
    __itt_group_splitter_max = 1<<12,
    __itt_group_structure = 1<<12,
    __itt_group_suppress = 1<<13,
    __itt_group_arrays    = 1<<14,
    __itt_group_all       = -1
} __itt_group_id;

#pragma pack(push, 8)

typedef struct ___itt_group_list
{
    __itt_group_id id;
    const char*    name;
} __itt_group_list;

#pragma pack(pop)

#define ITT_GROUP_LIST(varname) \
    static __itt_group_list varname[] = {       \
        { __itt_group_all,       "all"       }, \
        { __itt_group_control,   "control"   }, \
        { __itt_group_thread,    "thread"    }, \
        { __itt_group_mark,      "mark"      }, \
        { __itt_group_sync,      "sync"      }, \
        { __itt_group_fsync,     "fsync"     }, \
        { __itt_group_jit,       "jit"       }, \
        { __itt_group_model,     "model"     }, \
        { __itt_group_counter,   "counter"   }, \
        { __itt_group_frame,     "frame"     }, \
        { __itt_group_stitch,    "stitch"    }, \
        { __itt_group_heap,      "heap"      }, \
        { __itt_group_structure, "structure" }, \
        { __itt_group_suppress,  "suppress"  }, \
        { __itt_group_arrays,    "arrays"    }, \
        { __itt_group_none,      NULL        }  \
    }

#endif /* _ITTNOTIFY_TYPES_H_ */
