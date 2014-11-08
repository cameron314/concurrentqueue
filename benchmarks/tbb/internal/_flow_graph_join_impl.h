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

#ifndef __TBB__flow_graph_join_impl_H
#define __TBB__flow_graph_join_impl_H

#ifndef __TBB_flow_graph_H
#error Do not #include this internal file directly; use public TBB headers instead.
#endif

#include "_flow_graph_types_impl.h"

namespace internal {

    typedef size_t tag_value;
    static const tag_value NO_TAG = tag_value(-1);

    struct forwarding_base {
        forwarding_base(graph &g) : my_graph_ptr(&g), current_tag(NO_TAG) {}
        virtual ~forwarding_base() {}
        // decrement_port_count may create a forwarding task.  If we cannot handle the task
        // ourselves, ask decrement_port_count to deal with it.
        virtual task * decrement_port_count(bool handle_task) = 0;
        virtual void increment_port_count() = 0;
        virtual task * increment_tag_count(tag_value /*t*/, bool /*handle_task*/) {return NULL;}
        // moved here so input ports can queue tasks
        graph* my_graph_ptr;
        tag_value current_tag; // so ports can refer to FE's desired items
    };

    template< int N >
    struct join_helper {

        template< typename TupleType, typename PortType >
        static inline void set_join_node_pointer(TupleType &my_input, PortType *port) {
            tbb::flow::get<N-1>( my_input ).set_join_node_pointer(port);
            join_helper<N-1>::set_join_node_pointer( my_input, port );
        }
        template< typename TupleType >
        static inline void consume_reservations( TupleType &my_input ) {
            tbb::flow::get<N-1>( my_input ).consume();
            join_helper<N-1>::consume_reservations( my_input );
        }

        template< typename TupleType >
        static inline void release_my_reservation( TupleType &my_input ) {
            tbb::flow::get<N-1>( my_input ).release();
        }

        template <typename TupleType>
        static inline void release_reservations( TupleType &my_input) {
            join_helper<N-1>::release_reservations(my_input);
            release_my_reservation(my_input);
        }

        template< typename InputTuple, typename OutputTuple >
        static inline bool reserve( InputTuple &my_input, OutputTuple &out) {
            if ( !tbb::flow::get<N-1>( my_input ).reserve( tbb::flow::get<N-1>( out ) ) ) return false;
            if ( !join_helper<N-1>::reserve( my_input, out ) ) {
                release_my_reservation( my_input );
                return false;
            }
            return true;
        }

        template<typename InputTuple, typename OutputTuple>
        static inline bool get_my_item( InputTuple &my_input, OutputTuple &out) {
            bool res = tbb::flow::get<N-1>(my_input).get_item(tbb::flow::get<N-1>(out) ); // may fail
            return join_helper<N-1>::get_my_item(my_input, out) && res;       // do get on other inputs before returning
        }

        template<typename InputTuple, typename OutputTuple>
        static inline bool get_items(InputTuple &my_input, OutputTuple &out) {
            return get_my_item(my_input, out);
        }

        template<typename InputTuple>
        static inline void reset_my_port(InputTuple &my_input) {
            join_helper<N-1>::reset_my_port(my_input);
            tbb::flow::get<N-1>(my_input).reset_port();
        }

        template<typename InputTuple>
        static inline void reset_ports(InputTuple& my_input) {
            reset_my_port(my_input);
        }

        template<typename InputTuple, typename TagFuncTuple>
        static inline void set_tag_func(InputTuple &my_input, TagFuncTuple &my_tag_funcs) {
            tbb::flow::get<N-1>(my_input).set_my_original_tag_func(tbb::flow::get<N-1>(my_tag_funcs));
            tbb::flow::get<N-1>(my_input).set_my_tag_func(tbb::flow::get<N-1>(my_input).my_original_func()->clone());
            tbb::flow::get<N-1>(my_tag_funcs) = NULL;
            join_helper<N-1>::set_tag_func(my_input, my_tag_funcs);
        }

        template< typename TagFuncTuple1, typename TagFuncTuple2>
        static inline void copy_tag_functors(TagFuncTuple1 &my_inputs, TagFuncTuple2 &other_inputs) {
            if(tbb::flow::get<N-1>(other_inputs).my_original_func()) {
                tbb::flow::get<N-1>(my_inputs).set_my_tag_func(tbb::flow::get<N-1>(other_inputs).my_original_func()->clone());
                tbb::flow::get<N-1>(my_inputs).set_my_original_tag_func(tbb::flow::get<N-1>(other_inputs).my_original_func()->clone());
            }
            join_helper<N-1>::copy_tag_functors(my_inputs, other_inputs);
        }

        template<typename InputTuple>
        static inline void reset_inputs(InputTuple &my_input __TBB_PFG_RESET_ARG(__TBB_COMMA reset_flags f)) {
            join_helper<N-1>::reset_inputs(my_input __TBB_PFG_RESET_ARG(__TBB_COMMA f));
            tbb::flow::get<N-1>(my_input).reset_receiver(__TBB_PFG_RESET_ARG(f));
        }
    };

    template< >
    struct join_helper<1> {

        template< typename TupleType, typename PortType >
        static inline void set_join_node_pointer(TupleType &my_input, PortType *port) {
            tbb::flow::get<0>( my_input ).set_join_node_pointer(port);
        }

        template< typename TupleType >
        static inline void consume_reservations( TupleType &my_input ) {
            tbb::flow::get<0>( my_input ).consume();
        }

        template< typename TupleType >
        static inline void release_my_reservation( TupleType &my_input ) {
            tbb::flow::get<0>( my_input ).release();
        }

        template<typename TupleType>
        static inline void release_reservations( TupleType &my_input) {
            release_my_reservation(my_input);
        }

        template< typename InputTuple, typename OutputTuple >
        static inline bool reserve( InputTuple &my_input, OutputTuple &out) {
            return tbb::flow::get<0>( my_input ).reserve( tbb::flow::get<0>( out ) );
        }

        template<typename InputTuple, typename OutputTuple>
        static inline bool get_my_item( InputTuple &my_input, OutputTuple &out) {
            return tbb::flow::get<0>(my_input).get_item(tbb::flow::get<0>(out));
        }

        template<typename InputTuple, typename OutputTuple>
        static inline bool get_items(InputTuple &my_input, OutputTuple &out) {
            return get_my_item(my_input, out);
        }

        template<typename InputTuple>
        static inline void reset_my_port(InputTuple &my_input) {
            tbb::flow::get<0>(my_input).reset_port();
        }

        template<typename InputTuple>
        static inline void reset_ports(InputTuple& my_input) {
            reset_my_port(my_input);
        }

        template<typename InputTuple, typename TagFuncTuple>
        static inline void set_tag_func(InputTuple &my_input, TagFuncTuple &my_tag_funcs) {
            tbb::flow::get<0>(my_input).set_my_original_tag_func(tbb::flow::get<0>(my_tag_funcs));
            tbb::flow::get<0>(my_input).set_my_tag_func(tbb::flow::get<0>(my_input).my_original_func()->clone());
            tbb::flow::get<0>(my_tag_funcs) = NULL;
        }

        template< typename TagFuncTuple1, typename TagFuncTuple2>
        static inline void copy_tag_functors(TagFuncTuple1 &my_inputs, TagFuncTuple2 &other_inputs) {
            if(tbb::flow::get<0>(other_inputs).my_original_func()) {
                tbb::flow::get<0>(my_inputs).set_my_tag_func(tbb::flow::get<0>(other_inputs).my_original_func()->clone());
                tbb::flow::get<0>(my_inputs).set_my_original_tag_func(tbb::flow::get<0>(other_inputs).my_original_func()->clone());
            }
        }
        template<typename InputTuple>
        static inline void reset_inputs(InputTuple &my_input __TBB_PFG_RESET_ARG(__TBB_COMMA reset_flags f)) {
            tbb::flow::get<0>(my_input).reset_receiver(__TBB_PFG_RESET_ARG(f));
        }
    };

