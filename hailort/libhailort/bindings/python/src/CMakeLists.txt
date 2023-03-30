cmake_minimum_required(VERSION 3.0.0)

option(HAILO_BUILD_PYHAILORT_INTERNAL OFF)

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

set(PYHAILORT_DIR ${CMAKE_CURRENT_LIST_DIR})

pybind11_add_module(_pyhailort
    pyhailort.cpp
    device_api.cpp
    hef_api.cpp
    vstream_api.cpp
    quantization_api.cpp
    ${HAILORT_OPS_CPP_SOURCES}
    ${HAILORT_COMMON_CPP_SOURCES}
)

set_target_properties(_pyhailort PROPERTIES
    CXX_STANDARD              14
    CXX_STANDARD_REQUIRED     YES
)

target_include_directories(_pyhailort
    PRIVATE
    $<BUILD_INTERFACE:${HAILORT_INC_DIR}>
    $<BUILD_INTERFACE:${HAILORT_COMMON_DIR}>
    $<BUILD_INTERFACE:${HAILORT_SRC_DIR}>
    $<BUILD_INTERFACE:${COMMON_INC_DIR}>
)

target_link_libraries(_pyhailort PRIVATE libhailort spdlog::spdlog)
if(WIN32)
    target_link_libraries(_pyhailort PRIVATE Ws2_32 Iphlpapi Shlwapi)
endif()
if(HAILO_BUILD_SERVICE)
    target_link_libraries(_pyhailort PRIVATE grpc++_unsecure hailort_rpc_grpc_proto hef_proto)
endif()

target_compile_options(_pyhailort PRIVATE ${HAILORT_COMPILE_OPTIONS})
exclude_archive_libs_symbols(_pyhailort)

if (HAILO_BUILD_PYHAILORT_INTERNAL)
    add_subdirectory(internal)
    # copy files to a path the venv will look for
    add_custom_target(pyhailort_internal_venv ALL
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:_pyhailort_internal> ${PROJECT_SOURCE_DIR}/platform_internals/hailo_platform_internals/pyhailort/
    )
    add_dependencies(pyhailort_internal_venv _pyhailort_internal)
endif()

# TODO (HRT-8637): change this hard-coded path
set(HAILO_PYHAILORT_TARGET_DIR ${CMAKE_CURRENT_LIST_DIR}/../platform/hailo_platform/pyhailort/)

# copy files to a path the venv and whl will look for
message(STATUS "Copying _pyhailort artifacts into " ${HAILO_PYHAILORT_TARGET_DIR})
add_custom_target(pyhailort_venv ALL
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:_pyhailort> ${HAILO_PYHAILORT_TARGET_DIR}
)
add_dependencies(pyhailort_venv _pyhailort)

install(TARGETS _pyhailort
    LIBRARY DESTINATION ${HAILO_PYHAILORT_TARGET_DIR}
    CONFIGURATIONS Release
)