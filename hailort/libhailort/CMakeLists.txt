cmake_minimum_required(VERSION 3.5.0)
# set(CMAKE_C_CLANG_TIDY "clang-tidy;-checks=*")

set(HAILORT_MAJOR_VERSION    5)
set(HAILORT_MINOR_VERSION    1)
set(HAILORT_REVISION_VERSION 1)

# Add the cmake folder so the modules there are found
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# Generate hef-proto files using host protobuf
protobuf_generate_cpp(PROTO_HEF_SRC PROTO_HEF_HEADER hef.proto)
protobuf_generate_python(PROTO_HEF_PY hef.proto) # TODO (HRT-12504): Copy hef_pb2.py to tools directory
protobuf_generate_python(PROTO_HEF_PY tracer_profiler.proto)

add_library(hef_proto ${PROTO_HEF_SRC} ${PROTO_HEF_HEADER} ${PROTO_HEF_PY})
target_link_libraries(hef_proto libprotobuf-lite)
set_target_properties(hef_proto PROPERTIES CXX_STANDARD 17 GENERATED TRUE POSITION_INDEPENDENT_CODE ON)
if(CMAKE_HOST_WIN32)
    # https://github.com/protocolbuffers/protobuf/tree/master/cmake#notes-on-compiler-warnings
    target_compile_options(hef_proto PRIVATE /wd4244)
endif()
get_filename_component(PROTO_HEADER_DIRECTORY ${PROTO_HEF_HEADER} DIRECTORY)
target_include_directories(hef_proto
    PUBLIC
    $<BUILD_INTERFACE: ${PROTO_HEADER_DIRECTORY}>
    $<BUILD_INTERFACE: ${Protobuf_INCLUDE_DIRS}>
)

protobuf_generate_cpp(PROTO_SCHEDULER_MON_SRC PROTO_SCHEDULER_MON_HEADR scheduler_mon.proto)
add_library(scheduler_mon_proto ${PROTO_SCHEDULER_MON_SRC} ${PROTO_SCHEDULER_MON_HEADR})
target_link_libraries(scheduler_mon_proto libprotobuf-lite)
set_target_properties(scheduler_mon_proto PROPERTIES CXX_STANDARD 17 GENERATED TRUE POSITION_INDEPENDENT_CODE ON)
if(CMAKE_HOST_WIN32)
    target_compile_options(scheduler_mon_proto PRIVATE /wd4244)
endif()
get_filename_component(PROTO_SCHEDULER_MON_HEADER_DIRECTORY ${PROTO_SCHEDULER_MON_HEADR} DIRECTORY)
target_include_directories(scheduler_mon_proto
    PUBLIC
    $<BUILD_INTERFACE: ${PROTO_SCHEDULER_MON_HEADER_DIRECTORY}>
    $<BUILD_INTERFACE: ${Protobuf_INCLUDE_DIRS}>
)

protobuf_generate_cpp(PROTO_PROFILER_SRC PROTO_PROFILER_HEADR tracer_profiler.proto)
add_library(profiler_proto ${PROTO_PROFILER_SRC} ${PROTO_PROFILER_HEADR})
target_link_libraries(profiler_proto libprotobuf-lite)
set_target_properties(profiler_proto PROPERTIES CXX_STANDARD 17 GENERATED TRUE POSITION_INDEPENDENT_CODE ON)
if(CMAKE_HOST_WIN32)
    target_compile_options(profiler_proto PRIVATE /wd4244)
endif()
get_filename_component(PROTO_PROFILER_HEADER_DIRECTORY ${PROTO_PROFILER_HEADR} DIRECTORY)
target_include_directories(profiler_proto
    PUBLIC
    $<BUILD_INTERFACE: ${PROTO_PROFILER_HEADER_DIRECTORY}>
    $<BUILD_INTERFACE: ${Protobuf_INCLUDE_DIRS}>
)

add_subdirectory(src)
set(NET_FLOW_INFRA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/infra/net_flow")

if(HAILO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(HAILO_BUILD_UT)
    add_subdirectory(tests)
endif()
add_subdirectory(bindings)
if(HAILO_BUILD_DOC)
    add_subdirectory(doc)
endif()

# Support builds without installation
set(HailoRT_DIR "${HailoRT_DIR}" PARENT_SCOPE)