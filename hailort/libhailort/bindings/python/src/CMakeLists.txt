# pyhailort — pybind11 extension for HailoRT Python bindings
# Build via: pip install .  (from platform/ dir, see pyproject.toml)

cmake_minimum_required(VERSION 3.15)

project(pyhailort)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.9")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# TODO HRT-19340: Remove setup.py fallback once meta-hailo kirkstone migrates to pyproject.toml
if(NOT SKBUILD)
    message(WARNING
        "Building without scikit-build-core. This is supported for legacy setup.py builds.\n"
        "Preferred method: pip install .  (from the platform/ directory)")
endif()

set(HAILORT_COMMON_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../../hailort/")

# Version: scikit-build-core sets SKBUILD_PROJECT_VERSION; setup.py passes -DHAILORT_VERSION=x.y.z
# TODO HRT-19340: Remove HAILORT_VERSION fallback once setup.py is removed
if(SKBUILD)
    string(REPLACE "." ";" _VER "${SKBUILD_PROJECT_VERSION}")
elseif(HAILORT_VERSION)
    message(STATUS "Using HAILORT_VERSION from command line: ${HAILORT_VERSION}")
    string(REPLACE "." ";" _VER "${HAILORT_VERSION}")
else()
    message(FATAL_ERROR "HAILORT_VERSION is not set. Pass -DHAILORT_VERSION=x.y.z or build via pip install .")
endif()
list(GET _VER 0 HAILORT_MAJOR_VERSION)
list(GET _VER 1 HAILORT_MINOR_VERSION)
list(GET _VER 2 HAILORT_REVISION_VERSION)

option(LIBHAILORT_PATH "Path to pre-built libhailort" "")
option(HAILORT_INCLUDE_DIR "Path to libhailort headers" "")

if(SKBUILD)
    # scikit-build-core builds: pybind11 is installed into the isolated build env
    # by pip from the [build-system] requires in pyproject.toml.
    find_package(pybind11 CONFIG REQUIRED)
else()
    # Legacy setup.py / standalone CMake builds: use vendored FetchContent script.
    # TODO HRT-20362: Remove this path once setup.py is fully retired.
    set(HAILO_EXTERNALS_CMAKE_SCRIPTS "${HAILORT_COMMON_DIR}/cmake/external/")
    set(HAILO_EXTERNAL_DIR "${HAILORT_COMMON_DIR}/external")
    include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/pybind11.cmake)
endif()
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
    session_api.cpp

    llm_api.cpp
    vlm_api.cpp
    speech2text_api.cpp
)

set_target_properties(_pyhailort PROPERTIES
    CXX_STANDARD              17
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
        HAILORT_MAJOR_VERSION=${HAILORT_MAJOR_VERSION}
        HAILORT_MINOR_VERSION=${HAILORT_MINOR_VERSION}
        HAILORT_REVISION_VERSION=${HAILORT_REVISION_VERSION}
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
    find_package(HailoRT ${HAILORT_MAJOR_VERSION}.${HAILORT_MINOR_VERSION}.${HAILORT_REVISION_VERSION} EXACT REQUIRED)
    target_link_libraries(_pyhailort PRIVATE HailoRT::libhailort)
endif()

if(WIN32)
    target_link_libraries(_pyhailort PRIVATE Ws2_32)
    target_compile_options(_pyhailort PRIVATE
        /DWIN32_LEAN_AND_MEAN
        /wd4201                     # Anonymous union/struct
        /wd4251                     # C++ ABI with STL
    )
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL Android)
    # TODO: HRT-14770 fix android build
    target_link_libraries(_pyhailort PRIVATE rt)
endif()

# pybind11 MODULE targets install via LIBRARY on all platforms (.so and .pyd)
install(TARGETS _pyhailort
    LIBRARY DESTINATION hailo_platform/pyhailort
)
