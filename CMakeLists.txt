cmake_minimum_required(VERSION 3.13)
project(concurrentqueue VERSION 0.1 LANGUAGES CXX)

set(CMAKE_C_STANDARD 11)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

enable_testing()

add_library(concurrentqueue INTERFACE)

# -Wall -pedantic-errors -Wpedantic -Wconversion -fno-elide-constructors
target_compile_options(concurrentqueue INTERFACE -Wall -pedantic-errors -Wpedantic -Wconversion -fno-elide-constructors)

target_include_directories(concurrentqueue INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(concurrentqueue INTERFACE Threads::Threads rt)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/concurrentqueueConfigVersion.cmake"
        VERSION 0.1
        COMPATIBILITY AnyNewerVersion
)
install(TARGETS concurrentqueue
        EXPORT concurrentqueueTargets
        LIBRARY DESTINATION lib COMPONENT Runtime
        ARCHIVE DESTINATION lib COMPONENT Development
        RUNTIME DESTINATION bin COMPONENT Runtime
        PUBLIC_HEADER DESTINATION include COMPONENT Development
        BUNDLE DESTINATION bin COMPONENT Runtime
)
include(CMakePackageConfigHelpers)
configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/cmake/concurrentqueueConfig.cmake.in"
        "${PROJECT_BINARY_DIR}/concurrentqueueConfig.cmake"
        INSTALL_DESTINATION lib/cmake/concurrentqueue
)
install(EXPORT concurrentqueueTargets DESTINATION lib/cmake/concurrentqueue)
install(FILES "${PROJECT_BINARY_DIR}/concurrentqueueConfigVersion.cmake"
        "${PROJECT_BINARY_DIR}/concurrentqueueConfig.cmake"
        DESTINATION lib/cmake/concurrentqueue)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ DESTINATION include)

add_subdirectory(tests)