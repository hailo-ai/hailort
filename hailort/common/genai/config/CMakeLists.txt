cmake_minimum_required(VERSION 3.5.0)

include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/json.cmake)

# Built with exceptions, like minja_wrapper, so libhailort holds a single variant of nlohmann's inline code
add_library(genai_config STATIC EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/config_json_reader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/embedding_tables.cpp
)

set_target_properties(genai_config PROPERTIES
    CXX_STANDARD              17
    CXX_STANDARD_REQUIRED     YES
    CXX_EXTENSIONS            NO
    POSITION_INDEPENDENT_CODE ON
)

target_compile_options(genai_config PRIVATE ${HAILORT_COMPILE_OPTIONS})
target_link_libraries(genai_config
    PRIVATE
    hailort_common
    nlohmann_json
)
