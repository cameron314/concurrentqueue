cmake_minimum_required(VERSION 3.9)
project(concurrentqueue VERSION 1.0.0)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME} 
        INTERFACE 
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/>
)

install(TARGETS ${PROJECT_NAME} 
    EXPORT ${PROJECT_NAME}Targets
)

write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
    VERSION 
        ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(${PROJECT_NAME}Config.cmake.in
                ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
        INSTALL_DESTINATION 
                ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}/
)

install(EXPORT 
                ${PROJECT_NAME}Targets
        FILE
                ${PROJECT_NAME}Targets.cmake
        NAMESPACE 
                "${PROJECT_NAME}::"
        DESTINATION
                ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
        COMPONENT
                Devel
)

install(
        FILES
                ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
                ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
        DESTINATION
                ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
        COMPONENT
                Devel
)

install(
        FILES 
                blockingconcurrentqueue.h 
                concurrentqueue.h 
                lightweightsemaphore.h 
                LICENSE.md
        DESTINATION 
                ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/moodycamel
)

set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VENDOR "Cameron Desrochers <cameron@moodycamel.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "An industrial-strength lock-free queue for C++.")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER ${CPACK_PACKAGE_VENDOR})
set(CPACK_GENERATOR "RPM;DEB")

include(CPack)