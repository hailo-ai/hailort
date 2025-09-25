cmake_minimum_required(VERSION 3.5.0)

include(GNUInstallDirs)
include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/cli11.cmake)
include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/json.cmake)
include(${HAILO_EXTERNALS_CMAKE_SCRIPTS}/dotwriter.cmake)

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
    parse_hef_command.cpp
    memory_requirements_command.cpp
    graph_printer.cpp
    mon_command.cpp

    run2/run2_command.cpp
    run2/network_runner.cpp
    run2/live_stats.cpp
    run2/timer_live_track.cpp
    run2/network_live_track.cpp
    run2/measurement_live_track.cpp
    run2/io_wrappers.cpp
    download_action_list_command.cpp
    )

if(UNIX)
    # Unix only modules
    set(HAILORTCLI_CPP_FILES ${HAILORTCLI_CPP_FILES}
        udp_rate_limiter_command.cpp
        measure_nnc_performance_command.cpp
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
    ${PROJECT_SOURCE_DIR}/common/src/firmware_header_utils.c
    ${PROJECT_SOURCE_DIR}/common/src/md5.c
    ${HAILORT_SRC_DIR}/net_flow/pipeline/pipeline.cpp # TODO: link dynamically with libhailort
)

target_compile_options(hailortcli PRIVATE ${HAILORT_COMPILE_OPTIONS})
set_property(TARGET hailortcli PROPERTY CXX_STANDARD 14)

# Link with a relative libhailort
set_property(TARGET hailortcli PROPERTY INSTALL_RPATH "$ORIGIN")
set_property(TARGET hailortcli APPEND PROPERTY INSTALL_RPATH "\$ORIGIN/../lib/")

target_link_libraries(hailortcli
    libhailort
    CLI11::CLI11
    nlohmann_json
    hailort_common
    eigen
    DotWriter
    scheduler_mon_proto
    profiler_proto)

if(WIN32)
    target_link_libraries(hailortcli winmm.lib) # Used for timeBeginPeriod, timeEndPeriod
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
   CONFIGURATIONS Release
)
cli11_install_completion_file(hailortcli)
