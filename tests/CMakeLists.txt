set(UNITTESTS_SRC
        unittests/unittests.cpp
        unittests/mallocmacro.cpp
        common/simplethread.cpp
        common/systemtime.cpp
)
add_executable(unittests ${UNITTESTS_SRC})
target_link_libraries(unittests PRIVATE concurrentqueue)
add_test(NAME unittests COMMAND unittests)

set(FUZZTESTS_SRC
        fuzztests/fuzztests.cpp
        common/simplethread.cpp
        common/systemtime.cpp
)
add_executable(fuzztests ${FUZZTESTS_SRC})
target_link_libraries(fuzztests PRIVATE concurrentqueue)
add_test(NAME fuzztests COMMAND fuzztests)

set(LIBTBB_SRC
        ${concurrentqueue_SOURCE_DIR}/benchmarks/tbb/cache_aligned_allocator.cpp
        ${concurrentqueue_SOURCE_DIR}/benchmarks/tbb/concurrent_queue.cpp
        ${concurrentqueue_SOURCE_DIR}/benchmarks/tbb/dynamic_link.cpp
        ${concurrentqueue_SOURCE_DIR}/benchmarks/tbb/tbb_misc.cpp
)
add_library(libtbb STATIC ${LIBTBB_SRC})
target_include_directories(libtbb PUBLIC ${concurrentqueue_SOURCE_DIR}/benchmarks/)
set_property(TARGET libtbb PROPERTY POSITION_INDEPENDENT_CODE ON)

set(BENCHMARKS_SRC
        ${concurrentqueue_SOURCE_DIR}/benchmarks/benchmarks.cpp
        ${concurrentqueue_SOURCE_DIR}/benchmarks/cpuid.cpp
        common/simplethread.cpp
        common/systemtime.cpp
)
add_executable(benchmarks ${BENCHMARKS_SRC})
target_link_libraries(benchmarks PRIVATE concurrentqueue libtbb)
add_test(NAME benchmarks COMMAND benchmarks)

