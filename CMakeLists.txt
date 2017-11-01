# At the time of writing, using version 3.4.
cmake_minimum_required(VERSION 3.4)


# Enable ccache (if installed) :)
find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM)

    message("NOTE: Using CCache for caching.")

    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif ()

project(moodycamel::ConcurrentQueue)

message(STATUS "Project:    ${PROJECT_NAME}")
message(STATUS "Platform:   ${CMAKE_SYSTEM_NAME} (${CMAKE_SYSTEM})")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")


# Ensure C++11 support 
set(CMAKE_CXX_STANDARD 11)

if (CMAKE_COMPILER_IS_GNUCXX)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic-errors -Wpedantic")

else ()
message(FATAL_ERROR "Unsupported platform!")
endif ()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)


# Add moody-only headers
include_directories(.)

add_subdirectory(benchmarks)
add_subdirectory(tests)

# Install selected headers.
install(FILES concurrentqueue.h blockingconcurrentqueue.h DESTINATION include/moodycamel)
