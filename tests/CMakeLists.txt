set(UNITTESTS_SRC
        unittests/unittests.cpp
        unittests/mallocmacro.cpp
        common/simplethread.cpp
        common/systemtime.cpp
)

add_executable(unittests ${UNITTESTS_SRC})
target_link_libraries(unittests PRIVATE concurrentqueue)

add_test(NAME unittests COMMAND unittests)