    //! The two-phase join port
    template< typename T >
    class reserving_port : public receiver<T> {
    public:
        typedef T input_type;
        typedef sender<T> predecessor_type;
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
        typedef std::vector<predecessor_type *> predecessor_vector_type;
#endif
    private:
        // ----------- Aggregator ------------
        enum op_type { reg_pred, rem_pred, res_item, rel_res, con_res
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
            , add_blt_pred, del_blt_pred, blt_pred_cnt, blt_pred_cpy
#endif
        };
        enum op_stat {WAIT=0, SUCCEEDED, FAILED};
        typedef reserving_port<T> my_class;

        class reserving_port_operation : public aggregated_operation<reserving_port_operation> {
        public:
            char type;
            union {
                T *my_arg;
                predecessor_type *my_pred;
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
                size_t cnt_val;
                predecessor_vector_type *pvec;
#endif
            };
            reserving_port_operation(const T& e, op_type t) :
                type(char(t)), my_arg(const_cast<T*>(&e)) {}
            reserving_port_operation(const predecessor_type &s, op_type t) : type(char(t)),
                my_pred(const_cast<predecessor_type *>(&s)) {}
            reserving_port_operation(op_type t) : type(char(t)) {}
        };

        typedef internal::aggregating_functor<my_class, reserving_port_operation> my_handler;
        friend class internal::aggregating_functor<my_class, reserving_port_operation>;
        aggregator<my_handler, reserving_port_operation> my_aggregator;

        void handle_operations(reserving_port_operation* op_list) {
            reserving_port_operation *current;
            bool no_predecessors;
            while(op_list) {
                current = op_list;
                op_list = op_list->next;
                switch(current->type) {
                case reg_pred:
                    no_predecessors = my_predecessors.empty();
                    my_predecessors.add(*(current->my_pred));
                    if ( no_predecessors ) {
                        (void) my_join->decrement_port_count(true); // may try to forward
                    }
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case rem_pred:
                    my_predecessors.remove(*(current->my_pred));
                    if(my_predecessors.empty()) my_join->increment_port_count();
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case res_item:
                    if ( reserved ) {
                        __TBB_store_with_release(current->status, FAILED);
                    }
                    else if ( my_predecessors.try_reserve( *(current->my_arg) ) ) {
                        reserved = true;
                        __TBB_store_with_release(current->status, SUCCEEDED);
                    } else {
                        if ( my_predecessors.empty() ) {
                            my_join->increment_port_count();
                        }
                        __TBB_store_with_release(current->status, FAILED);
                    }
                    break;
                case rel_res:
                    reserved = false;
                    my_predecessors.try_release( );
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case con_res:
                    reserved = false;
                    my_predecessors.try_consume( );
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
                case add_blt_pred:
                    my_predecessors.internal_add_built_predecessor(*(current->my_pred));
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case del_blt_pred:
                    my_predecessors.internal_delete_built_predecessor(*(current->my_pred));
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case blt_pred_cnt:
                    current->cnt_val = my_predecessors.predecessor_count();
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case blt_pred_cpy:
                    my_predecessors.copy_predecessors(*(current->pvec));
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
#endif  /* TBB_PREVIEW_FLOW_GRAPH_FEATURES */
                }
            }
        }

    protected:
        template< typename R, typename B > friend class run_and_put_task;
        template<typename X, typename Y> friend class internal::broadcast_cache;
        template<typename X, typename Y> friend class internal::round_robin_cache;
        task *try_put_task( const T & ) {
            return NULL;
        }

    public:

        //! Constructor
        reserving_port() : reserved(false) {
            my_join = NULL;
            my_predecessors.set_owner( this );
            my_aggregator.initialize_handler(my_handler(this));
        }

        // copy constructor
        reserving_port(const reserving_port& /* other */) : receiver<T>() {
            reserved = false;
            my_join = NULL;
            my_predecessors.set_owner( this );
            my_aggregator.initialize_handler(my_handler(this));
        }

        void set_join_node_pointer(forwarding_base *join) {
            my_join = join;
        }

