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

#ifndef _TBB_mailbox_H
#define _TBB_mailbox_H

#include "tbb/tbb_stddef.h"
#include "tbb/cache_aligned_allocator.h"

#include "scheduler_common.h"
#include "tbb/atomic.h"

namespace tbb {
namespace internal {

struct task_proxy : public task {
    static const intptr_t      pool_bit = 1<<0;
    static const intptr_t   mailbox_bit = 1<<1;
    static const intptr_t location_mask = pool_bit | mailbox_bit;
    /* All but two low-order bits represent a (task*).
       Two low-order bits mean:
       1 = proxy is/was/will be in task pool
       2 = proxy is/was/will be in mailbox */
    intptr_t task_and_tag;

    //! Pointer to next task_proxy in a mailbox
    task_proxy *__TBB_atomic next_in_mailbox;

    //! Mailbox to which this was mailed.
    mail_outbox* outbox;

    //! True if the proxy is stored both in its sender's pool and in the destination mailbox.
    static bool is_shared ( intptr_t tat ) {
        return (tat & location_mask) == location_mask;
    }

    //! Returns a pointer to the encapsulated task or NULL.
    static task* task_ptr ( intptr_t tat ) {
        return (task*)(tat & ~location_mask);
    }

    //! Returns a pointer to the encapsulated task or NULL, and frees proxy if necessary.
    template<intptr_t from_bit>
    inline task* extract_task () {
        __TBB_ASSERT( prefix().extra_state == es_task_proxy, "Normal task misinterpreted as a proxy?" );
        intptr_t tat = __TBB_load_with_acquire(task_and_tag);
        __TBB_ASSERT( tat == from_bit || (is_shared(tat) && task_ptr(tat)),
            "Proxy's tag cannot specify both locations if the proxy "
            "was retrieved from one of its original locations" );
        if ( tat != from_bit ) {
            const intptr_t cleaner_bit = location_mask & ~from_bit;
            // Attempt to transition the proxy to the "empty" state with
            // cleaner_bit specifying entity responsible for its eventual freeing.
            // Explicit cast to void* is to work around a seeming ICC 11.1 bug.
            if ( as_atomic(task_and_tag).compare_and_swap(cleaner_bit, tat) == tat ) {
                // Successfully grabbed the task, and left new owner with the job of freeing the proxy
                return task_ptr(tat);
            }
        }
        // Proxied task has already been claimed from another proxy location.
        __TBB_ASSERT( task_and_tag == from_bit, "Empty proxy cannot contain non-zero task pointer" );
        poison_pointer(outbox);
        poison_pointer(next_in_mailbox);
        poison_value(task_and_tag);
        return NULL;
    }
}; // struct task_proxy

//! Internal representation of mail_outbox, without padding.
class unpadded_mail_outbox {
protected:
    typedef task_proxy*__TBB_atomic proxy_ptr;

    //! Pointer to first task_proxy in mailbox, or NULL if box is empty. 
    proxy_ptr my_first;

    //! Pointer to pointer that will point to next item in the queue.  Never NULL.
    proxy_ptr* __TBB_atomic my_last;

    //! Owner of mailbox is not executing a task, and has drained its own task pool.
    bool my_is_idle;
};

//! Class representing where mail is put.
/** Padded to occupy a cache line. */
class mail_outbox : padded<unpadded_mail_outbox> {

    task_proxy* internal_pop() {
        task_proxy* const first = __TBB_load_relaxed(my_first);
        if( !first )
            return NULL;
        __TBB_control_consistency_helper(); // on my_first
        // There is a first item in the mailbox.  See if there is a second.
        if( task_proxy* second = first->next_in_mailbox ) {
            // There are at least two items, so first item can be popped easily.
            my_first = second;
        } else {
            // There is only one item.  Some care is required to pop it.
            my_first = NULL;
            if( as_atomic(my_last).compare_and_swap(&my_first,&first->next_in_mailbox) == &first->next_in_mailbox )
            {
                // Successfully transitioned mailbox from having one item to having none.
                __TBB_ASSERT(!first->next_in_mailbox,NULL);
            } else {
                // Some other thread updated my_last but has not filled in first->next_in_mailbox
                // Wait until first item points to second item.
                atomic_backoff backoff;
                while( !(second = first->next_in_mailbox) ) backoff.pause();
                my_first = second;
            }
        }
        return first;
    }
public:
    friend class mail_inbox;

