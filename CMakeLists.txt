cmake_minimum_required(VERSION 3.2 FATAL_ERROR)

project(ConcurrentQueue VERSION 1.1.0 LANGUAGES C CXX)

add_library(ConcurrentQueue INTERFACE)
target_include_directories(ConcurrentQueue INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include/ConcurrentQueue>
)
target_compile_features(ConcurrentQueue INTERFACE
  cxx_constexpr
  cxx_defaulted_move_initializers
  cxx_auto_type
)

add_library(ConcurrentQueue::ConcurrentQueue ALIAS ConcurrentQueue)

# Install and exports
include(GNUInstallDirs)

install(TARGETS ConcurrentQueue EXPORT ConcurrentQueueConfig)
install(
  FILES
    concurrentqueue.h
    blockingconcurrentqueue.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ConcurrentQueue
)

install(EXPORT ConcurrentQueueConfig NAMESPACE ConcurrentQueue:: DESTINATION share/ConcurrentQueue/cmake)
export(TARGETS ConcurrentQueue NAMESPACE ConcurrentQueue:: FILE ConcurrentQueueConfig.cmake)

export(PACKAGE ConcurrentQueue)

