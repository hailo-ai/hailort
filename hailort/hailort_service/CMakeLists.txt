cmake_minimum_required(VERSION 3.0.0)

add_executable(hailort_service
    hailort_rpc_service.cpp
    hailort_service.cpp
    service_resource_manager.hpp
)
target_compile_options(hailort_service PRIVATE ${HAILORT_COMPILE_OPTIONS})
set_property(TARGET hailort_service PROPERTY CXX_STANDARD 14)
target_link_libraries(hailort_service
    libhailort
    spdlog::spdlog
    grpc++_unsecure
    hailort_rpc_grpc_proto)
target_include_directories(hailort_service
    PRIVATE
    ${HAILORT_INC_DIR}
    ${COMMON_INC_DIR}
    ${RPC_DIR}
)
disable_exceptions(hailort_service)

# Install systemd unit file
set(SYSTEMD_UNIT_DIR "/lib/systemd/system/")
if(NOT CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(SYSTEMD_UNIT_DIR ${CMAKE_INSTALL_PREFIX}/${SYSTEMD_UNIT_DIR})
endif()

set(HAILORT_SERVICE_UNIT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/hailort.service)
install(
    FILES "${HAILORT_SERVICE_UNIT_FILE}"
    DESTINATION "${SYSTEMD_UNIT_DIR}"
    CONFIGURATIONS Release
    COMPONENT hailort_service
)

install(
    TARGETS hailort_service
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    CONFIGURATIONS Release
)

# Create empty directory for hailort log file
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(HAILORT_LOG_DIR "/var/log/hailo")
    install(DIRECTORY DESTINATION ${HAILORT_LOG_DIR})
endif()