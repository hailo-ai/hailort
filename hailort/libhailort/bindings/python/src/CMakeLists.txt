cmake_minimum_required(VERSION 3.0.0)
project(pyhailort)

get_filename_component(HAILORT_PROJECT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../../" ABSOLUTE)
get_filename_component(HAILORT_COMMON_DIR "${HAILORT_PROJECT_SOURCE_DIR}/hailort/" ABSOLUTE)
get_filename_component(PYHAILORT_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)

set(HAILO_EXTERNAL_DIR ${HAILORT_COMMON_DIR}/external)
set(HAILO_EXTERNALS_CMAKE_SCRIPTS ${HAILORT_COMMON_DIR}/cmake/external/)

option(LIBHAILORT_PATH "Path to libhailort to link against" "")
option(HAILORT_INCLUDE_DIR "Path to include dir of libhailort" "")
option(HAILO_BUILD_PYHAILORT_INTERNAL OFF)
option(HAILO_BUILD_RAW_CONNECTION OFF)

include(ExternalProject)
include(GNUInstallDirs)
include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/pybind11.cmake)
include_directories(${HAILORT_COMMON_DIR})

FUNCTION(exclude_archive_libs_symbols target) # should be same as in common_compiler_options.cmake
    if(WIN32)
        # TODO: check if there are required actions for Windows
    elseif(UNIX)
        get_property(TEMP_LINK_FLAGS TARGET ${target} PROPERTY LINK_FLAGS)
        set(TEMP_LINK_FLAGS "${TEMP_LINK_FLAGS} -Wl,--exclude-libs=ALL")
        set_property(TARGET ${target} PROPERTY LINK_FLAGS ${TEMP_LINK_FLAGS})
    endif()
ENDFUNCTION()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    if(NOT DEFINED PYBIND11_PYTHON_VERSION)
        message(FATAL_ERROR "PYBIND11_PYTHON_VERSION is not defined. To build _pyhailort, pass python version")
    endif()
    string(REPLACE "." "" dpython ${PYBIND11_PYTHON_VERSION}) # E.g "3.5" -> "35"
    if(${dpython} LESS "38")
        set(m_flag "m")
    else()
        set(m_flag "")
    endif()
    set(PYTHON_MODULE_EXTENSION ".cpython-${dpython}${m_flag}-${CMAKE_SYSTEM_PROCESSOR}-linux-gnu.so")
endif()

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
    # VISIBILITY_INLINES_HIDDEN YES
)

# allow user to inject a specific libhailort (and headers) to link against.
# use case: cross compilation
if(LIBHAILORT_PATH AND HAILORT_INCLUDE_DIR)
    message(STATUS "LIBHAILORT_PATH is set. Will link against given libhailort: ${LIBHAILORT_PATH}")
    message(STATUS "HAILORT_INCLUDE_DIR is set. Will include given include dir: ${HAILORT_INCLUDE_DIR}")

    # the library to link against
    target_link_libraries(_pyhailort PRIVATE ${LIBHAILORT_PATH})

    # the include dir
    include_directories(${HAILORT_INCLUDE_DIR})

    # since we are linking against an injected libhailort, we need to define the version
    target_compile_definitions(
        _pyhailort
        PUBLIC
        HAILORT_MAJOR_VERSION=4
        HAILORT_MINOR_VERSION=18
        HAILORT_REVISION_VERSION=1
    )
elseif(LIBHAILORT_PATH OR HAILORT_INCLUDE_DIR)
    message(FATAL_ERROR "Both LIBHAILORT_PATH and HAILORT_INCLUDE_DIR must be defined or none of them")
else()
    find_package(HailoRT 4.18.1 EXACT REQUIRED)
    target_link_libraries(_pyhailort PRIVATE HailoRT::libhailort)
endif()

if(WIN32)
    target_link_libraries(_pyhailort PRIVATE Ws2_32)
    target_compile_options(_pyhailort PRIVATE
        /DWIN32_LEAN_AND_MEAN
        /DNOMINMAX                  # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4201 /wd4251
    )
endif()

target_compile_options(_pyhailort PRIVATE ${HAILORT_COMPILE_OPTIONS})
exclude_archive_libs_symbols(_pyhailort)

# TODO (HRT-8637): change this hard-coded path
set(HAILO_PYHAILORT_TARGET_DIR ${CMAKE_CURRENT_LIST_DIR}/../platform/hailo_platform/pyhailort/)

add_custom_target(pyhailort_venv ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:_pyhailort> ${HAILO_PYHAILORT_TARGET_DIR}
)
add_dependencies(pyhailort_venv _pyhailort)

install(TARGETS _pyhailort
    LIBRARY DESTINATION ${HAILO_PYHAILORT_TARGET_DIR}
    CONFIGURATIONS Release
)

if (HAILO_BUILD_PYHAILORT_INTERNAL OR HAILO_BUILD_RAW_CONNECTION)
    add_subdirectory(internal)
endif()
