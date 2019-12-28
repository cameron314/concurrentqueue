#!/bin/bash
set -eux

#g++ ../jtest/jtest.cpp -o jtest_release.exe -Wall -D_DEBUG -O2
#g++ ../ntest/ntest.cpp -o ntest_release.exe -Wall -D_DEBUG -O2
#g++ ../../example/peterson/peterson.cpp -o peterson_release.exe -Wall -D_DEBUG -O2
g++ ../../example/proxy_collector/proxy_collector.cpp -o proxy_collector_release.exe -Wall -D_DEBUG -O2
g++ ../../example/ref_counting/ref_counting.cpp -o ref_counting_release.exe -Wall -D_DEBUG -O2
g++ ../../example/smr/smr.cpp -o smr_release.exe -Wall -D_DEBUG -O2
g++ ../../example/spsc_queue/spsc_queue.cpp -o spsc_queue_release.exe -Wall -D_DEBUG -O2
g++ ../../example/stack/stack.cpp -o stack_release.exe -Wall -D_DEBUG -O2
g++ ../../example/condvar/condvar.cpp -o condvar_release.exe -Wall -D_DEBUG -O2
g++ ../../example/mutex_business_logic/mutex_business_logic.cpp -o mutex_business_logic_release.exe -Wall -D_DEBUG -O2
g++ ../../example/ws_deque/ws_deque.cpp -o ws_deque_release.exe -Wall -D_DEBUG -O2
g++ ../../example/cli_ws_deque/cli_ws_deque.cpp -o cli_ws_deque_release.exe -Wall -D_DEBUG -O2
g++ ../../example/java_ws_deque/java_ws_deque.cpp -o java_ws_deque_release.exe -Wall -D_DEBUG -O2
g++ ../main.cpp -o test_release.exe -Wall -D_DEBUG -O2