        //! Add a predecessor
        bool register_predecessor( sender<T> &src ) {
            reserving_port_operation op_data(src, reg_pred);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        //! Remove a predecessor
        bool remove_predecessor( sender<T> &src ) {
            reserving_port_operation op_data(src, rem_pred);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        //! Reserve an item from the port
        bool reserve( T &v ) {
            reserving_port_operation op_data(v, res_item);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        //! Release the port
        void release( ) {
            reserving_port_operation op_data(rel_res);
            my_aggregator.execute(&op_data);
        }

        //! Complete use of the port
        void consume( ) {
            reserving_port_operation op_data(con_res);
            my_aggregator.execute(&op_data);
        }

#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
        /*override*/void internal_add_built_predecessor(predecessor_type &src) {
            reserving_port_operation op_data(src, add_blt_pred);
            my_aggregator.execute(&op_data);
        }

        /*override*/void internal_delete_built_predecessor(predecessor_type &src) {
            reserving_port_operation op_data(src, del_blt_pred);
            my_aggregator.execute(&op_data);
        }

        /*override*/size_t predecessor_count() {
            reserving_port_operation op_data(blt_pred_cnt);
            my_aggregator.execute(&op_data);
            return op_data.cnt_val;
        }

        /*override*/void copy_predecessors(predecessor_vector_type &v) {
            reserving_port_operation op_data(blt_pred_cpy);
            op_data.pvec = &v;
            my_aggregator.execute(&op_data);
        }
#endif  /* TBB_PREVIEW_FLOW_GRAPH_FEATURES */

        /*override*/void reset_receiver( __TBB_PFG_RESET_ARG(reset_flags f)) {
            my_predecessors.reset(__TBB_PFG_RESET_ARG(f));
            reserved = false;
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
            __TBB_ASSERT(!(f&rf_extract) || my_predecessors.empty(), "port edges not removed");
#endif
        }

    private:
        forwarding_base *my_join;
        reservable_predecessor_cache< T, null_mutex > my_predecessors;
        bool reserved;
    };

    //! queueing join_port
    template<typename T>
    class queueing_port : public receiver<T>, public item_buffer<T> {
    public:
        typedef T input_type;
        typedef sender<T> predecessor_type;
        typedef queueing_port<T> my_node_type;
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
        typedef std::vector<predecessor_type *> predecessor_vector_type;
#endif

    // ----------- Aggregator ------------
    private:
        enum op_type { get__item, res_port, try__put_task
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
            , add_blt_pred, del_blt_pred, blt_pred_cnt, blt_pred_cpy 
#endif
        };
        enum op_stat {WAIT=0, SUCCEEDED, FAILED};
        typedef queueing_port<T> my_class;

        class queueing_port_operation : public aggregated_operation<queueing_port_operation> {
        public:
            char type;
            T my_val;
            T *my_arg;
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
            sender<T> *pred;
            size_t cnt_val;
            predecessor_vector_type *pvec;
#endif
            task * bypass_t;
            // constructor for value parameter
            queueing_port_operation(const T& e, op_type t) :
                type(char(t)), my_val(e)
                , bypass_t(NULL)
            {}
            // constructor for pointer parameter
            queueing_port_operation(const T* p, op_type t) :
                type(char(t)), my_arg(const_cast<T*>(p))
                , bypass_t(NULL)
            {}
            // constructor with no parameter
            queueing_port_operation(op_type t) : type(char(t))
                , bypass_t(NULL)
            {}
        };

        typedef internal::aggregating_functor<my_class, queueing_port_operation> my_handler;
        friend class internal::aggregating_functor<my_class, queueing_port_operation>;
        aggregator<my_handler, queueing_port_operation> my_aggregator;

        void handle_operations(queueing_port_operation* op_list) {
            queueing_port_operation *current;
            bool was_empty;
            while(op_list) {
                current = op_list;
                op_list = op_list->next;
                switch(current->type) {
                case try__put_task: {
                        task *rtask = NULL;
                        was_empty = this->buffer_empty();
                        this->push_back(current->my_val);
                        if (was_empty) rtask = my_join->decrement_port_count(false);
                        else
                            rtask = SUCCESSFULLY_ENQUEUED;
                        current->bypass_t = rtask;
                        __TBB_store_with_release(current->status, SUCCEEDED);
                    }
                    break;
                case get__item:
                    if(!this->buffer_empty()) {
                        this->copy_front(*(current->my_arg));
                        __TBB_store_with_release(current->status, SUCCEEDED);
                    }
                    else {
                        __TBB_store_with_release(current->status, FAILED);
                    }
                    break;
                case res_port:
                    __TBB_ASSERT(this->my_item_valid(this->my_head), "No item to reset");
                    this->destroy_front();
                    if(this->my_item_valid(this->my_head)) {
                        (void)my_join->decrement_port_count(true);
                    }
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
                case add_blt_pred:
                    my_built_predecessors.add_edge(*(current->pred));
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case del_blt_pred:
                    my_built_predecessors.delete_edge(*(current->pred));
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case blt_pred_cnt:
                    current->cnt_val = my_built_predecessors.edge_count();
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case blt_pred_cpy:
                    my_built_predecessors.copy_edges(*(current->pvec));
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
#endif  /* TBB_PREVIEW_FLOW_GRAPH_FEATURES */
                }
            }
        }
    // ------------ End Aggregator ---------------

    protected:
        template< typename R, typename B > friend class run_and_put_task;
        template<typename X, typename Y> friend class internal::broadcast_cache;
        template<typename X, typename Y> friend class internal::round_robin_cache;
        /*override*/task *try_put_task(const T &v) {
            queueing_port_operation op_data(v, try__put_task);
            my_aggregator.execute(&op_data);
            __TBB_ASSERT(op_data.status == SUCCEEDED || !op_data.bypass_t, "inconsistent return from aggregator");
            if(!op_data.bypass_t) return SUCCESSFULLY_ENQUEUED;
            return op_data.bypass_t;
        }

    public:

        //! Constructor
        queueing_port() : item_buffer<T>() {
            my_join = NULL;
            my_aggregator.initialize_handler(my_handler(this));
        }

        //! copy constructor
        queueing_port(const queueing_port& /* other */) : receiver<T>(), item_buffer<T>() {
            my_join = NULL;
            my_aggregator.initialize_handler(my_handler(this));
        }

        //! record parent for tallying available items
        void set_join_node_pointer(forwarding_base *join) {
            my_join = join;
        }

        bool get_item( T &v ) {
            queueing_port_operation op_data(&v, get__item);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        // reset_port is called when item is accepted by successor, but
        // is initiated by join_node.
        void reset_port() {
            queueing_port_operation op_data(res_port);
            my_aggregator.execute(&op_data);
            return;
        }

#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
        /*override*/void internal_add_built_predecessor(sender<T> &p) {
            queueing_port_operation op_data(add_blt_pred);
            op_data.pred = &p;
            my_aggregator.execute(&op_data);
        }

        /*override*/void internal_delete_built_predecessor(sender<T> &p) {
            queueing_port_operation op_data(del_blt_pred);
            op_data.pred = &p;
            my_aggregator.execute(&op_data);
        }

        /*override*/size_t predecessor_count() {
            queueing_port_operation op_data(blt_pred_cnt);
            my_aggregator.execute(&op_data);
            return op_data.cnt_val;
        }

        /*override*/void copy_predecessors(predecessor_vector_type &v) {
            queueing_port_operation op_data(blt_pred_cpy);
            op_data.pvec = &v;
            my_aggregator.execute(&op_data);
        }

        /*override*/void reset_receiver(__TBB_PFG_RESET_ARG(reset_flags f)) { 
            item_buffer<T>::reset(); 
            if (f & rf_extract)
                my_built_predecessors.receiver_extract(*this);
        }
#else
        /*override*/void reset_receiver(__TBB_PFG_RESET_ARG(reset_flags /*f*/)) { item_buffer<T>::reset(); }
#endif  /* TBB_PREVIEW_FLOW_GRAPH_FEATURES */

    private:
        forwarding_base *my_join;
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
        edge_container<sender<T> > my_built_predecessors;
#endif
    };

#include "_flow_graph_tagged_buffer_impl.h"

    template< typename T >
    class tag_matching_port : public receiver<T>, public tagged_buffer< tag_value, T, NO_TAG > {
    public:
        typedef T input_type;
        typedef sender<T> predecessor_type;
        typedef tag_matching_port<T> my_node_type;  // for forwarding, if needed
        typedef function_body<input_type, tag_value> my_tag_func_type;
        typedef tagged_buffer<tag_value,T,NO_TAG> my_buffer_type;
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
        typedef std::vector<predecessor_type *> predecessor_vector_type;
#endif
    private:
// ----------- Aggregator ------------
    private:
        enum op_type { try__put, get__item, res_port,
            add_blt_pred, del_blt_pred, blt_pred_cnt, blt_pred_cpy
        };
        enum op_stat {WAIT=0, SUCCEEDED, FAILED};
        typedef tag_matching_port<T> my_class;

        class tag_matching_port_operation : public aggregated_operation<tag_matching_port_operation> {
        public:
            char type;
            T my_val;
            T *my_arg;
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
            predecessor_type *pred;
            size_t cnt_val;
            predecessor_vector_type *pvec;
#endif
            tag_value my_tag_value;
            // constructor for value parameter
            tag_matching_port_operation(const T& e, op_type t) :
                type(char(t)), my_val(e) {}
            // constructor for pointer parameter
            tag_matching_port_operation(const T* p, op_type t) :
                type(char(t)), my_arg(const_cast<T*>(p)) {}
            // constructor with no parameter
            tag_matching_port_operation(op_type t) : type(char(t)) {}
        };

        typedef internal::aggregating_functor<my_class, tag_matching_port_operation> my_handler;
        friend class internal::aggregating_functor<my_class, tag_matching_port_operation>;
        aggregator<my_handler, tag_matching_port_operation> my_aggregator;

        void handle_operations(tag_matching_port_operation* op_list) {
            tag_matching_port_operation *current;
            while(op_list) {
                current = op_list;
                op_list = op_list->next;
                switch(current->type) {
                case try__put: {
                        bool was_inserted = this->tagged_insert(current->my_tag_value, current->my_val);
                        // return failure if a duplicate insertion occurs
                        __TBB_store_with_release(current->status, was_inserted ? SUCCEEDED : FAILED);
                    }
                    break;
                case get__item:
                    // use current_tag from FE for item
                    if(!this->tagged_find(my_join->current_tag, *(current->my_arg))) {
                        __TBB_ASSERT(false, "Failed to find item corresponding to current_tag.");
                    }
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case res_port:
                    // use current_tag from FE for item
                    this->tagged_delete(my_join->current_tag);
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
                case add_blt_pred:
                    my_built_predecessors.add_edge(*(current->pred));
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case del_blt_pred:
                    my_built_predecessors.delete_edge(*(current->pred));
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case blt_pred_cnt:
                    current->cnt_val = my_built_predecessors.edge_count();
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case blt_pred_cpy:
                    my_built_predecessors.copy_edges(*(current->pvec));
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
#endif
                }
            }
        }
// ------------ End Aggregator ---------------
    protected:
        template< typename R, typename B > friend class run_and_put_task;
        template<typename X, typename Y> friend class internal::broadcast_cache;
        template<typename X, typename Y> friend class internal::round_robin_cache;
        /*override*/task *try_put_task(const T& v) {
            tag_matching_port_operation op_data(v, try__put);
            op_data.my_tag_value = (*my_tag_func)(v);
            task *rtask = NULL;
            my_aggregator.execute(&op_data);
            if(op_data.status == SUCCEEDED) {
                rtask = my_join->increment_tag_count(op_data.my_tag_value, false);  // may spawn
                // rtask has to reflect the return status of the try_put
                if(!rtask) rtask = SUCCESSFULLY_ENQUEUED;
            }
            return rtask;
        }

    public:

        tag_matching_port() : receiver<T>(), tagged_buffer<tag_value, T, NO_TAG>() {
            my_join = NULL;
            my_tag_func = NULL;
            my_original_tag_func = NULL;
            my_aggregator.initialize_handler(my_handler(this));
        }

        // copy constructor
        tag_matching_port(const tag_matching_port& /*other*/) : receiver<T>(), tagged_buffer<tag_value,T, NO_TAG>() {
            my_join = NULL;
            // setting the tag methods is done in the copy-constructor for the front-end.
            my_tag_func = NULL;
            my_original_tag_func = NULL;
            my_aggregator.initialize_handler(my_handler(this));
        }

        ~tag_matching_port() {
            if (my_tag_func) delete my_tag_func;
            if (my_original_tag_func) delete my_original_tag_func;
        }

        void set_join_node_pointer(forwarding_base *join) {
            my_join = join;
        }

        void set_my_original_tag_func(my_tag_func_type *f) {
            my_original_tag_func = f;
        }

        void set_my_tag_func(my_tag_func_type *f) {
            my_tag_func = f;
        }

        bool get_item( T &v ) {
            tag_matching_port_operation op_data(&v, get__item);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
        /*override*/void internal_add_built_predecessor(sender<T> &p) {
            tag_matching_port_operation op_data(add_blt_pred);
            op_data.pred = &p;
            my_aggregator.execute(&op_data);
        }

        /*override*/void internal_delete_built_predecessor(sender<T> &p) {
            tag_matching_port_operation op_data(del_blt_pred);
            op_data.pred = &p;
            my_aggregator.execute(&op_data);
        }

        /*override*/size_t predecessor_count() {
            tag_matching_port_operation op_data(blt_pred_cnt);
            my_aggregator.execute(&op_data);
            return op_data.cnt_val;
        }

        /*override*/void copy_predecessors(predecessor_vector_type &v) {
            tag_matching_port_operation op_data(blt_pred_cpy);
            op_data.pvec = &v;
            my_aggregator.execute(&op_data);
        }
#endif

        // reset_port is called when item is accepted by successor, but
        // is initiated by join_node.
        void reset_port() {
            tag_matching_port_operation op_data(res_port);
            my_aggregator.execute(&op_data);
            return;
        }

        my_tag_func_type *my_func() { return my_tag_func; }
        my_tag_func_type *my_original_func() { return my_original_tag_func; }

#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
        /*override*/void reset_receiver(__TBB_PFG_RESET_ARG(reset_flags f)) { 
            my_buffer_type::reset(); 
           if (f & rf_extract)
              my_built_predecessors.receiver_extract(*this);
        }
#else
        /*override*/void reset_receiver(__TBB_PFG_RESET_ARG(reset_flags /*f*/)) { my_buffer_type::reset(); }
#endif

    private:
        // need map of tags to values
        forwarding_base *my_join;
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
        edge_container<predecessor_type> my_built_predecessors;
#endif
        my_tag_func_type *my_tag_func;
        my_tag_func_type *my_original_tag_func;
    };  // tag_matching_port

    using namespace graph_policy_namespace;

    template<graph_buffer_policy JP, typename InputTuple, typename OutputTuple>
    class join_node_base;

    //! join_node_FE : implements input port policy
    template<graph_buffer_policy JP, typename InputTuple, typename OutputTuple>
    class join_node_FE;

    template<typename InputTuple, typename OutputTuple>
    class join_node_FE<reserving, InputTuple, OutputTuple> : public forwarding_base {
    public:
        static const int N = tbb::flow::tuple_size<OutputTuple>::value;
        typedef OutputTuple output_type;
        typedef InputTuple input_type;
        typedef join_node_base<reserving, InputTuple, OutputTuple> my_node_type; // for forwarding

        join_node_FE(graph &g) : forwarding_base(g), my_node(NULL) {
            ports_with_no_inputs = N;
            join_helper<N>::set_join_node_pointer(my_inputs, this);
        }

        join_node_FE(const join_node_FE& other) : forwarding_base(*(other.forwarding_base::my_graph_ptr)), my_node(NULL) {
            ports_with_no_inputs = N;
            join_helper<N>::set_join_node_pointer(my_inputs, this);
        }

        void set_my_node(my_node_type *new_my_node) { my_node = new_my_node; }

       void increment_port_count() {
            ++ports_with_no_inputs;
        }

        // if all input_ports have predecessors, spawn forward to try and consume tuples
        task * decrement_port_count(bool handle_task) {
            if(ports_with_no_inputs.fetch_and_decrement() == 1) {
                task* tp = this->my_graph_ptr->root_task();
                if(tp) {
                    task *rtask = new ( task::allocate_additional_child_of( *tp ) )
                        forward_task_bypass<my_node_type>(*my_node);
                    if(!handle_task) return rtask;
                    FLOW_SPAWN(*rtask);
                }
            }
            return NULL;
        }

        input_type &input_ports() { return my_inputs; }

    protected:

        void reset( __TBB_PFG_RESET_ARG( reset_flags f)) {
            // called outside of parallel contexts
            ports_with_no_inputs = N;
            join_helper<N>::reset_inputs(my_inputs __TBB_PFG_RESET_ARG( __TBB_COMMA f));
        }

        // all methods on input ports should be called under mutual exclusion from join_node_base.

        bool tuple_build_may_succeed() {
            return !ports_with_no_inputs;
        }

        bool try_to_make_tuple(output_type &out) {
            if(ports_with_no_inputs) return false;
            return join_helper<N>::reserve(my_inputs, out);
        }

        void tuple_accepted() {
            join_helper<N>::consume_reservations(my_inputs);
        }
        void tuple_rejected() {
            join_helper<N>::release_reservations(my_inputs);
        }

        input_type my_inputs;
        my_node_type *my_node;
        atomic<size_t> ports_with_no_inputs;
    };

    template<typename InputTuple, typename OutputTuple>
    class join_node_FE<queueing, InputTuple, OutputTuple> : public forwarding_base {
    public:
        static const int N = tbb::flow::tuple_size<OutputTuple>::value;
        typedef OutputTuple output_type;
        typedef InputTuple input_type;
        typedef join_node_base<queueing, InputTuple, OutputTuple> my_node_type; // for forwarding

        join_node_FE(graph &g) : forwarding_base(g), my_node(NULL) {
            ports_with_no_items = N;
            join_helper<N>::set_join_node_pointer(my_inputs, this);
        }

        join_node_FE(const join_node_FE& other) : forwarding_base(*(other.forwarding_base::my_graph_ptr)), my_node(NULL) {
            ports_with_no_items = N;
            join_helper<N>::set_join_node_pointer(my_inputs, this);
        }

        // needed for forwarding
        void set_my_node(my_node_type *new_my_node) { my_node = new_my_node; }

        void reset_port_count() {
            ports_with_no_items = N;
        }

        // if all input_ports have items, spawn forward to try and consume tuples
        task * decrement_port_count(bool handle_task)
        {
            if(ports_with_no_items.fetch_and_decrement() == 1) {
                task* tp = this->my_graph_ptr->root_task();
                if(tp) {
                    task *rtask = new ( task::allocate_additional_child_of( *tp ) )
                        forward_task_bypass <my_node_type>(*my_node);
                    if(!handle_task) return rtask;
                    FLOW_SPAWN( *rtask);
                }
            }
            return NULL;
        }

        void increment_port_count() { __TBB_ASSERT(false, NULL); }  // should never be called

        input_type &input_ports() { return my_inputs; }

    protected:

        void reset( __TBB_PFG_RESET_ARG( reset_flags f)) {
            reset_port_count();
            join_helper<N>::reset_inputs(my_inputs __TBB_PFG_RESET_ARG( __TBB_COMMA f) );
        }

        // all methods on input ports should be called under mutual exclusion from join_node_base.

        bool tuple_build_may_succeed() {
            return !ports_with_no_items;
        }

        bool try_to_make_tuple(output_type &out) {
            if(ports_with_no_items) return false;
            return join_helper<N>::get_items(my_inputs, out);
        }

        void tuple_accepted() {
            reset_port_count();
            join_helper<N>::reset_ports(my_inputs);
        }
        void tuple_rejected() {
            // nothing to do.
        }

        input_type my_inputs;
        my_node_type *my_node;
        atomic<size_t> ports_with_no_items;
    };

    // tag_matching join input port.
    template<typename InputTuple, typename OutputTuple>
    class join_node_FE<tag_matching, InputTuple, OutputTuple> : public forwarding_base,
             //     buffer of tag value counts                       buffer of output items
             public tagged_buffer<tag_value, size_t, NO_TAG>, public item_buffer<OutputTuple> {
    public:
        static const int N = tbb::flow::tuple_size<OutputTuple>::value;
        typedef OutputTuple output_type;
        typedef InputTuple input_type;
        typedef tagged_buffer<tag_value, size_t, NO_TAG> my_tag_buffer;
        typedef item_buffer<output_type> output_buffer_type;
        typedef join_node_base<tag_matching, InputTuple, OutputTuple> my_node_type; // for forwarding

// ----------- Aggregator ------------
        // the aggregator is only needed to serialize the access to the hash table.
        // and the output_buffer_type base class
    private:
        enum op_type { res_count, inc_count, may_succeed, try_make };
        enum op_stat {WAIT=0, SUCCEEDED, FAILED};
        typedef join_node_FE<tag_matching, InputTuple, OutputTuple> my_class;

        class tag_matching_FE_operation : public aggregated_operation<tag_matching_FE_operation> {
        public:
            char type;
            union {
                tag_value my_val;
                output_type* my_output;
            };
            task *bypass_t;
            bool enqueue_task;
            // constructor for value parameter
            tag_matching_FE_operation(const tag_value& e , bool q_task , op_type t) : type(char(t)), my_val(e),
                 bypass_t(NULL), enqueue_task(q_task) {}
            tag_matching_FE_operation(output_type *p, op_type t) : type(char(t)), my_output(p), bypass_t(NULL),
                 enqueue_task(true) {}
            // constructor with no parameter
            tag_matching_FE_operation(op_type t) : type(char(t)), bypass_t(NULL), enqueue_task(true) {}
        };

        typedef internal::aggregating_functor<my_class, tag_matching_FE_operation> my_handler;
        friend class internal::aggregating_functor<my_class, tag_matching_FE_operation>;
        aggregator<my_handler, tag_matching_FE_operation> my_aggregator;

        // called from aggregator, so serialized
        // construct as many output objects as possible.
        // returns a task pointer if the a task would have been enqueued but we asked that
        // it be returned.  Otherwise returns NULL.
        task * fill_output_buffer(tag_value t, bool should_enqueue, bool handle_task) {
            output_type l_out;
            task *rtask = NULL;
            task* tp = this->my_graph_ptr->root_task();
            bool do_fwd = should_enqueue && this->buffer_empty() && tp;
            this->current_tag = t;
            this->tagged_delete(this->current_tag);   // remove the tag
            if(join_helper<N>::get_items(my_inputs, l_out)) {  //  <== call back
                this->push_back(l_out);
                if(do_fwd) {  // we enqueue if receiving an item from predecessor, not if successor asks for item
                    rtask = new ( task::allocate_additional_child_of( *tp ) )
                        forward_task_bypass<my_node_type>(*my_node);
                    if(handle_task) {
                        FLOW_SPAWN(*rtask);
                        rtask = NULL;
                    }
                    do_fwd = false;
                }
                // retire the input values
                join_helper<N>::reset_ports(my_inputs);  //  <== call back
                this->current_tag = NO_TAG;
            }
            else {
                __TBB_ASSERT(false, "should have had something to push");
            }
            return rtask;
        }

        void handle_operations(tag_matching_FE_operation* op_list) {
            tag_matching_FE_operation *current;
            while(op_list) {
                current = op_list;
                op_list = op_list->next;
                switch(current->type) {
                case res_count:  // called from BE
                    {
                        this->destroy_front();
                        __TBB_store_with_release(current->status, SUCCEEDED);
                    }
                    break;
                case inc_count: {  // called from input ports
                        size_t *p = 0;
                        tag_value t = current->my_val;
                        bool do_enqueue = current->enqueue_task;
                        if(!(this->tagged_find_ref(t,p))) {
                            this->tagged_insert(t, 0);
                            if(!(this->tagged_find_ref(t,p))) {
                                __TBB_ASSERT(false, "should find tag after inserting it");
                            }
                        }
                        if(++(*p) == size_t(N)) {
                            task *rtask = fill_output_buffer(t, true, do_enqueue);
                            __TBB_ASSERT(!rtask || !do_enqueue, "task should not be returned");
                            current->bypass_t = rtask;
                        }
                    }
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case may_succeed:  // called from BE
                    __TBB_store_with_release(current->status, this->buffer_empty() ? FAILED : SUCCEEDED);
                    break;
                case try_make:  // called from BE
                    if(this->buffer_empty()) {
                        __TBB_store_with_release(current->status, FAILED);
                    }
                    else {
                        this->copy_front(*(current->my_output));
                        __TBB_store_with_release(current->status, SUCCEEDED);
                    }
                    break;
                }
            }
        }
// ------------ End Aggregator ---------------

    public:
        template<typename FunctionTuple>
        join_node_FE(graph &g, FunctionTuple tag_funcs) : forwarding_base(g), my_node(NULL) {
            join_helper<N>::set_join_node_pointer(my_inputs, this);
            join_helper<N>::set_tag_func(my_inputs, tag_funcs);
            my_aggregator.initialize_handler(my_handler(this));
        }

        join_node_FE(const join_node_FE& other) : forwarding_base(*(other.forwarding_base::my_graph_ptr)), my_tag_buffer(),
        output_buffer_type() {
            my_node = NULL;
            join_helper<N>::set_join_node_pointer(my_inputs, this);
            join_helper<N>::copy_tag_functors(my_inputs, const_cast<input_type &>(other.my_inputs));
            my_aggregator.initialize_handler(my_handler(this));
        }

        // needed for forwarding
        void set_my_node(my_node_type *new_my_node) { my_node = new_my_node; }

        void reset_port_count() {  // called from BE
            tag_matching_FE_operation op_data(res_count);
            my_aggregator.execute(&op_data);
            return;
        }

        // if all input_ports have items, spawn forward to try and consume tuples
        // return a task if we are asked and did create one.
        task *increment_tag_count(tag_value t, bool handle_task) {  // called from input_ports
            tag_matching_FE_operation op_data(t, handle_task, inc_count);
            my_aggregator.execute(&op_data);
            return op_data.bypass_t;
        }

        /*override*/ task *decrement_port_count(bool /*handle_task*/) { __TBB_ASSERT(false, NULL); return NULL; }

        void increment_port_count() { __TBB_ASSERT(false, NULL); }  // should never be called

        input_type &input_ports() { return my_inputs; }

    protected:

        void reset( __TBB_PFG_RESET_ARG( reset_flags f )) {
            // called outside of parallel contexts
            join_helper<N>::reset_inputs(my_inputs __TBB_PFG_RESET_ARG( __TBB_COMMA f));

            my_tag_buffer::reset();  // have to reset the tag counts
            output_buffer_type::reset();  // also the queue of outputs
            my_node->current_tag = NO_TAG;
        }

        // all methods on input ports should be called under mutual exclusion from join_node_base.

        bool tuple_build_may_succeed() {  // called from back-end
            tag_matching_FE_operation op_data(may_succeed);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        // cannot lock while calling back to input_ports.  current_tag will only be set
        // and reset under the aggregator, so it will remain consistent.
        bool try_to_make_tuple(output_type &out) {
            tag_matching_FE_operation op_data(&out,try_make);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        void tuple_accepted() {
            reset_port_count();  // reset current_tag after ports reset.
        }

        void tuple_rejected() {
            // nothing to do.
        }

        input_type my_inputs;  // input ports
        my_node_type *my_node;
    }; // join_node_FE<tag_matching, InputTuple, OutputTuple>

    //! join_node_base
    template<graph_buffer_policy JP, typename InputTuple, typename OutputTuple>
    class join_node_base : public graph_node, public join_node_FE<JP, InputTuple, OutputTuple>,
                           public sender<OutputTuple> {
    protected:
        using graph_node::my_graph;
    public:
        typedef OutputTuple output_type;

        typedef receiver<output_type> successor_type;
        typedef join_node_FE<JP, InputTuple, OutputTuple> input_ports_type;
        using input_ports_type::tuple_build_may_succeed;
        using input_ports_type::try_to_make_tuple;
        using input_ports_type::tuple_accepted;
        using input_ports_type::tuple_rejected;
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
        typedef std::vector<successor_type *> successor_vector_type;
#endif

    private:
        // ----------- Aggregator ------------
        enum op_type { reg_succ, rem_succ, try__get, do_fwrd, do_fwrd_bypass
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
            , add_blt_succ, del_blt_succ, blt_succ_cnt, blt_succ_cpy
#endif
        };
        enum op_stat {WAIT=0, SUCCEEDED, FAILED};
        typedef join_node_base<JP,InputTuple,OutputTuple> my_class;

        class join_node_base_operation : public aggregated_operation<join_node_base_operation> {
        public:
            char type;
            union {
                output_type *my_arg;
                successor_type *my_succ;
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
                size_t cnt_val;
                successor_vector_type *svec;
#endif
            };
            task *bypass_t;
            join_node_base_operation(const output_type& e, op_type t) : type(char(t)),
                my_arg(const_cast<output_type*>(&e)), bypass_t(NULL) {}
            join_node_base_operation(const successor_type &s, op_type t) : type(char(t)),
                my_succ(const_cast<successor_type *>(&s)), bypass_t(NULL) {}
            join_node_base_operation(op_type t) : type(char(t)), bypass_t(NULL) {}
        };

        typedef internal::aggregating_functor<my_class, join_node_base_operation> my_handler;
        friend class internal::aggregating_functor<my_class, join_node_base_operation>;
        bool forwarder_busy;
        aggregator<my_handler, join_node_base_operation> my_aggregator;

        void handle_operations(join_node_base_operation* op_list) {
            join_node_base_operation *current;
            while(op_list) {
                current = op_list;
                op_list = op_list->next;
                switch(current->type) {
                case reg_succ: {
                        my_successors.register_successor(*(current->my_succ));
                        task* tp = this->graph_node::my_graph.root_task();
                        if(tuple_build_may_succeed() && !forwarder_busy && tp) {
                            task *rtask = new ( task::allocate_additional_child_of(*tp) )
                                    forward_task_bypass
                                    <join_node_base<JP,InputTuple,OutputTuple> >(*this);
                            FLOW_SPAWN(*rtask);
                            forwarder_busy = true;
                        }
                        __TBB_store_with_release(current->status, SUCCEEDED);
                    }
                    break;
                case rem_succ:
                    my_successors.remove_successor(*(current->my_succ));
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case try__get:
                    if(tuple_build_may_succeed()) {
                        if(try_to_make_tuple(*(current->my_arg))) {
                            tuple_accepted();
                            __TBB_store_with_release(current->status, SUCCEEDED);
                        }
                        else __TBB_store_with_release(current->status, FAILED);
                    }
                    else __TBB_store_with_release(current->status, FAILED);
                    break;
                case do_fwrd_bypass: {
                        bool build_succeeded;
                        task *last_task = NULL;
                        output_type out;
                        if(tuple_build_may_succeed()) {
                            do {
                                build_succeeded = try_to_make_tuple(out);
                                if(build_succeeded) {
                                    task *new_task = my_successors.try_put_task(out);
                                    last_task = combine_tasks(last_task, new_task);
                                    if(new_task) {
                                        tuple_accepted();
                                    }
                                    else {
                                        tuple_rejected();
                                        build_succeeded = false;
                                    }
                                }
                            } while(build_succeeded);
                        }
                        current->bypass_t = last_task;
                        __TBB_store_with_release(current->status, SUCCEEDED);
                        forwarder_busy = false;
                    }
                    break;
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
                case add_blt_succ:
                    my_successors.internal_add_built_successor(*(current->my_succ));
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case del_blt_succ:
                    my_successors.internal_delete_built_successor(*(current->my_succ));
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case blt_succ_cnt:
                    current->cnt_val = my_successors.successor_count();
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
                case blt_succ_cpy:
                    my_successors.copy_successors(*(current->svec));
                    __TBB_store_with_release(current->status, SUCCEEDED);
                    break;
#endif  /* TBB_PREVIEW_FLOW_GRAPH_FEATURES */
                }
            }
        }
        // ---------- end aggregator -----------
    public:
        join_node_base(graph &g) : graph_node(g), input_ports_type(g), forwarder_busy(false) {
            my_successors.set_owner(this);
            input_ports_type::set_my_node(this);
            my_aggregator.initialize_handler(my_handler(this));
        }

        join_node_base(const join_node_base& other) :
            graph_node(other.graph_node::my_graph), input_ports_type(other),
            sender<OutputTuple>(), forwarder_busy(false), my_successors() {
            my_successors.set_owner(this);
            input_ports_type::set_my_node(this);
            my_aggregator.initialize_handler(my_handler(this));
        }

        template<typename FunctionTuple>
        join_node_base(graph &g, FunctionTuple f) : graph_node(g), input_ports_type(g, f), forwarder_busy(false) {
            my_successors.set_owner(this);
            input_ports_type::set_my_node(this);
            my_aggregator.initialize_handler(my_handler(this));
        }

        bool register_successor(successor_type &r) {
            join_node_base_operation op_data(r, reg_succ);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        bool remove_successor( successor_type &r) {
            join_node_base_operation op_data(r, rem_succ);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

        bool try_get( output_type &v) {
            join_node_base_operation op_data(v, try__get);
            my_aggregator.execute(&op_data);
            return op_data.status == SUCCEEDED;
        }

#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
        /*override*/void internal_add_built_successor( successor_type &r) {
            join_node_base_operation op_data(r, add_blt_succ);
            my_aggregator.execute(&op_data);
        }

        /*override*/void internal_delete_built_successor( successor_type &r) {
            join_node_base_operation op_data(r, del_blt_succ);
            my_aggregator.execute(&op_data);
        }

        /*override*/size_t successor_count() {
            join_node_base_operation op_data(blt_succ_cnt);
            my_aggregator.execute(&op_data);
            return op_data.cnt_val;
        }

        /*override*/ void copy_successors(successor_vector_type &v) {
            join_node_base_operation op_data(blt_succ_cpy);
            op_data.svec = &v;
            my_aggregator.execute(&op_data);
        }
#endif  /* TBB_PREVIEW_FLOW_GRAPH_FEATURES */

    protected:

        /*override*/void reset(__TBB_PFG_RESET_ARG(reset_flags f)) {
            input_ports_type::reset(__TBB_PFG_RESET_ARG(f));
#if TBB_PREVIEW_FLOW_GRAPH_FEATURES
            my_successors.reset(f);
#endif
        }

    private:
        broadcast_cache<output_type, null_rw_mutex> my_successors;

        friend class forward_task_bypass< join_node_base<JP, InputTuple, OutputTuple> >;
        task *forward_task() {
            join_node_base_operation op_data(do_fwrd_bypass);
            my_aggregator.execute(&op_data);
            return op_data.bypass_t;
        }

    };

    // join base class type generator
    template<int N, template<class> class PT, typename OutputTuple, graph_buffer_policy JP>
    struct join_base {
        typedef typename internal::join_node_base<JP, typename wrap_tuple_elements<N,PT,OutputTuple>::type, OutputTuple> type;
    };

    //! unfolded_join_node : passes input_ports_type to join_node_base.  We build the input port type
    //  using tuple_element.  The class PT is the port type (reserving_port, queueing_port, tag_matching_port)
    //  and should match the graph_buffer_policy.

    template<int N, template<class> class PT, typename OutputTuple, graph_buffer_policy JP>
    class unfolded_join_node : public join_base<N,PT,OutputTuple,JP>::type {
    public:
        typedef typename wrap_tuple_elements<N, PT, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<JP, input_ports_type, output_type > base_type;
    public:
        unfolded_join_node(graph &g) : base_type(g) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };

    // tag_matching unfolded_join_node.  This must be a separate specialization because the constructors
    // differ.

    template<typename OutputTuple>
    class unfolded_join_node<2,tag_matching_port,OutputTuple,tag_matching> : public
            join_base<2,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename tbb::flow::tuple_element<0, OutputTuple>::type T0;
        typedef typename tbb::flow::tuple_element<1, OutputTuple>::type T1;
    public:
        typedef typename wrap_tuple_elements<2,tag_matching_port,OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename tbb::flow::tuple< f0_p, f1_p > func_initializer_type;
    public:
        template<typename B0, typename B1>
        unfolded_join_node(graph &g, B0 b0, B1 b1) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };

    template<typename OutputTuple>
    class unfolded_join_node<3,tag_matching_port,OutputTuple,tag_matching> : public
            join_base<3,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename tbb::flow::tuple_element<0, OutputTuple>::type T0;
        typedef typename tbb::flow::tuple_element<1, OutputTuple>::type T1;
        typedef typename tbb::flow::tuple_element<2, OutputTuple>::type T2;
    public:
        typedef typename wrap_tuple_elements<3, tag_matching_port, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename internal::function_body<T2, tag_value> *f2_p;
        typedef typename tbb::flow::tuple< f0_p, f1_p, f2_p > func_initializer_type;
    public:
        template<typename B0, typename B1, typename B2>
        unfolded_join_node(graph &g, B0 b0, B1 b1, B2 b2) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1),
                    new internal::function_body_leaf<T2, tag_value, B2>(b2)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };

    template<typename OutputTuple>
    class unfolded_join_node<4,tag_matching_port,OutputTuple,tag_matching> : public
            join_base<4,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename tbb::flow::tuple_element<0, OutputTuple>::type T0;
        typedef typename tbb::flow::tuple_element<1, OutputTuple>::type T1;
        typedef typename tbb::flow::tuple_element<2, OutputTuple>::type T2;
        typedef typename tbb::flow::tuple_element<3, OutputTuple>::type T3;
    public:
        typedef typename wrap_tuple_elements<4, tag_matching_port, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename internal::function_body<T2, tag_value> *f2_p;
        typedef typename internal::function_body<T3, tag_value> *f3_p;
        typedef typename tbb::flow::tuple< f0_p, f1_p, f2_p, f3_p > func_initializer_type;
    public:
        template<typename B0, typename B1, typename B2, typename B3>
        unfolded_join_node(graph &g, B0 b0, B1 b1, B2 b2, B3 b3) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1),
                    new internal::function_body_leaf<T2, tag_value, B2>(b2),
                    new internal::function_body_leaf<T3, tag_value, B3>(b3)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };

    template<typename OutputTuple>
    class unfolded_join_node<5,tag_matching_port,OutputTuple,tag_matching> : public
            join_base<5,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename tbb::flow::tuple_element<0, OutputTuple>::type T0;
        typedef typename tbb::flow::tuple_element<1, OutputTuple>::type T1;
        typedef typename tbb::flow::tuple_element<2, OutputTuple>::type T2;
        typedef typename tbb::flow::tuple_element<3, OutputTuple>::type T3;
        typedef typename tbb::flow::tuple_element<4, OutputTuple>::type T4;
    public:
        typedef typename wrap_tuple_elements<5, tag_matching_port, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename internal::function_body<T2, tag_value> *f2_p;
        typedef typename internal::function_body<T3, tag_value> *f3_p;
        typedef typename internal::function_body<T4, tag_value> *f4_p;
        typedef typename tbb::flow::tuple< f0_p, f1_p, f2_p, f3_p, f4_p > func_initializer_type;
    public:
        template<typename B0, typename B1, typename B2, typename B3, typename B4>
        unfolded_join_node(graph &g, B0 b0, B1 b1, B2 b2, B3 b3, B4 b4) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1),
                    new internal::function_body_leaf<T2, tag_value, B2>(b2),
                    new internal::function_body_leaf<T3, tag_value, B3>(b3),
                    new internal::function_body_leaf<T4, tag_value, B4>(b4)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };

#if __TBB_VARIADIC_MAX >= 6
    template<typename OutputTuple>
    class unfolded_join_node<6,tag_matching_port,OutputTuple,tag_matching> : public
            join_base<6,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename tbb::flow::tuple_element<0, OutputTuple>::type T0;
        typedef typename tbb::flow::tuple_element<1, OutputTuple>::type T1;
        typedef typename tbb::flow::tuple_element<2, OutputTuple>::type T2;
        typedef typename tbb::flow::tuple_element<3, OutputTuple>::type T3;
        typedef typename tbb::flow::tuple_element<4, OutputTuple>::type T4;
        typedef typename tbb::flow::tuple_element<5, OutputTuple>::type T5;
    public:
        typedef typename wrap_tuple_elements<6, tag_matching_port, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename internal::function_body<T2, tag_value> *f2_p;
        typedef typename internal::function_body<T3, tag_value> *f3_p;
        typedef typename internal::function_body<T4, tag_value> *f4_p;
        typedef typename internal::function_body<T5, tag_value> *f5_p;
        typedef typename tbb::flow::tuple< f0_p, f1_p, f2_p, f3_p, f4_p, f5_p > func_initializer_type;
    public:
        template<typename B0, typename B1, typename B2, typename B3, typename B4, typename B5>
        unfolded_join_node(graph &g, B0 b0, B1 b1, B2 b2, B3 b3, B4 b4, B5 b5) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1),
                    new internal::function_body_leaf<T2, tag_value, B2>(b2),
                    new internal::function_body_leaf<T3, tag_value, B3>(b3),
                    new internal::function_body_leaf<T4, tag_value, B4>(b4),
                    new internal::function_body_leaf<T5, tag_value, B5>(b5)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };
#endif

#if __TBB_VARIADIC_MAX >= 7
    template<typename OutputTuple>
    class unfolded_join_node<7,tag_matching_port,OutputTuple,tag_matching> : public
            join_base<7,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename tbb::flow::tuple_element<0, OutputTuple>::type T0;
        typedef typename tbb::flow::tuple_element<1, OutputTuple>::type T1;
        typedef typename tbb::flow::tuple_element<2, OutputTuple>::type T2;
        typedef typename tbb::flow::tuple_element<3, OutputTuple>::type T3;
        typedef typename tbb::flow::tuple_element<4, OutputTuple>::type T4;
        typedef typename tbb::flow::tuple_element<5, OutputTuple>::type T5;
        typedef typename tbb::flow::tuple_element<6, OutputTuple>::type T6;
    public:
        typedef typename wrap_tuple_elements<7, tag_matching_port, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename internal::function_body<T2, tag_value> *f2_p;
        typedef typename internal::function_body<T3, tag_value> *f3_p;
        typedef typename internal::function_body<T4, tag_value> *f4_p;
        typedef typename internal::function_body<T5, tag_value> *f5_p;
        typedef typename internal::function_body<T6, tag_value> *f6_p;
        typedef typename tbb::flow::tuple< f0_p, f1_p, f2_p, f3_p, f4_p, f5_p, f6_p > func_initializer_type;
    public:
        template<typename B0, typename B1, typename B2, typename B3, typename B4, typename B5, typename B6>
        unfolded_join_node(graph &g, B0 b0, B1 b1, B2 b2, B3 b3, B4 b4, B5 b5, B6 b6) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1),
                    new internal::function_body_leaf<T2, tag_value, B2>(b2),
                    new internal::function_body_leaf<T3, tag_value, B3>(b3),
                    new internal::function_body_leaf<T4, tag_value, B4>(b4),
                    new internal::function_body_leaf<T5, tag_value, B5>(b5),
                    new internal::function_body_leaf<T6, tag_value, B6>(b6)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };
