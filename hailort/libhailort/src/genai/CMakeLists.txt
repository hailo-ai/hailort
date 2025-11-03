cmake_minimum_required(VERSION 3.5.0)

set(SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/genai_common.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/llm/llm.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vlm/vlm.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/speech2text/speech2text.cpp
)

# Add tokenizer source files when enabled
if(HAILO_BUILD_CLIENT_TOKENIZER)
    list(APPEND SRC_FILES
        ${HAILORT_TOKENIZER_DIR}/hailo_tokenizer.cpp
    )
endif()

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)