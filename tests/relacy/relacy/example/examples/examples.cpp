#include "stdafx.h"
#include "spsc_overwrite_queue.hpp"
#include "amp_condvar.hpp"


int main()
{
    rl::test_params p;
    p.iteration_count = 10000;
    //p.search_type = rl::sched_bound;
    //p.context_bound = 3;

    rl::execute<spsc_overwrite_queue_test, 2>(p);
    rl::simulate<amp_condvar_test>(p);
    rl::simulate<amp_condvar_test2>(p);
}

