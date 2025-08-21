cmake_minimum_required(VERSION 3.5.0)

protobuf_generate_cpp(PROTO_RPC_SRC PROTO_RPC_HEADER rpc.proto)
get_filename_component(PROTO_HEADER_DIRECTORY ${PROTO_RPC_HEADER} DIRECTORY)

add_library(rpc_proto STATIC EXCLUDE_FROM_ALL ${PROTO_RPC_SRC} ${PROTO_RPC_HEADER})
target_link_libraries(rpc_proto libprotobuf-lite)
set_target_properties(rpc_proto PROPERTIES CXX_STANDARD 17 GENERATED TRUE POSITION_INDEPENDENT_CODE ON)
if(CMAKE_HOST_WIN32)
    # https://github.com/protocolbuffers/protobuf/tree/master/cmake#notes-on-compiler-warnings
    target_compile_options(rpc_proto PRIVATE /wd4244)
endif()
get_filename_component(PROTO_HEADER_DIRECTORY ${PROTO_RPC_HEADER} DIRECTORY)
target_include_directories(rpc_proto
    PUBLIC
    $<BUILD_INTERFACE: ${PROTO_HEADER_DIRECTORY}>
    $<BUILD_INTERFACE: ${Protobuf_INCLUDE_DIRS}>
)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/serializer.cpp
)

set(HRPC_PROTOCOL_CPP_SOURCES ${SRC_FILES} PARENT_SCOPE)