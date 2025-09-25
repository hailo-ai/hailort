cmake_minimum_required(VERSION 3.5.0)

add_subdirectory(data_quantization_example)
add_subdirectory(raw_streams_example)
add_subdirectory(vstreams_example)
add_subdirectory(infer_pipeline_example)
add_subdirectory(multi_network_vstream_example)
add_subdirectory(switch_network_groups_example)
add_subdirectory(switch_network_groups_manually_example)
add_subdirectory(multi_device_example)
add_subdirectory(power_measurement_example)
add_subdirectory(notification_callback_example)

set(C_EXAMPLE_TARGETS
    c_data_quantization_example
    c_raw_streams_example
    c_vstreams_example
    c_infer_pipeline_example
    c_multi_network_vstream_example
    c_switch_network_groups_example
    c_switch_network_groups_manually_example
    c_multi_device_example
    c_power_measurement_example
    c_notification_callback_example
)

if(NOT CMAKE_SYSTEM_NAME STREQUAL QNX)
    # TODO: HRT-10956 support QNX async examples
    add_subdirectory(raw_async_streams_single_thread_example)
    set(C_EXAMPLE_TARGETS ${C_EXAMPLE_TARGETS} c_raw_async_streams_single_thread_example)
endif()

add_custom_target(c_hailort_examples)
add_dependencies(c_hailort_examples ${C_EXAMPLE_TARGETS})