cmake_minimum_required(VERSION 3.0.0)

message(STATUS "Building protobuf::protoc...")
if(NOT protobuf_BUILD_TESTS)
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "Build protobuf tests")
endif()
if(MSVC AND NOT protobuf_MSVC_STATIC_RUNTIME)
    set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "Protobuf MSVC static runtime")
endif()
if(NOT protobuf_WITH_ZLIB)
    set(protobuf_WITH_ZLIB OFF CACHE BOOL "Compile protobuf with zlib")
endif()
add_subdirectory(${HAILO_EXTERNAL_DIR}/protobuf/cmake build_protoc)

if(HAILO_BUILD_SERVICE)
    message(STATUS "Building grpc...")
    # The following is an awful hack needed in order to force grpc to use our libprotobuf+liborotoc targets
    # ('formal' options are to let grpc recompile it which causes a name conflict,
    # or let it use find_package and take the risk it will use a different installed lib)
    set(gRPC_PROTOBUF_PROVIDER "hack" CACHE STRING "Provider of protobuf library")
    # Avoiding "formal" gRPC_PROTOBUF_PROVIDER option, the following variables should be set independently
    set(_gRPC_PROTOBUF_LIBRARIES protobuf::libprotobuf)
    set(_gRPC_PROTOBUF_PROTOC_LIBRARIES protobuf::libprotoc)
    # Build grpc_cpp_plugin target only
    add_subdirectory(${HAILO_EXTERNAL_DIR}/grpc build_grpc EXCLUDE_FROM_ALL)
    add_custom_target(grpc_cpp_plugin_target ALL)
    add_dependencies(grpc_cpp_plugin_target grpc_cpp_plugin)
endif()