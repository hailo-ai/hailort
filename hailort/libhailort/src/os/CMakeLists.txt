cmake_minimum_required(VERSION 3.5.0)

if(WIN32)
    add_subdirectory(windows)
elseif(UNIX)
    add_subdirectory(linux)
else()
    message(FATAL_ERROR "Unexpeced platform target, stopping build")
endif()

set(HAILORT_CPP_SOURCES ${HAILORT_CPP_SOURCES} ${SRC_FILES} PARENT_SCOPE)