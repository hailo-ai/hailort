cmake_minimum_required(VERSION 3.11.0)

include(${CMAKE_CURRENT_LIST_DIR}/protobuf.cmake)
include(FetchContent)

FetchContent_Declare(
    grpc
    GIT_REPOSITORY  https://github.com/grpc/grpc
    GIT_TAG         6847e05dbb8088a918f06e2231a405942b5c002d # v1.54.0
    GIT_SHALLOW     TRUE
    SOURCE_DIR      ${HAILO_EXTERNAL_DIR}/grpc-src
    SUBBUILD_DIR    ${HAILO_EXTERNAL_DIR}/grpc-subbuild
)

FetchContent_GetProperties(grpc)
if(NOT grpc_POPULATED)
    FetchContent_Populate(grpc)
    if (NOT HAILO_EXTERNALS_EXCLUDE_TARGETS)
        message(STATUS "Building grpc...")
        include(${CMAKE_CURRENT_LIST_DIR}/../execute_cmake.cmake)
        set(TOOL_BUILD_TYPE "Release")
        execute_cmake(
            SOURCE_DIR ${HAILO_EXTERNAL_DIR}/grpc-src
            BUILD_DIR ${HAILO_EXTERNAL_DIR}/grpc-build
            CONFIGURE_ARGS
                -DCMAKE_BUILD_TYPE=${TOOL_BUILD_TYPE}
        
                -DgRPC_BUILD_TESTS:BOOL=OFF
                # TODO: check flag on Windows
                # -DgRPC_BUILD_MSVC_MP_COUNT:STRING=-1
                -DgRPC_PROTOBUF_PROVIDER:STRING=package
                -DgRPC_PROTOBUF_PACKAGE_TYPE:STRING=CONFIG
                -DProtobuf_DIR:PATH=${PROTOBUF_CONFIG_DIR}
            BUILD_ARGS
                --config ${TOOL_BUILD_TYPE} --target grpc_cpp_plugin ${CMAKE_EXTRA_BUILD_ARGS}
            PARALLEL_BUILD
        )
        
        if(HAILO_BUILD_SERVICE)
            # TODO: go over BUILD_TESTING vs gRPC_BUILD_TESTS. what about avoiding the hack the same way we did for grpc_cpp_plugin?
            set(BUILD_TESTING OFF) # disabe abseil tests
            set(gRPC_ZLIB_PROVIDER "module" CACHE STRING "Provider of zlib library")
            # The following is an awful hack needed in order to force grpc to use our libprotobuf+liborotoc targets
            # ('formal' options are to let grpc recompile it which causes a name conflict,
            # or let it use find_package and take the risk it will use a different installed lib)
            set(gRPC_PROTOBUF_PROVIDER "hack" CACHE STRING "Provider of protobuf library")
            add_subdirectory(${grpc_SOURCE_DIR} ${grpc_BINARY_DIR} EXCLUDE_FROM_ALL)
        endif()
    endif()
endif()