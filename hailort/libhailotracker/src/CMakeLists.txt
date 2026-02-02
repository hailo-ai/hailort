cmake_minimum_required(VERSION 3.11)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(HAILOTRACKER_SOURCES
    hailotracker.cpp
    tracklet.cpp
    kalman_filter.cpp
    tracker_core.cpp
    association.cpp
)

set(HAILOTRACKER_PUBLIC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/hailotracker.h
)

add_library(hailotracker_internal OBJECT ${HAILOTRACKER_SOURCES})
target_include_directories(hailotracker_internal
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/../../libhailort/include
)
target_link_libraries(hailotracker_internal PRIVATE Eigen3::Eigen)
set_target_properties(hailotracker_internal PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_compile_options(hailotracker_internal PRIVATE ${HAILORT_COMPILE_OPTIONS})
disable_exceptions(hailotracker_internal)

add_library(hailotracker SHARED $<TARGET_OBJECTS:hailotracker_internal>)
target_include_directories(hailotracker
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(hailotracker PRIVATE Eigen3::Eigen)
set_target_properties(hailotracker PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${HAILO_TRACKER_MAJOR_VERSION}
    PUBLIC_HEADER "${HAILOTRACKER_PUBLIC_HEADERS}"
)

install(TARGETS hailotracker
    EXPORT HailoTrackerTargets
    CONFIGURATIONS Release
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION include
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

set(CMAKE_SCRIPTS_DIR ${CMAKE_CURRENT_BINARY_DIR})
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/../Config.cmake.in
  "${CMAKE_SCRIPTS_DIR}/HailoTrackerConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HailoTracker
)
write_basic_package_version_file(
  "${CMAKE_SCRIPTS_DIR}/HailoTrackerConfigVersion.cmake"
  VERSION "${HAILO_TRACKER_MAJOR_VERSION}.${HAILO_TRACKER_MINOR_VERSION}.${HAILO_TRACKER_REVISION_VERSION}"
  COMPATIBILITY SameMajorVersion
)

# Support builds without installation
set(HailoTracker_DIR "${CMAKE_SCRIPTS_DIR}" PARENT_SCOPE)

install(FILES
        "${CMAKE_SCRIPTS_DIR}/HailoTrackerConfig.cmake"
        "${CMAKE_SCRIPTS_DIR}/HailoTrackerConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HailoTracker
    COMPONENT libhailotracker
)
install(EXPORT HailoTrackerTargets
    FILE HailoTrackerTargets.cmake
    NAMESPACE HailoTracker::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HailoTracker
    COMPONENT libhailotracker
)
