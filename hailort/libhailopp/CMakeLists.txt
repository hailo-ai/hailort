cmake_minimum_required(VERSION 3.11)

set(HAILO_PP_MAJOR_VERSION 1)
set(HAILO_PP_MINOR_VERSION 0)
set(HAILO_PP_REVISION_VERSION 0)

project(libhailopp VERSION ${HAILO_PP_MAJOR_VERSION}.${HAILO_PP_MINOR_VERSION}.${HAILO_PP_REVISION_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set external directory for dependencies (e.g., Eigen)
if(NOT DEFINED HAILO_EXTERNAL_DIR)
    set(HAILO_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../external")
endif()
set(HAILO_EXTERNALS_CMAKE_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/external")
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common_compiler_options.cmake)
include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/eigen.cmake)
include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/tl-expected.cmake)

# Build the library
add_subdirectory(src)

# Examples
if(HAILO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Unit tests
if(HAILO_BUILD_UT)
    add_subdirectory(tests)
endif()
