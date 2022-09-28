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

namespace hailort
{

// TODO: Deal with longer networks names - should use HAILO_MAX_NETWORK_NAME_SIZE but its too long for one line
constexpr size_t NETWORK_NAME_WIDTH = 40;
constexpr size_t STREAM_NAME_WIDTH = 60;
constexpr size_t NUMBER_WIDTH = 15;
constexpr size_t LINE_LENGTH = 125;

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
        std::setw(NUMBER_WIDTH) << std::left << "Core%" << 
        std::setw(NUMBER_WIDTH) << std::left << "PID" << 
        "\n" << std::left << std::string(LINE_LENGTH, '-') << "\n";
    static const uint32_t header_lines_count = 2;

    return header_lines_count;
}

size_t MonCommand::print_networks_info_table(const ProtoMon &mon_message)
{
    const std::string &pid = mon_message.pid();
    for (auto net_info : mon_message.networks_infos()) {
        auto &net_name = net_info.network_name();
        auto fps = net_info.fps();
        auto core = net_info.core_utilization();

        std::cout << std::setprecision(1) << std::fixed <<
            std::setw(NETWORK_NAME_WIDTH) << std::left << net_name <<
            std::setw(NUMBER_WIDTH) << std::left << fps <<
            std::setw(NUMBER_WIDTH) << std::left << core << 
            std::setw(NUMBER_WIDTH) << std::left << pid << "\n";
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
hailo_status MonCommand::print_table()
{
    while (true) {
        auto epsilon = std::chrono::milliseconds(500);
        std::chrono::milliseconds time_interval = DEFAULT_SCHEDULER_MON_INTERVAL + epsilon;
        auto scheduler_mon_files = Filesystem::get_latest_files_in_dir_flat(SCHEDULER_MON_TMP_DIR, time_interval);
        if (HAILO_SUCCESS != scheduler_mon_files.status() || scheduler_mon_files->empty()) {
            LOGGER__WARNING("Getting scheduler monitor files failed. Please check the application is running and environment variable '{}' is set to 1.",
                SCHEDULER_MON_ENV_VAR);
            return HAILO_NOT_FOUND;
        }

        std::vector<ProtoMon> mon_messages;
        mon_messages.reserve(scheduler_mon_files->size());
        for (const auto &mon_file : scheduler_mon_files.value()) {
            auto file = LockedFile::create(mon_file, "r");
            if (HAILO_SUCCESS != file.status()) {
                LOGGER__ERROR("Failed to open and lock file {}, with status: {}", mon_file, file.status());
                continue;
            }

            ProtoMon mon_message;
            if (!mon_message.ParseFromFileDescriptor(file->get_fd())) {
                LOGGER__WARNING("Failed to ParseFromFileDescriptor monitor file {} with errno {}", mon_file, errno);
                continue;
            }

            mon_messages.emplace_back(std::move(mon_message));
        }

        size_t total_lines_count = print_networks_info_header();
        for (auto &mon_message : mon_messages) {
            total_lines_count += print_networks_info_table(mon_message);
        }

        std::cout << "\n\n";
        total_lines_count += 2;

        total_lines_count += print_frames_header();
        for (auto &mon_message : mon_messages) {
            total_lines_count += print_frames_table(mon_message);
        }
        CliCommon::reset_cursor(total_lines_count);

        std::this_thread::sleep_for(DEFAULT_SCHEDULER_MON_INTERVAL);
    }
    
    return HAILO_SUCCESS;
}
#endif

} /* namespace hailort */