#endif

#if __TBB_VARIADIC_MAX >= 8
    template<typename OutputTuple>
    class unfolded_join_node<8,tag_matching_port,OutputTuple,tag_matching> : public
            join_base<8,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename tbb::flow::tuple_element<0, OutputTuple>::type T0;
        typedef typename tbb::flow::tuple_element<1, OutputTuple>::type T1;
        typedef typename tbb::flow::tuple_element<2, OutputTuple>::type T2;
        typedef typename tbb::flow::tuple_element<3, OutputTuple>::type T3;
        typedef typename tbb::flow::tuple_element<4, OutputTuple>::type T4;
        typedef typename tbb::flow::tuple_element<5, OutputTuple>::type T5;
        typedef typename tbb::flow::tuple_element<6, OutputTuple>::type T6;
        typedef typename tbb::flow::tuple_element<7, OutputTuple>::type T7;
    public:
        typedef typename wrap_tuple_elements<8, tag_matching_port, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename internal::function_body<T2, tag_value> *f2_p;
        typedef typename internal::function_body<T3, tag_value> *f3_p;
        typedef typename internal::function_body<T4, tag_value> *f4_p;
        typedef typename internal::function_body<T5, tag_value> *f5_p;
        typedef typename internal::function_body<T6, tag_value> *f6_p;
        typedef typename internal::function_body<T7, tag_value> *f7_p;
        typedef typename tbb::flow::tuple< f0_p, f1_p, f2_p, f3_p, f4_p, f5_p, f6_p, f7_p > func_initializer_type;
    public:
        template<typename B0, typename B1, typename B2, typename B3, typename B4, typename B5, typename B6, typename B7>
        unfolded_join_node(graph &g, B0 b0, B1 b1, B2 b2, B3 b3, B4 b4, B5 b5, B6 b6, B7 b7) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1),
                    new internal::function_body_leaf<T2, tag_value, B2>(b2),
                    new internal::function_body_leaf<T3, tag_value, B3>(b3),
                    new internal::function_body_leaf<T4, tag_value, B4>(b4),
                    new internal::function_body_leaf<T5, tag_value, B5>(b5),
                    new internal::function_body_leaf<T6, tag_value, B6>(b6),
                    new internal::function_body_leaf<T7, tag_value, B7>(b7)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };
