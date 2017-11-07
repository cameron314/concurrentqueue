

set(TARGET_HPP
    ../corealgos.h
    ../common/simplethread.h
    ../common/systemtime.h
    ../../concurrentqueue.h
    ../../blockingconcurrentqueue.h
    )

set(TARGET_CPP
    fuzztests.cpp
    ../common/simplethread.cpp
    ../common/systemtime.cpp
    )

add_executable(fuzztests ${TARGET_HPP} ${TARGET_CPP})

# Additional Include Directories
target_include_directories(fuzztests PUBLIC

)

target_link_libraries(fuzztests
    pthread
)