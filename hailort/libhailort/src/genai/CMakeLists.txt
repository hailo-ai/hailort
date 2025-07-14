cmake_minimum_required(VERSION 3.5.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/genai_common.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/llm/llm.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text2image/text2image.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vlm/vlm.cpp
)

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)