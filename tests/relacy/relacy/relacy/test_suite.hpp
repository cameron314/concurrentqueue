/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_TEST_SUITE_HPP
#define RL_TEST_SUITE_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "test_result.hpp"


namespace rl
{


template<
    typename derived_t,
    thread_id_t static_thread_count_param,
    test_result_e result = test_result_success>
struct test_suite : nocopy<>
{
    static thread_id_t const dynamic_thread_count = 0;

    struct params
    {
        static thread_id_t const static_thread_count = static_thread_count_param;
        static thread_id_t const dynamic_thread_count = derived_t::dynamic_thread_count;
        static thread_id_t const thread_count = static_thread_count + dynamic_thread_count;
        static test_result_e const expected_result = result;
    };

    void invariant() {}
    void before() {}
    void after() {}
};


}

#endif
