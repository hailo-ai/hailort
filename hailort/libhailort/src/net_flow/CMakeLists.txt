cmake_minimum_required(VERSION 3.0.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/nms_post_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/yolo_post_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/yolox_post_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/ssd_post_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/argmax_post_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/softmax_post_process.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/pipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/inference_pipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/vstream.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)
