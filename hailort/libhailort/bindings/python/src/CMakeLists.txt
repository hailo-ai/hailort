cmake_minimum_required(VERSION 3.5.0)
project(pyhailort)

find_program(CCACHE_PROGRAM ccache)
find_program(CLACHE_PROGRAM clcache)

if(CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
elseif(CLCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CLCACHE_PROGRAM}")
endif()

if(WIN32)
    find_program(SCCACHE sccache)
    if(SCCACHE)
        set(CMAKE_C_COMPILER_LAUNCHER ${SCCACHE})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${SCCACHE})
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
        cmake_policy(SET CMP0141 NEW)
    endif()
endif()

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.9")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

set(HAILORT_COMMON_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../../hailort/")
set(HAILO_EXTERNALS_CMAKE_SCRIPTS ${HAILORT_COMMON_DIR}/cmake/external/)
set(HAILO_EXTERNAL_DIR ${HAILORT_COMMON_DIR}/external) # used in pybind11.cmake

if(UNIX)
    # PYTHON_MODULE_EXTENSION does not take the CMAKE_SYSTEM_PROCESSOR into account so it needs to be added manually
    string(REGEX REPLACE "\\." "" PYBIND11_PYTHON_VERSION_NO_DOT ${PYBIND11_PYTHON_VERSION})
    set(PYTHON_MODULE_EXTENSION ".cpython-${PYBIND11_PYTHON_VERSION_NO_DOT}-${CMAKE_SYSTEM_PROCESSOR}-linux-gnu.so")
endif()

get_filename_component(
    HAILO_PYHAILORT_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../platform/hailo_platform/pyhailort/"
    ABSOLUTE
)

option(LIBHAILORT_PATH "Path to libhailort to link against" "")
option(HAILORT_INCLUDE_DIR "Path to include dir of libhailort" "")

include(GNUInstallDirs)
include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/pybind11.cmake)
include_directories(${HAILORT_COMMON_DIR})

pybind11_add_module(_pyhailort
    pyhailort.cpp
    device_api.cpp
    vdevice_api.cpp
    infer_model_api.cpp
    network_group_api.cpp
    hef_api.cpp
    vstream_api.cpp
    quantization_api.cpp
)

set_target_properties(_pyhailort PROPERTIES
    CXX_STANDARD              14
    CXX_STANDARD_REQUIRED     YES
    CXX_EXTENSIONS            NO
    C_VISIBILITY_PRESET       hidden
    CXX_VISIBILITY_PRESET     hidden
)

if(LIBHAILORT_PATH AND HAILORT_INCLUDE_DIR)
    message(STATUS "Will link against given libhailort: ${LIBHAILORT_PATH}")
    message(STATUS "Will include given include dir:     ${HAILORT_INCLUDE_DIR}")

    target_link_libraries(_pyhailort PRIVATE ${LIBHAILORT_PATH})
    include_directories(${HAILORT_INCLUDE_DIR})
    target_compile_definitions(
        _pyhailort
        PUBLIC
        HAILORT_MAJOR_VERSION=4
        HAILORT_MINOR_VERSION=23
        HAILORT_REVISION_VERSION=0
    )
    set_target_properties(
        _pyhailort
        PROPERTIES
        INSTALL_RPATH "${LIBHAILORT_PATH}"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
elseif(LIBHAILORT_PATH OR HAILORT_INCLUDE_DIR)
    message(FATAL_ERROR "Both LIBHAILORT_PATH and HAILORT_INCLUDE_DIR must be defined or none of them. LIBHAILORT_PATH: '${LIBHAILORT_PATH}', HAILORT_INCLUDE_DIR: '${HAILORT_INCLUDE_DIR}'")
else()
    find_package(HailoRT 4.23.0 EXACT REQUIRED)
    target_link_libraries(_pyhailort PRIVATE HailoRT::libhailort)
endif()

if(WIN32)
    target_link_libraries(_pyhailort PRIVATE Ws2_32)
    target_compile_options(_pyhailort PRIVATE
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX                  # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201                     # Anonymous union/struct
        /wd4251                     # C++ ABI with STL
    )
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL Android)
    # TODO: HRT-14770 fix android build
    target_link_libraries(_pyhailort PRIVATE rt)
endif()

install(
    TARGETS _pyhailort
    LIBRARY DESTINATION ${HAILO_PYHAILORT_DIR}
)
