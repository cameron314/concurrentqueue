

set(TARGET_HPP
    minitest.h
    ../corealgos.h
    ../common/simplethread.h
    ../common/systemtime.h
    ../../concurrentqueue.h
    ../../blockingconcurrentqueue.h
    )

set(TARGET_CPP
    mallocmacro.cpp
    unittests.cpp
    ../common/simplethread.cpp
    ../common/systemtime.cpp
    )

add_executable(unittests ${TARGET_HPP} ${TARGET_CPP})

# Additional Include Directories
target_include_directories(unittests PRIVATE

    )

target_link_libraries(unittests
    pthread
    )