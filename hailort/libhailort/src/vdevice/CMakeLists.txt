cmake_minimum_required(VERSION 3.0.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/vdevice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vdevice_core_op.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline_multiplexer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vdevice_stream.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vdevice_stream_multiplexer_wrapper.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler/network_group_scheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler/scheduler_oracle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler/scheduled_core_op_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler/multi_device_scheduled_stream.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)
