cmake_minimum_required(VERSION 3.0.0)

include(ExternalProject)

FUNCTION(exclude_archive_libs_symbols target) # should be same as in common_compiler_options.cmake
    if(WIN32)
        # TODO: check if there are required actions for Windows
    elseif(UNIX)
        get_property(TEMP_LINK_FLAGS TARGET ${target} PROPERTY LINK_FLAGS)
        set(TEMP_LINK_FLAGS "${TEMP_LINK_FLAGS} -Wl,--exclude-libs=ALL")
        set_property(TARGET ${target} PROPERTY LINK_FLAGS ${TEMP_LINK_FLAGS})
    else()
        message(FATAL_ERROR "Unexpeced host, stopping build")
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

option(HAILO_BUILD_PYHAILORT_INTERNAL OFF)

set(PYHAILORT_DIR ${CMAKE_CURRENT_LIST_DIR})

pybind11_add_module(_pyhailort
    pyhailort.cpp
    device_api.cpp
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

find_package(HailoRT 4.14.0 EXACT REQUIRED)

target_link_libraries(_pyhailort PRIVATE HailoRT::libhailort)
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