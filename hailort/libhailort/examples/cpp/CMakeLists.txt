cmake_minimum_required(VERSION 3.5.0)

add_subdirectory(vstreams_example)
add_subdirectory(infer_pipeline_example)
add_subdirectory(async_infer_basic_example)
add_subdirectory(async_infer_advanced_example)
add_subdirectory(raw_streams_example)
add_subdirectory(multi_network_vstream_example)
add_subdirectory(switch_network_groups_example)
add_subdirectory(switch_network_groups_manually_example)
add_subdirectory(multi_device_example)
add_subdirectory(power_measurement_example)
add_subdirectory(multi_process_example)
add_subdirectory(notification_callback_example)

set(CPP_EXAMPLE_TARGETS
    cpp_vstreams_example
    cpp_infer_pipeline_example
    cpp_async_infer_basic_example
    cpp_async_infer_advanced_example
    cpp_raw_streams_example
    cpp_multi_network_vstream_example
    cpp_switch_network_groups_example
    cpp_switch_network_groups_manually_example
    cpp_multi_device_example
    cpp_power_measurement_example
    cpp_multi_process_example
    cpp_notification_callback_example
)

if(NOT CMAKE_SYSTEM_NAME STREQUAL QNX)
    # TODO: HRT-10956 support QNX async examples
    add_subdirectory(raw_async_streams_multi_thread_example)
    add_subdirectory(raw_async_streams_single_thread_example)
    set(CPP_EXAMPLE_TARGETS ${CPP_EXAMPLE_TARGETS}
        cpp_raw_async_streams_multi_thread_example
        cpp_raw_async_streams_single_thread_example)
endif()

add_custom_target(cpp_hailort_examples)
add_dependencies(cpp_hailort_examples ${CPP_EXAMPLE_TARGETS})
