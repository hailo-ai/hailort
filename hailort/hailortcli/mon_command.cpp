/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file mon_command.cpp
 * @brief Monitor of networks - Presents information about the running networks
 **/

#include "mon_command.hpp"
#include "common.hpp"
#include "hailo/hailort.h"
#include "common/filesystem.hpp"

#include <iostream>
#if defined(__GNUC__)
#include <sys/ioctl.h>
#endif

namespace hailort
{

// TODO: Deal with longer networks names - should use HAILO_MAX_NETWORK_NAME_SIZE but its too long for one line
constexpr size_t NETWORK_NAME_WIDTH = 40;
constexpr size_t STREAM_NAME_WIDTH = 60;
constexpr size_t ACTIVE_TIME_WIDTH = 25;
constexpr size_t NUMBER_WIDTH = 15;
constexpr size_t TERMINAL_DEFAULT_WIDTH = 80;
constexpr size_t LINE_LENGTH = 125;
constexpr std::chrono::milliseconds EPSILON_TIME(500);

MonCommand::MonCommand(CLI::App &parent_app) :
    Command(parent_app.add_subcommand("monitor", "Monitor of networks - Presents information about the running networks. " \
     "To enable monitor, set in the application process the environment variable '" + std::string(SCHEDULER_MON_ENV_VAR) + "' to 1."))
{}

hailo_status MonCommand::execute()
{
#ifdef _WIN32
    LOGGER__ERROR("hailortcli `monitor` command is not supported on Windows");
    return HAILO_NOT_IMPLEMENTED;
#else
    return print_table();
#endif
}

size_t MonCommand::print_networks_info_header()
{
    std::cout << 
        std::setw(NETWORK_NAME_WIDTH) << std::left << "Network" <<
        std::setw(NUMBER_WIDTH) << std::left << "FPS" <<
        std::setw(ACTIVE_TIME_WIDTH) << std::left << "Active Time (%) " <<
        std::setw(NUMBER_WIDTH) << std::left << "PID" << 
        "\n" << std::left << std::string(LINE_LENGTH, '-') << "\n";
    static const uint32_t header_lines_count = 2;

    return header_lines_count;
}

size_t MonCommand::print_networks_info_table(const ProtoMon &mon_message)
{
    const uint32_t NUMBER_OBJECTS_COUNT = 3;
    auto data_line_len = (NUMBER_WIDTH * NUMBER_OBJECTS_COUNT) + NETWORK_NAME_WIDTH;
    auto rest_line_len = LINE_LENGTH - data_line_len;

    const std::string &pid = mon_message.pid();
    for (auto net_info : mon_message.networks_infos()) {
        auto &net_name = net_info.network_name();
        auto fps = net_info.fps();
        auto active_time = net_info.active_time();

        std::cout << std::setprecision(1) << std::fixed <<
            std::setw(NETWORK_NAME_WIDTH) << std::left << net_name <<
            std::setw(NUMBER_WIDTH) << std::left << fps <<
            std::setw(ACTIVE_TIME_WIDTH) << std::left << active_time <<
            std::setw(NUMBER_WIDTH) << std::left << pid << std::string(rest_line_len, ' ') << "\n";
    }

    return mon_message.networks_infos().size();
}

size_t MonCommand::print_frames_header()
{
    std::cout << 
        std::setw(NETWORK_NAME_WIDTH) << std::left << "Network" <<
        std::setw(STREAM_NAME_WIDTH) << std::left << "Stream" <<
        std::setw(NUMBER_WIDTH) << std::left << "Direction" << 
        std::setw(NUMBER_WIDTH) << std::left << "Frames" << 
        "\n" << std::left << std::string(LINE_LENGTH, '-') << "\n";
    static const size_t header_lines_count = 2;
    return header_lines_count;
}

size_t MonCommand::print_frames_table(const ProtoMon &mon_message)
{
    size_t table_lines_count = 0;
    for (auto &net_info : mon_message.net_frames_infos()) {
        auto &net_name = net_info.network_name();
        table_lines_count += net_info.streams_frames_infos().size();
        for (auto &streams_frames : net_info.streams_frames_infos()) {
            auto &stream_name = streams_frames.stream_name();
            auto stream_direction = (streams_frames.stream_direction() == PROTO__STREAM_DIRECTION__HOST_TO_DEVICE) ? "H2D" : "D2H";

            std::string frames;
            if (SCHEDULER_MON_NAN_VAL == streams_frames.buffer_frames_size() || SCHEDULER_MON_NAN_VAL == streams_frames.pending_frames_count()) {
                frames = "NaN";
            } else {
                frames = std::to_string(streams_frames.pending_frames_count()) + "/" + std::to_string(streams_frames.buffer_frames_size());
            }
            
            std::cout << 
                std::setw(NETWORK_NAME_WIDTH) << std::left << net_name <<
                std::setw(STREAM_NAME_WIDTH) << std::left << stream_name <<
                std::setw(NUMBER_WIDTH) << std::left << stream_direction << 
                std::setw(NUMBER_WIDTH) << std::left << frames << "\n";
        }
    }
    
    return table_lines_count;
}

#if defined(__GNUC__)
Expected<uint16_t> get_terminal_line_width()
{
    struct winsize w;
    int ret = ioctl(0, TIOCGWINSZ, &w);
    if (ret != 0) {
        LOGGER__DEBUG("Failed to get_terminal_line_width, with errno: {}, using default value: {}", errno);
        return TERMINAL_DEFAULT_WIDTH;
    }

    uint16_t terminal_line_width = w.ws_col;
    return terminal_line_width;
}

hailo_status MonCommand::print_table()
{
    std::chrono::milliseconds time_interval = DEFAULT_SCHEDULER_MON_INTERVAL + EPSILON_TIME;
    auto terminal_line_width_expected = get_terminal_line_width();
    CHECK_EXPECTED_AS_STATUS(terminal_line_width_expected);
    auto terminal_line_width = terminal_line_width_expected.release();

    size_t last_run_total_lines_count = 0;
    bool data_was_printed = false;
    while (true) {
        size_t total_lines_count = 0;
        bool print_warning_msg = true; // Will change to false only if mon directory is valid and there are updated files in it.

        auto mon_dir_valid = Filesystem::is_directory(SCHEDULER_MON_TMP_DIR);
        CHECK_EXPECTED_AS_STATUS(mon_dir_valid);

        std::vector<ProtoMon> mon_messages;
        if (mon_dir_valid.value()) {
            auto scheduler_mon_files = Filesystem::get_latest_files_in_dir_flat(SCHEDULER_MON_TMP_DIR, time_interval);
            CHECK_EXPECTED_AS_STATUS(scheduler_mon_files);
            print_warning_msg = scheduler_mon_files->empty();

            mon_messages.reserve(scheduler_mon_files->size());
            for (const auto &mon_file : scheduler_mon_files.value()) {
                auto file = LockedFile::create(mon_file, "r");
                if (HAILO_SUCCESS != file.status()) {
                    LOGGER__ERROR("Failed to open and lock file {}, with status: {}", mon_file, file.status());
                    total_lines_count++;
                    continue;
                }

                ProtoMon mon_message;
                if (!mon_message.ParseFromFileDescriptor(file->get_fd())) {
                    LOGGER__WARNING("Failed to ParseFromFileDescriptor monitor file {} with errno {}", mon_file, errno);
                    total_lines_count++;
                    continue;
                }

                mon_messages.emplace_back(std::move(mon_message));
            }
        }

        total_lines_count += print_networks_info_header();
        for (auto &mon_message : mon_messages) {
            total_lines_count += print_networks_info_table(mon_message);
        }

        std::cout << std::string(terminal_line_width, ' ') << "\n";
        std::cout << std::string(terminal_line_width, ' ') << "\n";
        total_lines_count += 2;

        total_lines_count += print_frames_header();
        for (auto &mon_message : mon_messages) {
            total_lines_count += print_frames_table(mon_message);
        }

        if (print_warning_msg) {
            std::cout << "Monitor did not retrieve any files. This occurs when there is no application currently running. If this is not the case, verify that environment variable '" <<
                SCHEDULER_MON_ENV_VAR << "' is set to 1.\n";
            total_lines_count++;

            if (data_was_printed) {
                auto lines_to_clear = last_run_total_lines_count - total_lines_count;
                CliCommon::clear_lines_down(lines_to_clear);
                total_lines_count += lines_to_clear;
                data_was_printed = false;
            }
        }
        else {
            data_was_printed = true;
            last_run_total_lines_count = total_lines_count;
        }

        CliCommon::reset_cursor(total_lines_count);
        std::this_thread::sleep_for(DEFAULT_SCHEDULER_MON_INTERVAL);
    }
    
    return HAILO_SUCCESS;
}
#endif

} /* namespace hailort */

