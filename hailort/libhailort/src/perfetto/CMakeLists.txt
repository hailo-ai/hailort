
cmake_minimum_required(VERSION 3.5.0)

if(ENABLE_PERFETTO)
    find_library(PERFETTO_LIB
        NAMES perfetto
        PATHS /usr/lib
        REQUIRED FALSE
    )

    set(SRC_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/hailort_perfetto.cpp
    )
    set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)

    if(PERFETTO_LIB)
        set_property(GLOBAL APPEND PROPERTY PERFETTO_COMPILE_DEFS HAVE_PERFETTO)
    else()
        message(FATAL_ERROR "ENABLE_PERFETTO=ON but Perfetto library not found")
    endif()
else()
    message(STATUS "Perfetto disabled (ENABLE_PERFETTO=OFF)")
    set_property(GLOBAL APPEND PROPERTY PERFETTO_COMPILE_DEFS PERFETTO_DISABLED)
endif()
