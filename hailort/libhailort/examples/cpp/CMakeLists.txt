cmake_minimum_required(VERSION 3.5.0)

project(hailort-examples-cpp)

set(
    CPP_EXAMPLES
    async_infer_advanced
    async_infer_basic
    multi_model_inference
    notification_callback
    power_measurement
)

if(UNIX)
    list(
        APPEND
        CPP_EXAMPLES
        query_performance_and_health_stats
        async_infer_dma_buffer
    )
endif()

# add subdirectories for each example, with suffix "_example" and add to the list of targets with prefix "cpp_" and suffix "_example"
set(CPP_EXAMPLE_TARGETS)
foreach(EXAMPLE_NAME IN LISTS CPP_EXAMPLES)
    string(TOLOWER ${EXAMPLE_NAME} EXAMPLE_NAME_LOWER)

    set(EXAMPLE_DIR "${EXAMPLE_NAME_LOWER}_example")
    add_subdirectory(${EXAMPLE_DIR})

    set(EXAMPLE_TARGET "cpp_${EXAMPLE_DIR}")
    list(APPEND CPP_EXAMPLE_TARGETS ${EXAMPLE_TARGET})
endforeach()

add_custom_target(cpp_hailort_examples)
add_dependencies(cpp_hailort_examples ${CPP_EXAMPLE_TARGETS})