#endif

#if __TBB_VARIADIC_MAX >= 9
    template<typename OutputTuple>
    class unfolded_join_node<9,tag_matching_port,OutputTuple,tag_matching> : public
            join_base<9,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename tbb::flow::tuple_element<0, OutputTuple>::type T0;
        typedef typename tbb::flow::tuple_element<1, OutputTuple>::type T1;
        typedef typename tbb::flow::tuple_element<2, OutputTuple>::type T2;
        typedef typename tbb::flow::tuple_element<3, OutputTuple>::type T3;
        typedef typename tbb::flow::tuple_element<4, OutputTuple>::type T4;
        typedef typename tbb::flow::tuple_element<5, OutputTuple>::type T5;
        typedef typename tbb::flow::tuple_element<6, OutputTuple>::type T6;
        typedef typename tbb::flow::tuple_element<7, OutputTuple>::type T7;
        typedef typename tbb::flow::tuple_element<8, OutputTuple>::type T8;
    public:
        typedef typename wrap_tuple_elements<9, tag_matching_port, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename internal::function_body<T2, tag_value> *f2_p;
        typedef typename internal::function_body<T3, tag_value> *f3_p;
        typedef typename internal::function_body<T4, tag_value> *f4_p;
        typedef typename internal::function_body<T5, tag_value> *f5_p;
        typedef typename internal::function_body<T6, tag_value> *f6_p;
        typedef typename internal::function_body<T7, tag_value> *f7_p;
        typedef typename internal::function_body<T8, tag_value> *f8_p;
        typedef typename tbb::flow::tuple< f0_p, f1_p, f2_p, f3_p, f4_p, f5_p, f6_p, f7_p, f8_p > func_initializer_type;
    public:
        template<typename B0, typename B1, typename B2, typename B3, typename B4, typename B5, typename B6, typename B7, typename B8>
        unfolded_join_node(graph &g, B0 b0, B1 b1, B2 b2, B3 b3, B4 b4, B5 b5, B6 b6, B7 b7, B8 b8) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1),
                    new internal::function_body_leaf<T2, tag_value, B2>(b2),
                    new internal::function_body_leaf<T3, tag_value, B3>(b3),
                    new internal::function_body_leaf<T4, tag_value, B4>(b4),
                    new internal::function_body_leaf<T5, tag_value, B5>(b5),
                    new internal::function_body_leaf<T6, tag_value, B6>(b6),
                    new internal::function_body_leaf<T7, tag_value, B7>(b7),
                    new internal::function_body_leaf<T8, tag_value, B8>(b8)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };
