/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_TEST_RESULT_HPP
#define RL_TEST_RESULT_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"


namespace rl
{


enum test_result_e
{
    test_result_success,
    test_result_until_condition_hit,
    test_result_inconsistent_test_suite,
    test_result_user_assert_failed,
    test_result_user_invariant_failed,
    test_result_data_race,
    test_result_access_to_freed_memory,
    test_result_double_free,
    test_result_memory_leak,
    test_result_resource_leak,
    test_result_unitialized_access,
    test_result_deadlock,
    test_result_livelock,

    // mutex
    test_result_recursion_on_nonrecursive_mutex,
    test_result_unlocking_mutex_wo_ownership,
    test_result_destroying_owned_mutex,
    test_result_double_initialization_of_mutex,
    test_result_usage_of_non_initialized_mutex,
    test_result_mutex_write_to_read_upgrade,
    test_result_mutex_read_to_write_upgrade,

    //condvar
    test_result_double_initialization_of_condvar,
    test_result_usage_of_non_initialized_condvar,

    //semaphore
    test_result_double_initialization_of_semaphore,
    test_result_usage_of_non_initialized_semaphore,

    //event
    test_result_double_initialization_of_event,
    test_result_usage_of_non_initialized_event,

    //dynamic thread
    test_result_thread_signal,
};


inline char const* test_result_str(test_result_e r)
{
    switch (r)
    {
    case test_result_success: return "SUCCESS";
    case test_result_until_condition_hit: return "UNTIL CONDITION HIT";
    case test_result_inconsistent_test_suite: return "INCONSISTENT TEST SUITE";
    case test_result_user_assert_failed: return "USER ASSERT FAILED";
    case test_result_user_invariant_failed: return "USER INVARIANT FAILED";
    case test_result_data_race: return "DATA RACE";
    case test_result_access_to_freed_memory: return "ACCESS TO FREED MEMORY";
    case test_result_double_free: return "DOUBLE FREE";
    case test_result_memory_leak: return "MEMORY LEAK";
    case test_result_resource_leak: return "RESOURCE LEAK";
    case test_result_unitialized_access: return "ACCESS TO UNINITIALIZED VARIABLE";
    case test_result_deadlock: return "DEADLOCK";
    case test_result_livelock: return "LIVELOCK";

    // mutex
    case test_result_recursion_on_nonrecursive_mutex: return "RECURSION ON NON-RECURSIVE MUTEX";
    case test_result_unlocking_mutex_wo_ownership: return "UNLOCKING MUTEX W/O OWNERSHIP";
    case test_result_destroying_owned_mutex: return "DESTROYING OWNED MUTEX";
    case test_result_double_initialization_of_mutex: return "DOUBLE INITIALIZATION OF MUTEX";
    case test_result_usage_of_non_initialized_mutex: return "USAGE OF NON INITIALIZED MUTEX";
    case test_result_mutex_write_to_read_upgrade: return "ATTEMPT TO UPGRADE EXCLUSIVE MUTEX OWNERSHIP TO SHARED";
    case test_result_mutex_read_to_write_upgrade: return "ATTEMPT TO UPGRADE SHARED MUTEX OWNERSHIP TO EXCLUSIVE";

    // condvar
    case test_result_double_initialization_of_condvar: return "DOUBLE INITIALIZATION OF CONDITION VARIABLE";
    case test_result_usage_of_non_initialized_condvar: return "USAGE OF NON INITIALIZED CONDITION VARIABLE";

    // semaphore
    case test_result_double_initialization_of_semaphore: return "DOUBLE INITIALIZATION OF SEMAPHORE";
    case test_result_usage_of_non_initialized_semaphore: return "USAGE OF NON INITIALIZED SEMAPHORE";

    // event
    case test_result_double_initialization_of_event: return "DOUBLE INITIALIZATION OF EVENT";
    case test_result_usage_of_non_initialized_event: return "USAGE OF NON INITIALIZED EVENT";

    default: RL_VERIFY(false); return "UNKNOWN ERROR";
    }
}


}

#endif
