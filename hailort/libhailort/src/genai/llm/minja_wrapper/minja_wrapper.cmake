cmake_minimum_required(VERSION 3.5.0)

include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/json.cmake)
include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/minja.cmake)

add_library(minja_wrapper
    STATIC
    ${CMAKE_CURRENT_LIST_DIR}/minja_wrapper.cpp
)

if(WIN32)
    target_compile_options(minja_wrapper PRIVATE
        /DNOMINMAX # NOMINMAX is required in order to play nice with std::min/std::max (otherwise Windows.h defines it's own)
        /wd4251    # C++ ABI with STL
    )
endif()

set_target_properties(minja_wrapper PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
target_include_directories(minja_wrapper PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(minja_wrapper
    PRIVATE
    hailort_common
    minja
    nlohmann_json
)