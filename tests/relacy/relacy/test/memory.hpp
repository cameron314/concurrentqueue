#pragma once

#include "../relacy/relacy_std.hpp"


struct test_memory_allocation : rl::test_suite<test_memory_allocation, 2>
{
    void thread(unsigned /*index*/)
    {
        VAR_T(int)* p1 = new VAR_T(int) (5), i1 = 5, * p11 = new VAR_T(int) (6);
        VAR(p1[0]) = 1;
        delete p1, delete p11;

        VAR_T(int)* p2 = new VAR_T(int) [10], i2 = 6, *p22 = new VAR_T(int) [20];
        VAR(p2[0]) = 1;
        delete [] p2, delete [] p22;

        void* p3 = malloc(10), *i3 = 0, *p33 = malloc(20);
        free(p3), free(p33);

        void* p4 = malloc(sizeof(int));
        int* i4 = new (p4) int (11);
        free(p4);

        //RL_ASSERT(false);
        (void)i1, (void)i2, (void)i3; (void)i4;
    }
};

