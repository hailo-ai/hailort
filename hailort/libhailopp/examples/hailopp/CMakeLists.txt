cmake_minimum_required(VERSION 3.11)

project(hailopp-examples-hailopp)

find_package(HailoRT 5 REQUIRED)
find_package(HailoPP 1 REQUIRED)

set(HAILOPP_EXAMPLES
    mot17_demo
)

set(HAILOPP_EXAMPLE_TARGETS)
foreach(EXAMPLE_NAME IN LISTS HAILOPP_EXAMPLES)
    set(EXAMPLE_DIR "hailopp_${EXAMPLE_NAME}")
    add_subdirectory(${EXAMPLE_DIR})

    set(EXAMPLE_TARGET "hailopp_${EXAMPLE_NAME}")
    list(APPEND HAILOPP_EXAMPLE_TARGETS ${EXAMPLE_TARGET})
endforeach()

add_custom_target(hailopp_hailopp_examples)
add_dependencies(hailopp_hailopp_examples ${HAILOPP_EXAMPLE_TARGETS})
