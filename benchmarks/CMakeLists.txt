

add_library(tbb STATIC
    tbb/cache_aligned_allocator.cpp
    tbb/concurrent_monitor.cpp
    tbb/concurrent_queue.cpp
    tbb/dynamic_link.cpp
    tbb/tbb_misc.cpp)

# Additional Include Directories
target_include_directories(tbb PRIVATE
    .
    )

target_compile_definitions(tbb PRIVATE
    -D__TBB_BUILD=1
    )

set(TARGET_HPP
    boostqueue.h
    lockbasedqueue.h
    simplelockfree.h
    stdqueue.h
    tbbqueue.h
    wrappers.h
    ../concurrentqueue.h
    ../blockingconcurrentqueue.h
    )

set(TARGET_CPP
    cpuid.cpp
    benchmarks.cpp
    ../tests/common/simplethread.cpp
    ../tests/common/systemtime.cpp
    )

add_executable(benchmarks ${TARGET_HPP} ${TARGET_CPP})

# Additional Include Directories
target_include_directories(benchmarks PRIVATE
    .
    ../tests
    ../tests/common
    )

target_link_libraries(benchmarks

    tbb
    pthread
    )