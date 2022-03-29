cmake_minimum_required(VERSION 3.0.0)

# Set firmware version
add_definitions( -DFIRMWARE_VERSION_MAJOR=4 )
add_definitions( -DFIRMWARE_VERSION_MINOR=6 )
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
    add_subdirectory(external/pybind11 EXCLUDE_FROM_ALL)
endif()
add_subdirectory(external/Catch2 EXCLUDE_FROM_ALL)
add_subdirectory(external/CLI11 EXCLUDE_FROM_ALL)
add_subdirectory(external/json EXCLUDE_FROM_ALL)
add_subdirectory(external/DotWriter EXCLUDE_FROM_ALL)
add_subdirectory(external/spdlog EXCLUDE_FROM_ALL)
set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_subdirectory(common)
add_subdirectory(libhailort)
add_subdirectory(hailortcli)

# copy files to venv
if(HAILO_BUILD_PYBIND AND HAILO_BUILD_PYHAILORT_VENV)
    set(VENV_DRIVERS_DIR ${CMAKE_SOURCE_DIR}/hailort/libhailort/bindings/python/platform/hailo_platform/drivers/hailort/)
    set(PYHAILORT_FILES_TO_COPY
        $<TARGET_FILE:_pyhailort>
    )
    set(VENV_PYHAILORT_INTERNAL_DIR ${CMAKE_SOURCE_DIR}/platform_internals/hailo_platform_internals/pyhailort/)
    set(PYHAILORT_INTERNAL_FILES_TO_COPY
        $<TARGET_FILE:_pyhailort_internal>
    )
    add_custom_target(
        pyhailort_venv ALL
        COMMAND ${CMAKE_COMMAND} -E copy ${PYHAILORT_FILES_TO_COPY} ${VENV_DRIVERS_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy ${PYHAILORT_INTERNAL_FILES_TO_COPY} ${VENV_PYHAILORT_INTERNAL_DIR}
    )
    add_dependencies(pyhailort_venv libhailort _pyhailort)
endif()

if(HAILO_WIN_DRIVER)
    add_subdirectory(drivers/win)
    add_subdirectory(packaging)
endif()

# Compile PCIe Driver if QNX
if(CMAKE_SYSTEM_NAME STREQUAL QNX)
    add_subdirectory(drivers/qnx)
endif()