#endif

#if __TBB_VARIADIC_MAX >= 10
    template<typename OutputTuple>
    class unfolded_join_node<10,tag_matching_port,OutputTuple,tag_matching> : public
            join_base<10,tag_matching_port,OutputTuple,tag_matching>::type {
        typedef typename tbb::flow::tuple_element<0, OutputTuple>::type T0;
        typedef typename tbb::flow::tuple_element<1, OutputTuple>::type T1;
        typedef typename tbb::flow::tuple_element<2, OutputTuple>::type T2;
        typedef typename tbb::flow::tuple_element<3, OutputTuple>::type T3;
        typedef typename tbb::flow::tuple_element<4, OutputTuple>::type T4;
        typedef typename tbb::flow::tuple_element<5, OutputTuple>::type T5;
        typedef typename tbb::flow::tuple_element<6, OutputTuple>::type T6;
        typedef typename tbb::flow::tuple_element<7, OutputTuple>::type T7;
        typedef typename tbb::flow::tuple_element<8, OutputTuple>::type T8;
        typedef typename tbb::flow::tuple_element<9, OutputTuple>::type T9;
    public:
        typedef typename wrap_tuple_elements<10, tag_matching_port, OutputTuple>::type input_ports_type;
        typedef OutputTuple output_type;
    private:
        typedef join_node_base<tag_matching, input_ports_type, output_type > base_type;
        typedef typename internal::function_body<T0, tag_value> *f0_p;
        typedef typename internal::function_body<T1, tag_value> *f1_p;
        typedef typename internal::function_body<T2, tag_value> *f2_p;
        typedef typename internal::function_body<T3, tag_value> *f3_p;
        typedef typename internal::function_body<T4, tag_value> *f4_p;
        typedef typename internal::function_body<T5, tag_value> *f5_p;
        typedef typename internal::function_body<T6, tag_value> *f6_p;
        typedef typename internal::function_body<T7, tag_value> *f7_p;
        typedef typename internal::function_body<T8, tag_value> *f8_p;
        typedef typename internal::function_body<T9, tag_value> *f9_p;
        typedef typename tbb::flow::tuple< f0_p, f1_p, f2_p, f3_p, f4_p, f5_p, f6_p, f7_p, f8_p, f9_p > func_initializer_type;
    public:
        template<typename B0, typename B1, typename B2, typename B3, typename B4, typename B5, typename B6, typename B7, typename B8, typename B9>
        unfolded_join_node(graph &g, B0 b0, B1 b1, B2 b2, B3 b3, B4 b4, B5 b5, B6 b6, B7 b7, B8 b8, B9 b9) : base_type(g,
                func_initializer_type(
                    new internal::function_body_leaf<T0, tag_value, B0>(b0),
                    new internal::function_body_leaf<T1, tag_value, B1>(b1),
                    new internal::function_body_leaf<T2, tag_value, B2>(b2),
                    new internal::function_body_leaf<T3, tag_value, B3>(b3),
                    new internal::function_body_leaf<T4, tag_value, B4>(b4),
                    new internal::function_body_leaf<T5, tag_value, B5>(b5),
                    new internal::function_body_leaf<T6, tag_value, B6>(b6),
                    new internal::function_body_leaf<T7, tag_value, B7>(b7),
                    new internal::function_body_leaf<T8, tag_value, B8>(b8),
                    new internal::function_body_leaf<T9, tag_value, B9>(b9)
                    ) ) {}
        unfolded_join_node(const unfolded_join_node &other) : base_type(other) {}
    };
#endif

    //! templated function to refer to input ports of the join node
    template<size_t N, typename JNT>
    typename tbb::flow::tuple_element<N, typename JNT::input_ports_type>::type &input_port(JNT &jn) {
        return tbb::flow::get<N>(jn.input_ports());
    }

}
#endif // __TBB__flow_graph_join_impl_H

