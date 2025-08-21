cmake_minimum_required(VERSION 3.5.0)

include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/protobuf.cmake)

protobuf_generate_cpp(PROTO_GENAI_SRC PROTO_GENAI_HEADER genai_scheme.proto)
get_filename_component(PROTO_HEADER_DIRECTORY ${PROTO_GENAI_HEADER} DIRECTORY)

add_library(genai_scheme_proto STATIC EXCLUDE_FROM_ALL ${PROTO_GENAI_SRC} ${PROTO_GENAI_HEADER})
target_link_libraries(genai_scheme_proto libprotobuf-lite)
set_target_properties(genai_scheme_proto PROPERTIES CXX_STANDARD 17 GENERATED TRUE POSITION_INDEPENDENT_CODE ON)
if(CMAKE_HOST_WIN32)
    # https://github.com/protocolbuffers/protobuf/tree/master/cmake#notes-on-compiler-warnings
    target_compile_options(genai_scheme_proto PRIVATE /wd4244)
endif()
get_filename_component(PROTO_HEADER_DIRECTORY ${PROTO_GENAI_HEADER} DIRECTORY)
target_include_directories(genai_scheme_proto
    PUBLIC
    $<BUILD_INTERFACE: ${PROTO_HEADER_DIRECTORY}>
    $<BUILD_INTERFACE: ${Protobuf_INCLUDE_DIRS}>
)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/serializer.cpp
)

set(GENAI_SCHEME_CPP_SOURCES ${SRC_FILES} PARENT_SCOPE)