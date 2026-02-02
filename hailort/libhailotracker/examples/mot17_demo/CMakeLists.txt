cmake_minimum_required(VERSION 3.11)

set(EXAMPLE_NAME hailotracker_mot17_demo)

add_executable(${EXAMPLE_NAME}
    mot17_demo.cpp
)
add_dependencies(${EXAMPLE_NAME} hailotracker)

set(LIBHAILOTRACKER_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(LIBHAILORT_ROOT ${LIBHAILOTRACKER_ROOT}/../libhailort)

target_include_directories(${EXAMPLE_NAME} PRIVATE
    $<BUILD_INTERFACE:${LIBHAILOTRACKER_ROOT}/include>
    $<BUILD_INTERFACE:${LIBHAILORT_ROOT}/include>
)

set_property(TARGET ${EXAMPLE_NAME} PROPERTY CXX_STANDARD 17)

if(WIN32)
    target_compile_options(${EXAMPLE_NAME} PRIVATE
        /DNOMINMAX # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4251    # C++ ABI with STL
    )
endif()

target_link_libraries(${EXAMPLE_NAME} PRIVATE hailotracker)

add_dependencies(hailotracker_examples ${EXAMPLE_NAME})

install(TARGETS ${EXAMPLE_NAME}
    RUNTIME DESTINATION bin/examples
)
