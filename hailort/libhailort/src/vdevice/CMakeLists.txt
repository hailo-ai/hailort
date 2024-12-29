cmake_minimum_required(VERSION 3.5.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/vdevice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vdevice_core_op.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vdevice_hrpc_client.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/vdevice_native_stream.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/callback_reorder_queue.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler/scheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler/scheduler_oracle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler/scheduled_core_op_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler/scheduled_stream.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler/infer_request_accumulator.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)
