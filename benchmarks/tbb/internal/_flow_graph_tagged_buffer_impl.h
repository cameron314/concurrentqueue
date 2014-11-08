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

// tagged buffer that can expand, and can support as many deletions as additions
// list-based, with elements of list held in array (for destruction management),
// multiplicative hashing (like ets).  No synchronization built-in.
//

#ifndef __TBB__flow_graph_tagged_buffer_impl_H
#define __TBB__flow_graph_tagged_buffer_impl_H

#ifndef __TBB_flow_graph_H
#error Do not #include this internal file directly; use public TBB headers instead.
#endif

// included in namespace tbb::flow::interface7::internal

template<typename T, typename U, size_t NoTagMark>
struct otherData {
    T t;
    U next;
    otherData() : t(NoTagMark), next(NULL) {}
};

template<typename TagType, typename ValueType, size_t NoTagMark>
struct buffer_element_type {
    // the second parameter below is void * because we can't forward-declare the type
    // itself, so we just reinterpret_cast below.
    typedef typename aligned_pair<ValueType, otherData<TagType, void *, NoTagMark> >::type type;
};

template
    <
     typename TagType, 
     typename ValueType, 
     size_t   NoTagMark = 0,
     typename Allocator=tbb::cache_aligned_allocator< typename buffer_element_type<TagType, ValueType, NoTagMark>::type >
    >
class tagged_buffer {
public:
    static const size_t INITIAL_SIZE = 8;  // initial size of the hash pointer table
    static const TagType NO_TAG = TagType(NoTagMark);
    typedef ValueType value_type;
    typedef typename buffer_element_type<TagType, ValueType, NO_TAG>::type element_type;
    typedef value_type *pointer_type;
    typedef element_type *list_array_type;  // array we manage manually
    typedef list_array_type *pointer_array_type;
    typedef typename Allocator::template rebind<list_array_type>::other pointer_array_allocator_type;
    typedef typename Allocator::template rebind<element_type>::other elements_array_allocator;
private:
    size_t my_size;
    size_t nelements;
    pointer_array_type pointer_array;    // pointer_array[my_size]
    list_array_type elements_array;      // elements_array[my_size / 2]
    element_type* free_list;

    size_t mask() { return my_size - 1; }

    static size_t hash(TagType t) {
        return uintptr_t(t)*tbb::internal::select_size_t_constant<0x9E3779B9,0x9E3779B97F4A7C15ULL>::value;
    }

    void set_up_free_list( element_type **p_free_list, list_array_type la, size_t sz) {
        for(size_t i=0; i < sz - 1; ++i ) {  // construct free list
            la[i].second.next = &(la[i+1]);
            la[i].second.t = NO_TAG;
        }
        la[sz-1].second.next = NULL;
        *p_free_list = &(la[0]);
    }

    // cleanup for exceptions
    struct DoCleanup {
        pointer_array_type *my_pa;
        list_array_type *my_elements;
        size_t my_size;

        DoCleanup(pointer_array_type &pa, list_array_type &my_els, size_t sz) :
            my_pa(&pa), my_elements(&my_els), my_size(sz) {  }
        ~DoCleanup() {
            if(my_pa) {
                size_t dont_care = 0;
                internal_free_buffer(*my_pa, *my_elements, my_size, dont_care);
            }
        }
    };

    // exception-safety requires we do all the potentially-throwing operations first
    void grow_array() {
        size_t new_size = my_size*2;
        size_t new_nelements = nelements;  // internal_free_buffer zeroes this
        list_array_type new_elements_array = NULL;
        pointer_array_type new_pointer_array = NULL;
        list_array_type new_free_list = NULL;
        {
            DoCleanup my_cleanup(new_pointer_array, new_elements_array, new_size);
            new_elements_array = elements_array_allocator().allocate(my_size);
            new_pointer_array = pointer_array_allocator_type().allocate(new_size);
            for(size_t i=0; i < new_size; ++i) new_pointer_array[i] = NULL;
            set_up_free_list(&new_free_list, new_elements_array, my_size );

            for(size_t i=0; i < my_size; ++i) {
                for( element_type* op = pointer_array[i]; op; op = (element_type *)(op->second.next)) {
                    value_type *ov = reinterpret_cast<value_type *>(&(op->first));
                    // could have std::move semantics
                    internal_tagged_insert(new_pointer_array, new_size, new_free_list, op->second.t, *ov);
                }
            }
            my_cleanup.my_pa = NULL;
            my_cleanup.my_elements = NULL;
        }

        internal_free_buffer(pointer_array, elements_array, my_size, nelements);
        free_list = new_free_list;
        pointer_array = new_pointer_array;
        elements_array = new_elements_array;
        my_size = new_size;
        nelements = new_nelements;
    }

