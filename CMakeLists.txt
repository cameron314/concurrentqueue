cmake_minimum_required(VERSION 3.14)
project(concurrentqueue CXX)

set(CMAKE_C_STANDARD 11)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

enable_testing()

add_library(concurrentqueue INTERFACE)

# -Wall -pedantic-errors -Wpedantic -Wconversion -fno-elide-constructors
target_compile_options(concurrentqueue INTERFACE -Wall -pedantic-errors -Wpedantic -Wconversion -fno-elide-constructors)

target_include_directories(concurrentqueue INTERFACE .)
target_link_libraries(concurrentqueue INTERFACE Threads::Threads rt)

add_subdirectory(tests)