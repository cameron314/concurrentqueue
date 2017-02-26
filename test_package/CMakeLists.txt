project(concurrentqueue_test_package)
cmake_minimum_required(VERSION 2.8)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()
conan_define_targets()

add_executable(example main.cpp)
if(NOT MSVC)
    target_compile_options(example PRIVATE -std=c++11)
endif()

find_package(Threads)
target_link_libraries(example PRIVATE CONAN_PKG::concurrentqueue ${CMAKE_THREAD_LIBS_INIT})