    // v should have perfect forwarding if std::move implemented.
    // we use this method to move elements in grow_array, so can't use class fields
    void internal_tagged_insert( element_type **p_pointer_array, size_t p_sz, list_array_type &p_free_list,
            const TagType t, const value_type &v) {
        size_t l_mask = p_sz-1;
        size_t h = hash(t) & l_mask;
        __TBB_ASSERT(p_free_list, "Error: free list not set up.");
        element_type* my_elem = p_free_list; p_free_list = (element_type *)(p_free_list->second.next);
        my_elem->second.t = t;
        (void) new(&(my_elem->first)) value_type(v);
        my_elem->second.next = p_pointer_array[h];
        p_pointer_array[h] = my_elem;
    }

    void internal_initialize_buffer() {
        pointer_array = pointer_array_allocator_type().allocate(my_size);
        for(size_t i = 0; i < my_size; ++i) pointer_array[i] = NULL;
        elements_array = elements_array_allocator().allocate(my_size / 2);
        set_up_free_list(&free_list, elements_array, my_size / 2);
    }

    // made static so an enclosed class can use to properly dispose of the internals
    static void internal_free_buffer( pointer_array_type &pa, list_array_type &el, size_t &sz, size_t &ne ) {
        if(pa) {
            for(size_t i = 0; i < sz; ++i ) {
                element_type *p_next;
                for( element_type *p = pa[i]; p; p = p_next) {
                    p_next = (element_type *)p->second.next;
                    value_type *vp = reinterpret_cast<value_type *>(&(p->first));
                    vp->~value_type();
                }
            }
            pointer_array_allocator_type().deallocate(pa, sz); 
            pa = NULL;
        }
        // Separate test (if allocation of pa throws, el may be allocated.
        // but no elements will be constructed.)
        if(el) {
            elements_array_allocator().deallocate(el, sz / 2);
            el = NULL;
        }
        sz = INITIAL_SIZE;
        ne = 0;
    }

public:
    tagged_buffer() : my_size(INITIAL_SIZE), nelements(0) {
        internal_initialize_buffer();
    }

    ~tagged_buffer() {
        internal_free_buffer(pointer_array, elements_array, my_size, nelements);
    }

    void reset() {
        internal_free_buffer(pointer_array, elements_array, my_size, nelements);
        internal_initialize_buffer();
    }

    bool tagged_insert(const TagType t, const value_type &v) {
        pointer_type p;
        if(tagged_find_ref(t, p)) {
            p->~value_type();
            (void) new(p) value_type(v);  // copy-construct into the space
            return false;
        }
        ++nelements;
        if(nelements*2 > my_size) grow_array();
        internal_tagged_insert(pointer_array, my_size, free_list, t, v);
        return true;
    }

    // returns reference to array element.v
    bool tagged_find_ref(const TagType t, pointer_type &v) {
        size_t i = hash(t) & mask();
        for(element_type* p = pointer_array[i]; p; p = (element_type *)(p->second.next)) {
            if(p->second.t == t) {
                v = reinterpret_cast<pointer_type>(&(p->first));
                return true;
            }
        }
        return false;
    }

    bool tagged_find( const TagType t, value_type &v) {
        value_type *p;
        if(tagged_find_ref(t, p)) {
            v = *p;
            return true;
        }
        else
            return false;
    }

    void tagged_delete(const TagType t) {
        size_t h = hash(t) & mask();
        element_type* prev = NULL;
        for(element_type* p = pointer_array[h]; p; prev = p, p = (element_type *)(p->second.next)) {
            if(p->second.t == t) {
                value_type *vp = reinterpret_cast<value_type *>(&(p->first));
                vp->~value_type();
                p->second.t = NO_TAG;
                if(prev) prev->second.next = p->second.next;
                else pointer_array[h] = (element_type *)(p->second.next);
                p->second.next = free_list;
                free_list = p;
                --nelements;
                return;
            }
        }
        __TBB_ASSERT(false, "tag not found for delete");
    }
};
#endif // __TBB__flow_graph_tagged_buffer_impl_H
