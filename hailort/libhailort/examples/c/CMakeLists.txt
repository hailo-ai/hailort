cmake_minimum_required(VERSION 3.0.0)

file(GLOB_RECURSE C_EXAMPLE_SOURCES "*.c")
SET_SOURCE_FILES_PROPERTIES(${C_EXAMPLE_SOURCES} PROPERTIES LANGUAGE C)

add_executable(c_data_quantization_example data_quantization_example.c)
target_link_libraries(c_data_quantization_example PRIVATE example_base)

add_executable(c_raw_streams_example raw_streams_example.c)
target_link_libraries(c_raw_streams_example PRIVATE example_base)

add_executable(c_vstreams_example vstreams_example.c)
target_link_libraries(c_vstreams_example PRIVATE example_base)

add_executable(c_infer_pipeline_example infer_pipeline_example.c)
target_link_libraries(c_infer_pipeline_example PRIVATE example_base)

add_executable(c_multi_network_vstream_example multi_network_vstream_example.c)
target_link_libraries(c_multi_network_vstream_example PRIVATE example_base)

add_executable(c_switch_hefs_example switch_hefs_example.c)
target_link_libraries(c_switch_hefs_example PRIVATE example_base)

add_executable(c_switch_single_io_hefs_example switch_single_io_hefs_example.c)
target_link_libraries(c_switch_single_io_hefs_example PRIVATE example_base)

add_executable(multi_device_example_c multi_device_example.c)
target_link_libraries(multi_device_example_c PRIVATE example_base)

set(EXAMPLES_C_TARGETS
    c_data_quantization_example
    c_raw_streams_example
    c_vstreams_example
    c_infer_pipeline_example
    c_multi_network_vstream_example
    c_switch_hefs_example
    c_switch_single_io_hefs_example
    multi_device_example_c
    PARENT_SCOPE)
