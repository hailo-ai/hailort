cmake_minimum_required(VERSION 3.5.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/nms_post_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/yolov5_post_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/yolov5_bbox_only_post_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/yolox_post_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/ssd_post_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/argmax_post_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/softmax_post_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/yolov5_seg_post_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/yolov8_post_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ops/yolov8_bbox_only_post_process.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/pipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/pipeline_internal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/filter_elements.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/queue_elements.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/edge_elements.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/multi_io_elements.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/async_pipeline_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/async_infer_runner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/infer_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/infer_model_hrpc_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/configured_infer_model_hrpc_client.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/vstream_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/vstream.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/inference_pipeline.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)
