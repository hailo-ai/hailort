cmake_minimum_required(VERSION 3.0.0)

include(GNUInstallDirs)

set(HAILORTCLI_CPP_FILES
    hailortcli.cpp
    command.cpp
    scan_command.cpp
    run_command.cpp
    inference_progress.cpp
    power_measurement_command.cpp
    fw_control_command.cpp
    fw_update_command.cpp
    ssb_update_command.cpp
    infer_stats_printer.cpp
    sensor_config_command.cpp
    board_config_command.cpp
    fw_config_command.cpp
    fw_logger_command.cpp
    fw_config_serializer.cpp
    common.cpp
    benchmark_command.cpp
    temp_measurement.cpp
    parse_hef_command.cpp
    graph_printer.cpp
    )
    
if(UNIX)
    # Unix only modules
    set(HAILORTCLI_CPP_FILES ${HAILORTCLI_CPP_FILES}
        udp_rate_limiter_command.cpp
        # TODO: We dont compile download_action_list_command on windows, as it uses packed enums (HRT-5919)
        download_action_list_command.cpp
    )
endif()

# 'config_definitions_json_file' is used in generate_definitions_json_str.in for configure_file()
file(READ "${PROJECT_SOURCE_DIR}/common/config_definitions.json" config_definitions_json_file)
set(config_definitions_header ${CMAKE_CURRENT_BINARY_DIR}/definitions_json.auto.hpp)
set(CONFIG_DEFENITIONS_IN ${PROJECT_SOURCE_DIR}/hailort/hailortcli/generate_definitions_json_str.in)
configure_file(${CONFIG_DEFENITIONS_IN} ${config_definitions_header})

add_executable(hailortcli
    ${config_definitions_header}
    ${HAILORTCLI_CPP_FILES}
    ${HAILORT_COMMON_CPP_SOURCES}
    ${PROJECT_SOURCE_DIR}/common/src/firmware_header_utils.c
    ${PROJECT_SOURCE_DIR}/common/src/md5.c
    ${HAILORT_SRC_DIR}/pipeline.cpp
    ${HAILO_FULL_OS_DIR}/event.cpp
)
target_compile_options(hailortcli PRIVATE ${HAILORT_COMPILE_OPTIONS})
set_property(TARGET hailortcli PROPERTY CXX_STANDARD 14)
set_property(TARGET hailortcli PROPERTY INSTALL_RPATH "$ORIGIN" "${CMAKE_INSTALL_LIBDIR}")
target_link_libraries(hailortcli libhailort CLI11::CLI11 nlohmann_json spdlog::spdlog readerwriterqueue)
target_link_libraries(hailortcli DotWriter)
# TODO: Remove microprofile after removing pipeline.cpp from hailortcli sources
target_link_libraries(hailortcli microprofile)

if(WIN32)
    target_link_libraries(hailortcli Ws2_32 Iphlpapi Shlwapi)
elseif(CMAKE_SYSTEM_NAME STREQUAL QNX)
    target_link_libraries(hailortcli pevents)
endif()
target_include_directories(hailortcli
    PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR} # CMAKE_CURRENT_BINARY_DIR is necessary for config_definitions_header
    ${HAILORT_COMMON_DIR}
    ${COMMON_INC_DIR}
    ${HAILORT_SRC_DIR}
)

install(TARGETS hailortcli
   RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
   CONFIGURATIONS Release)