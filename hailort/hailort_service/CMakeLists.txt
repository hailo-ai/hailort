cmake_minimum_required(VERSION 3.0.0)

add_executable(hailort_service
    hailort_rpc_service.cpp
    hailort_service.cpp
    service_resource_manager.hpp
    ${HAILORT_COMMON_CPP_SOURCES}
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
    ${HAILORT_COMMON_DIR}
    ${COMMON_INC_DIR}
    ${RPC_DIR}
)
disable_exceptions(hailort_service)

set(SYSTEMD_UNIT_DIR "lib/systemd/system/")
if(NOT CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(SYSTEMD_UNIT_DIR ${CMAKE_INSTALL_PREFIX}/${SYSTEMD_UNIT_DIR})
endif()

# Install service's environment variables file
set(ENV_VARS_FILE_DIR ${CMAKE_INSTALL_SYSCONFDIR}/default/)
set(HAILORT_SERVICE_ENV_VARS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/hailort_service)
install(
    FILES "${HAILORT_SERVICE_ENV_VARS_FILE}"
    DESTINATION "${ENV_VARS_FILE_DIR}"
    CONFIGURATIONS Release
    COMPONENT hailort_service
)

# Install systemd unit file
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

set(DAEMON_PID_DIR "/run/hailo")
add_definitions(-DHAILO_DAEMON_PID_DIR="${DAEMON_PID_DIR}")
add_definitions(-DHAILO_DAEMON_PID_FILE="${DAEMON_PID_DIR}/hailort_service.pid")

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    # Create empty directory for hailort log file
    set(HAILORT_LOG_DIR "/var/log/hailo")
    install(DIRECTORY DESTINATION ${HAILORT_LOG_DIR})

    # Create empty directory for default PID file
    install(DIRECTORY DESTINATION ${DAEMON_PID_DIR})
endif()