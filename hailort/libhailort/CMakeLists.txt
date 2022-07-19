cmake_minimum_required(VERSION 3.0.0)
# set(CMAKE_C_CLANG_TIDY "clang-tidy;-checks=*")

set(HAILORT_MAJOR_VERSION    4)
set(HAILORT_MINOR_VERSION    9)
set(HAILORT_REVISION_VERSION 0)

# Add the cmake folder so the modules there are found
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# Generate hef-proto files using host protobuf
protobuf_generate_cpp(PROTO_HEF_SRC PROTO_HEF_HEADR hef.proto)

add_library(hef_proto ${PROTO_HEF_SRC} ${PROTO_HEF_HEADR})
target_link_libraries(hef_proto libprotobuf)
set_target_properties(hef_proto PROPERTIES CXX_STANDARD 14 GENERATED TRUE POSITION_INDEPENDENT_CODE ON)
if(CMAKE_HOST_WIN32)
    # https://github.com/protocolbuffers/protobuf/tree/master/cmake#notes-on-compiler-warnings
    target_compile_options(hef_proto PRIVATE /wd4244)
endif()
get_filename_component(PROTO_HEADER_DIRECTORY ${PROTO_HEF_HEADR} DIRECTORY)
target_include_directories(hef_proto
    PUBLIC
    $<BUILD_INTERFACE: ${PROTO_HEADER_DIRECTORY}>
    $<BUILD_INTERFACE: ${Protobuf_INCLUDE_DIRS}>
)

# Add readerwriterqueue as a header-only library
add_library(readerwriterqueue INTERFACE)
target_include_directories(readerwriterqueue INTERFACE ${HAILO_EXTERNAL_DIR}/readerwriterqueue)

add_subdirectory(src)
if(HAILO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(HAILO_BUILD_UT)
    add_subdirectory(tests)
endif()
add_subdirectory(bindings)
add_subdirectory(doc)
