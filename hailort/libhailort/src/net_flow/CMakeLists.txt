cmake_minimum_required(VERSION 3.0.0)

set(HAILORT_OPS_CPP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/nms_post_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/yolo_post_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/ssd_post_process.cpp
)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/pipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/inference_pipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/vstream.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} ${HAILORT_OPS_CPP_SOURCES} PARENT_SCOPE)
set(HAILORT_OPS_CPP_SOURCES ${HAILORT_OPS_CPP_SOURCES} PARENT_SCOPE)
