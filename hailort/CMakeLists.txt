cmake_minimum_required(VERSION 3.0.0)

# Set firmware version
add_definitions( -DFIRMWARE_VERSION_MAJOR=4 )
add_definitions( -DFIRMWARE_VERSION_MINOR=7 )
add_definitions( -DFIRMWARE_VERSION_REVISION=0 )

message(STATUS "Building pre_build")
include(${CMAKE_CURRENT_LIST_DIR}/libhailort/cmake/execute_cmake.cmake)
set(HAILO_EXTERNAL_DIR ${CMAKE_CURRENT_LIST_DIR}/external)
execute_cmake(
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/pre_build
    BUILD_DIR ${CMAKE_CURRENT_LIST_DIR}/pre_build/build
    CONFIGURE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_LIST_DIR}/pre_build/install
        -DHAILO_EXTERNAL_DIR=${HAILO_EXTERNAL_DIR}
        -DHAILO_OFFLINE_COMPILATION=${HAILO_OFFLINE_COMPILATION}
    BUILD_ARGS
        --config ${CMAKE_BUILD_TYPE} --target install ${CMAKE_EXTRA_BUILD_ARGS}
    PARALLEL_BUILD
)

# BENCHMARK_ENABLE_TESTING can be used by other 3rd party projects, therefore we define it
# before adding projects
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Enable testing of the benchmark library.")
add_subdirectory(external/benchmark EXCLUDE_FROM_ALL)

# Include host protobuf for protoc (https://stackoverflow.com/questions/53651181/cmake-find-protobuf-package-in-custom-directory)
if(CMAKE_HOST_UNIX)
    include(${CMAKE_CURRENT_LIST_DIR}/pre_build/install/lib/cmake/protobuf/protobuf-config.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/pre_build/install/lib/cmake/protobuf/protobuf-module.cmake)
else()
    include(${CMAKE_CURRENT_LIST_DIR}/pre_build/install/cmake/protobuf-config.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/pre_build/install/cmake/protobuf-module.cmake)
endif()

# Add target protobuf directory and exclude its targets from all
# Disable protobuf tests, protoc and MSVC static runtime unless they are already defined
# NOTE: we can also force - set(protobuf_BUILD_TESTS OFF CACHE BOOL "Build protobuf tests" FORCE)
if(NOT protobuf_BUILD_TESTS)
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "Build protobuf tests")
endif()
if(NOT protobuf_BUILD_PROTOC_BINARIES)
    set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "Build libprotoc and protoc compiler")
endif()
if(MSVC AND NOT protobuf_MSVC_STATIC_RUNTIME)
    set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "Protobuf MSVC static runtime")
endif()
if(NOT protobuf_WITH_ZLIB)
    set(protobuf_WITH_ZLIB OFF CACHE BOOL "Compile protobuf with zlib")
endif()
add_subdirectory(external/protobuf/cmake EXCLUDE_FROM_ALL)
if(NOT MSVC)
    set_target_properties(libprotobuf PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

set(HAILORT_INC_DIR ${PROJECT_SOURCE_DIR}/hailort/libhailort/include)
set(HAILORT_SRC_DIR ${PROJECT_SOURCE_DIR}/hailort/libhailort/src)
set(HAILORT_COMMON_DIR ${PROJECT_SOURCE_DIR}/hailort/)
set(COMMON_INC_DIR ${PROJECT_SOURCE_DIR}/common/include)
set(DRIVER_INC_DIR ${PROJECT_SOURCE_DIR}/hailort/drivers/common)

if(HAILO_BUILD_PYBIND)
    if(NOT PYTHON_EXECUTABLE AND PYBIND11_PYTHON_VERSION)
        # PYBIND11_PYTHON_VERSION is prioritized (not virtual environment) if PYTHON_EXECUTABLE is not set.
        # See https://pybind11.readthedocs.io/en/stable/changelog.html#v2-6-0-oct-21-2020
        find_package(PythonInterp ${PYBIND11_PYTHON_VERSION} REQUIRED)
        set(PYTHON_EXECUTABLE ${Python_EXECUTABLE})
    endif()
    add_subdirectory(external/pybind11 EXCLUDE_FROM_ALL)
endif()
add_subdirectory(external/Catch2 EXCLUDE_FROM_ALL)
add_subdirectory(external/CLI11 EXCLUDE_FROM_ALL)
add_subdirectory(external/json EXCLUDE_FROM_ALL)
add_subdirectory(external/DotWriter EXCLUDE_FROM_ALL)
add_subdirectory(external/spdlog EXCLUDE_FROM_ALL)
set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_SYSTEM_NAME STREQUAL QNX)
    add_library(pevents STATIC EXCLUDE_FROM_ALL external/pevents/src/pevents.cpp)
    target_include_directories(pevents PUBLIC external/pevents/src)
    target_compile_definitions(pevents PRIVATE -DWFMO)
endif()

# microprofile
if(HAILO_MICROPROFILE)
    add_library(microprofile STATIC EXCLUDE_FROM_ALL external/microprofile/microprofile.cpp)
    set_target_properties(microprofile PROPERTIES
        CXX_STANDARD              11
        CXX_STANDARD_REQUIRED     YES
        POSITION_INDEPENDENT_CODE ON
    )
    target_compile_definitions(microprofile
        PRIVATE
        -DMICROPROFILE_WEBSERVER=1
        -DMICROPROFILE_GPU_TIMERS=0
        -DMICROPROFILE_NAME_MAX_LEN=256
        PUBLIC
        -DMICROPROFILE_ENABLED=1
    )
    target_include_directories(microprofile PUBLIC external/microprofile)
else()
    add_library(microprofile INTERFACE)
    target_compile_definitions(microprofile INTERFACE -DMICROPROFILE_ENABLED=0)
    target_include_directories(microprofile INTERFACE external/microprofile)
endif()

add_subdirectory(common)
add_subdirectory(libhailort)
add_subdirectory(hailortcli)

if(HAILO_WIN_DRIVER)
    add_subdirectory(drivers/win)
    add_subdirectory(packaging)
endif()

# Compile PCIe Driver if QNX
if(CMAKE_SYSTEM_NAME STREQUAL QNX)
    add_subdirectory(drivers/qnx)
endif()