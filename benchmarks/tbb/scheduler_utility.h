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

#ifndef _TBB_scheduler_utility_H
#define _TBB_scheduler_utility_H

#include "scheduler.h"

namespace tbb {
namespace internal {

//------------------------------------------------------------------------
// auto_empty_task
//------------------------------------------------------------------------

//! Smart holder for the empty task class with automatic destruction
class auto_empty_task {
    task* my_task;
    generic_scheduler* my_scheduler;
public:
    auto_empty_task ( __TBB_CONTEXT_ARG(generic_scheduler *s, task_group_context* context) ) 
        : my_task( new(&s->allocate_task(sizeof(empty_task), __TBB_CONTEXT_ARG(NULL, context))) empty_task )
        , my_scheduler(s)
    {}
    // empty_task has trivial destructor, so there's no need to call it.
    ~auto_empty_task () { my_scheduler->free_task<small_local_task>(*my_task); }

    operator task& () { return *my_task; }
    task* operator & () { return my_task; }
    task_prefix& prefix () { return my_task->prefix(); }
}; // class auto_empty_task

//------------------------------------------------------------------------
// fast_reverse_vector
//------------------------------------------------------------------------

//! Vector that grows without reallocations, and stores items in the reverse order.
/** Requires to initialize its first segment with a preallocated memory chunk
    (usually it is static array or an array allocated on the stack).
    The second template parameter specifies maximal number of segments. Each next 
    segment is twice as large as the previous one. **/
template<typename T, size_t max_segments = 16>
class fast_reverse_vector
{
public:
    fast_reverse_vector ( T* initial_segment, size_t segment_size )
        : m_cur_segment(initial_segment)
        , m_cur_segment_size(segment_size)
        , m_pos(segment_size)
        , m_num_segments(0)
        , m_size(0)
    {
        __TBB_ASSERT ( initial_segment && segment_size, "Nonempty initial segment must be supplied");
    }

    ~fast_reverse_vector ()
    {
        for ( size_t i = 1; i < m_num_segments; ++i )
            NFS_Free( m_segments[i] );
    }

    size_t size () const { return m_size + m_cur_segment_size - m_pos; }

    void push_back ( const T& val )
    {
        if ( !m_pos ) {
            if ( !m_num_segments ) m_segments[m_num_segments++] = m_cur_segment;
            m_size += m_cur_segment_size;
            m_cur_segment_size *= 2;
            m_pos = m_cur_segment_size;
            m_segments[m_num_segments++] = m_cur_segment = (T*)NFS_Allocate( m_cur_segment_size, sizeof(T), NULL );
            __TBB_ASSERT ( m_num_segments < max_segments, "Maximal capacity exceeded" );
        }
        m_cur_segment[--m_pos] = val;
    }

    //! Copies the contents of the vector into the dst array. 
    /** Can only be used when T is a POD type, as copying does not invoke copy constructors. **/
    void copy_memory ( T* dst ) const
    {
        size_t sz = m_cur_segment_size - m_pos;
        memcpy( dst, m_cur_segment + m_pos, sz * sizeof(T) );
        dst += sz;
        sz = m_cur_segment_size / 2;
        for ( long i = (long)m_num_segments - 2; i >= 0; --i ) {
            memcpy( dst, m_segments[i], sz * sizeof(T) );
            dst += sz;
            sz /= 2;
        }
    }

protected:
    //! The current (not completely filled) segment
    T       *m_cur_segment;

    //! Capacity of m_cur_segment
    size_t  m_cur_segment_size;

    //! Insertion position in m_cur_segment
    size_t  m_pos;

    //! Array of segments (has fixed size specified by the second template parameter)
    T       *m_segments[max_segments];
    
    //! Number of segments (the size of m_segments)
    size_t  m_num_segments;

    //! Number of items in the segments in m_segments
    size_t  m_size;

}; // class fast_reverse_vector

} // namespace internal
} // namespace tbb

#endif /* _TBB_scheduler_utility_H */
