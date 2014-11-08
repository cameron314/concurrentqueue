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

#include "tbb/concurrent_hash_map.h"

namespace tbb {

namespace internal {
#if !TBB_NO_LEGACY
struct hash_map_segment_base {
    typedef spin_rw_mutex segment_mutex_t;
    //! Type of a hash code.
    typedef size_t hashcode_t;
    //! Log2 of n_segment
    static const size_t n_segment_bits = 6;
    //! Maximum size of array of chains
    static const size_t max_physical_size = size_t(1)<<(8*sizeof(hashcode_t)-n_segment_bits);
    //! Mutex that protects this segment
    segment_mutex_t my_mutex;
    // Number of nodes
    atomic<size_t> my_logical_size;
    // Size of chains
    /** Always zero or a power of two */
    size_t my_physical_size;
    //! True if my_logical_size>=my_physical_size.
    /** Used to support Intel(R) Thread Checker. */
    bool __TBB_EXPORTED_METHOD internal_grow_predicate() const;
};

bool hash_map_segment_base::internal_grow_predicate() const {
    // Intel(R) Thread Checker considers the following reads to be races, so we hide them in the 
    // library so that Intel(R) Thread Checker will ignore them.  The reads are used in a double-check
    // context, so the program is nonetheless correct despite the race.
    return my_logical_size >= my_physical_size && my_physical_size < max_physical_size;
}
#endif//!TBB_NO_LEGACY

} // namespace internal

} // namespace tbb

