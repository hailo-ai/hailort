include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/common_compiler_options.cmake)

# Proto file
get_filename_component(hailort_rpc_proto "hailort_rpc.proto" ABSOLUTE)
get_filename_component(hailort_rpc_proto_path "${hailort_rpc_proto}" PATH)
# Generated sources
set(hailort_rpc_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/hailort_rpc.pb.cc")
set(hailort_rpc_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/hailort_rpc.pb.h")
set(hailort_rpc_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/hailort_rpc.grpc.pb.cc")
set(hailort_rpc_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/hailort_rpc.grpc.pb.h")

set(HAILO_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
if(HAILO_BUILD_SERVICE)
    if(CMAKE_HOST_UNIX)
        set(HAILO_GRPC_CPP_PLUGIN_EXECUTABLE "${HAILO_EXTERNAL_DIR}/grpc-build/grpc_cpp_plugin")
    else()
        set(HAILO_GRPC_CPP_PLUGIN_EXECUTABLE "${HAILO_EXTERNAL_DIR}/grpc-build/Release/grpc_cpp_plugin.exe")
    endif()
endif()

add_custom_command(
    OUTPUT "${hailort_rpc_proto_srcs}" "${hailort_rpc_proto_hdrs}" "${hailort_rpc_grpc_srcs}" "${hailort_rpc_grpc_hdrs}"
    COMMAND ${HAILO_PROTOBUF_PROTOC}
    ARGS
        --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${hailort_rpc_proto_path}"
        --plugin=protoc-gen-grpc="${HAILO_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${hailort_rpc_proto}"
    DEPENDS "${hailort_rpc_proto}")
    
add_library(hailort_rpc_grpc_proto STATIC EXCLUDE_FROM_ALL
    ${hailort_rpc_grpc_srcs}
    ${hailort_rpc_grpc_hdrs}
    ${hailort_rpc_proto_srcs}
    ${hailort_rpc_proto_hdrs})

set_target_properties(hailort_rpc_grpc_proto PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(hailort_rpc_grpc_proto libprotobuf-lite grpc++_unsecure)
# Include generated *.pb.h files
target_include_directories(hailort_rpc_grpc_proto PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
disable_exceptions(hailort_rpc_grpc_proto)