    //! Push task_proxy onto the mailbox queue of another thread.
    /** Implementation is wait-free. */
    void push( task_proxy& t ) {
        __TBB_ASSERT(&t, NULL);
        t.next_in_mailbox = NULL; 
        proxy_ptr * const link = (proxy_ptr *)__TBB_FetchAndStoreW(&my_last,(intptr_t)&t.next_in_mailbox);
        // No release fence required for the next store, because there are no memory operations 
        // between the previous fully fenced atomic operation and the store.
        __TBB_store_relaxed(*link, &t);
    }

    //! Return true if mailbox is empty
    bool empty() {
        return __TBB_load_relaxed(my_first) == NULL;
    }

    //! Construct *this as a mailbox from zeroed memory.
    /** Raise assertion if *this is not previously zeroed, or sizeof(this) is wrong.
        This method is provided instead of a full constructor since we know the object
        will be constructed in zeroed memory. */
    void construct() {
        __TBB_ASSERT( sizeof(*this)==NFS_MaxLineSize, NULL );
        __TBB_ASSERT( !my_first, NULL );
        __TBB_ASSERT( !my_last, NULL );
        __TBB_ASSERT( !my_is_idle, NULL );
        my_last=&my_first;
        suppress_unused_warning(pad);
    }

    //! Drain the mailbox 
    intptr_t drain() {
        intptr_t k = 0;
        // No fences here because other threads have already quit.
        for( ; task_proxy* t = my_first; ++k ) {
            my_first = t->next_in_mailbox;
            NFS_Free((char*)t - task_prefix_reservation_size);
        }
        return k;  
    }

    //! True if thread that owns this mailbox is looking for work.
    bool recipient_is_idle() {
        return my_is_idle;
    }
}; // class mail_outbox

//! Class representing source of mail.
class mail_inbox {
    //! Corresponding sink where mail that we receive will be put.
    mail_outbox* my_putter;
public:
    //! Construct unattached inbox
    mail_inbox() : my_putter(NULL) {}

    //! Attach inbox to a corresponding outbox. 
    void attach( mail_outbox& putter ) {
        __TBB_ASSERT(!my_putter,"already attached");
        my_putter = &putter;
    }
    //! Detach inbox from its outbox
    void detach() {
        __TBB_ASSERT(my_putter,"not attached");
        my_putter = NULL;
    }
    //! Get next piece of mail, or NULL if mailbox is empty.
    task_proxy* pop() {
        return my_putter->internal_pop();
    }
    //! Return true if mailbox is empty
    bool empty() {
        return my_putter->empty();
    }
    //! Indicate whether thread that reads this mailbox is idle.
    /** Raises assertion failure if mailbox is redundantly marked as not idle. */
    void set_is_idle( bool value ) {
        if( my_putter ) {
            __TBB_ASSERT( my_putter->my_is_idle || value, "attempt to redundantly mark mailbox as not idle" );
            my_putter->my_is_idle = value;
        }
    }
    //! Indicate whether thread that reads this mailbox is idle.
    bool is_idle_state ( bool value ) const {
        return !my_putter || my_putter->my_is_idle == value;
    }

#if DO_ITT_NOTIFY
    //! Get pointer to corresponding outbox used for ITT_NOTIFY calls.
    void* outbox() const {return my_putter;}
#endif /* DO_ITT_NOTIFY */ 
}; // class mail_inbox

} // namespace internal
} // namespace tbb

#endif /* _TBB_mailbox_H */
