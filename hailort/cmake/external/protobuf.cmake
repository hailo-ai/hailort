cmake_minimum_required(VERSION 3.11.0)

include(FetchContent)

# TODO: support cross generators - https://gitlab.kitware.com/cmake/cmake/-/issues/20536
FetchContent_Declare(
    protobuf
    GIT_REPOSITORY  https://github.com/protocolbuffers/protobuf.git
    GIT_TAG         f0dc78d7e6e331b8c6bb2d5283e06aa26883ca7c # v21.12
    GIT_SHALLOW     TRUE
    SOURCE_DIR      ${HAILO_EXTERNAL_DIR}/protobuf-src
    SUBBUILD_DIR    ${HAILO_EXTERNAL_DIR}/protobuf-subbuild
)

FetchContent_GetProperties(protobuf)
if(NOT protobuf_POPULATED)
    FetchContent_Populate(protobuf)
    if (NOT HAILO_EXTERNALS_EXCLUDE_TARGETS)
        message(STATUS "Building protobuf::protoc...")
        include(${CMAKE_CURRENT_LIST_DIR}/../execute_cmake.cmake)
        set(TOOL_BUILD_TYPE "Release")
        set(PROTOBUF_INSTALL_DIR ${HAILO_EXTERNAL_DIR}/protobuf-install)

        execute_cmake(
            SOURCE_DIR ${HAILO_EXTERNAL_DIR}/protobuf-src
            BUILD_DIR ${HAILO_EXTERNAL_DIR}/protobuf-build
            CONFIGURE_ARGS
                -DCMAKE_BUILD_TYPE=${TOOL_BUILD_TYPE}
                -DCMAKE_INSTALL_PREFIX=${PROTOBUF_INSTALL_DIR}

                -Dprotobuf_BUILD_TESTS:BOOL=OFF
                -Dprotobuf_WITH_ZLIB:BOOL=OFF
                -Dprotobuf_MSVC_STATIC_RUNTIME:BOOL=OFF
            BUILD_ARGS
                # NOTE: We are installing instead of building protoc because "hailort\external\protobuf-build\cmake\protobuf-targets.cmake" (in Windows) is based on config type.
                # TODO: consider importing protobuf_generate_cpp instead? will it solve it?
                --config ${TOOL_BUILD_TYPE} --target install ${CMAKE_EXTRA_BUILD_ARGS}
            PARALLEL_BUILD
        )

        if(WIN32)
            set(PROTOBUF_CONFIG_DIR ${PROTOBUF_INSTALL_DIR}/cmake)
        else()
            set(PROTOBUF_CONFIG_DIR ${PROTOBUF_INSTALL_DIR}/lib/cmake/protobuf)
        endif()

        # Include host protobuf for protoc (https://stackoverflow.com/questions/53651181/cmake-find-protobuf-package-in-custom-directory)
        include(${PROTOBUF_CONFIG_DIR}/protobuf-config.cmake)
        include(${PROTOBUF_CONFIG_DIR}/protobuf-module.cmake)

        set(protobuf_BUILD_TESTS OFF CACHE BOOL "Build protobuf tests" FORCE)
        set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "Build libprotoc and protoc compiler" FORCE)
        set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "Protobuf MSVC static runtime" FORCE)
        set(protobuf_WITH_ZLIB OFF CACHE BOOL "Compile protobuf with zlib" FORCE)
        add_subdirectory(${protobuf_SOURCE_DIR} ${protobuf_BINARY_DIR} EXCLUDE_FROM_ALL)

        if(NOT MSVC)
            set_target_properties(libprotobuf PROPERTIES POSITION_INDEPENDENT_CODE ON)
            set_target_properties(libprotobuf-lite PROPERTIES POSITION_INDEPENDENT_CODE ON)
        endif()
    endif()
endif()





