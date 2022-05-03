cmake_minimum_required(VERSION 3.0.0)
project(hailort_prebuild)

if(NOT HAILO_OFFLINE_COMPILATION)
    set(HAILO_PRE_BUILD_EXTERNAL_DIR ${CMAKE_CURRENT_LIST_DIR}/external)
    include(../libhailort/cmake/execute_cmake.cmake)
    message("Downloading dependencies to ${HAILO_EXTERNAL_DIR} ...")
    execute_cmake(
        SOURCE_DIR "${HAILO_PRE_BUILD_EXTERNAL_DIR}"
        BUILD_DIR "${HAILO_PRE_BUILD_EXTERNAL_DIR}/build"
        CONFIGURE_ARGS "-DHAILO_EXTERNAL_DIR=${HAILO_EXTERNAL_DIR}"
    )
    message("Finished downloading dependencies")
else()
    message("Offline compilation, skipped dependencies download")
endif()

add_subdirectory(tools)