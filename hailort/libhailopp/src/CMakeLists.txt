cmake_minimum_required(VERSION 3.11)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_subdirectory(hailotracker)

set(HAILOPP_PUBLIC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/hailotracker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/status.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/platform.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/expected.hpp
)

add_library(hailopp SHARED $<TARGET_OBJECTS:hailotracker_internal>)
target_include_directories(hailopp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(hailopp PRIVATE Eigen3::Eigen)
set_target_properties(hailopp PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${HAILO_PP_MAJOR_VERSION}
    PUBLIC_HEADER "${HAILOPP_PUBLIC_HEADERS}"
)

install(TARGETS hailopp
    EXPORT HailoPPTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION include
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

set(CMAKE_SCRIPTS_DIR ${CMAKE_CURRENT_BINARY_DIR})
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/../Config.cmake.in
    "${CMAKE_SCRIPTS_DIR}/HailoPPConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HailoPP
)
write_basic_package_version_file(
    "${CMAKE_SCRIPTS_DIR}/HailoPPConfigVersion.cmake"
    VERSION "${HAILO_PP_MAJOR_VERSION}.${HAILO_PP_MINOR_VERSION}.${HAILO_PP_REVISION_VERSION}"
    COMPATIBILITY SameMajorVersion
)

# Support builds without installation
set(HailoPP_DIR "${CMAKE_SCRIPTS_DIR}" PARENT_SCOPE)

install(FILES
        "${CMAKE_SCRIPTS_DIR}/HailoPPConfig.cmake"
        "${CMAKE_SCRIPTS_DIR}/HailoPPConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HailoPP
    COMPONENT libhailopp
)
install(EXPORT HailoPPTargets
    FILE HailoPPTargets.cmake
    NAMESPACE HailoPP::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HailoPP
    COMPONENT libhailopp
)
