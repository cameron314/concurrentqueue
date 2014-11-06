/*  Relacy Race Detector
 *  Copyright (c) 2008-2013, Dmitry S. Vyukov
 *  All rights reserved.
 *  This software is provided AS-IS with no warranty, either express or implied.
 *  This software is distributed under a license and may not be copied,
 *  modified or distributed except as expressly authorized under the
 *  terms of the license contained in the file LICENSE in this distribution.
 */

#ifndef RL_TEST_PARAMS_HPP
#define RL_TEST_PARAMS_HPP
#ifdef _MSC_VER
#   pragma once
#endif

#include "base.hpp"
#include "test_result.hpp"


namespace rl
{

enum scheduler_type_e
{
    sched_random,
    sched_bound,
    sched_full,
    sched_count,

    random_scheduler_type = sched_random,
    fair_context_bound_scheduler_type = sched_bound,
    fair_full_search_scheduler_type = sched_full,
    scheduler_type_count
};

inline char const* format(scheduler_type_e t)
{
    switch (t)
    {
    case sched_random: return "random scheduler";
    case sched_bound: return "context bound scheduler";
    case sched_full: return "full search scheduler";
    default: break;
    }
    RL_VERIFY(false);
    throw std::logic_error("invalid scheduler type");
}


struct test_params
{
    // input params
    iteration_t                 iteration_count;
    std::ostream*               output_stream;
    std::ostream*               progress_stream;
    unsigned                    progress_output_period;
    bool                        collect_history;
    bool                        output_history;
    scheduler_type_e            search_type;
    unsigned                    context_bound;
    unsigned                    execution_depth_limit;
    string                      initial_state;

    // output params
    test_result_e               test_result;
    iteration_t                 stop_iteration;
    string                      test_name;
    string                      final_state;

    test_params()
    {
        iteration_count         = 1000;
        output_stream           = &std::cout;
        progress_stream         = &std::cout;
        progress_output_period  = 3;
        collect_history         = false;
        output_history          = false;
        search_type             = random_scheduler_type;
        context_bound           = 1;
        execution_depth_limit   = 2000;

        test_result             = test_result_success;
        stop_iteration          = 0;
    }
};


}

#endif
