cmake_minimum_required(VERSION 3.11)

set(HAILOTRACKER_SOURCES
    hailotracker.cpp
    tracklet.cpp
    kalman_filter.cpp
    tracker_core.cpp
    association.cpp
)

add_library(hailotracker_internal OBJECT ${HAILOTRACKER_SOURCES})
target_include_directories(hailotracker_internal
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../libhailort/include
)
target_link_libraries(hailotracker_internal PRIVATE Eigen3::Eigen tl::expected)
set_target_properties(hailotracker_internal PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_compile_options(hailotracker_internal PRIVATE ${HAILORT_COMPILE_OPTIONS})
target_compile_definitions(hailotracker_internal PRIVATE _HAILOPP_EXPORTING)
disable_exceptions(hailotracker_internal)
