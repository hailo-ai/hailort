/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file mon_command.cpp
 * @brief Monitor of networks - Presents information about the running networks
 **/

#include "hailo/hailort.h"

#include "common/filesystem.hpp"
#include "common/env_vars.hpp"

#include "mon_command.hpp"
#include "common.hpp"

#include <iostream>
#include <signal.h>
#include <thread>
#if defined(__GNUC__)
#include <sys/ioctl.h>
#endif

namespace hailort
{

constexpr size_t STRING_WIDTH = 60;
constexpr size_t NETWORK_GROUP_NAME_WIDTH = STRING_WIDTH;
constexpr size_t DEVICE_ID_WIDTH = STRING_WIDTH;
constexpr size_t STREAM_NAME_WIDTH = STRING_WIDTH;
constexpr size_t UTILIZATION_WIDTH = 25;
constexpr size_t NUMBER_WIDTH = 15;
constexpr size_t FRAME_VALUE_WIDTH = 8;
constexpr size_t TERMINAL_DEFAULT_WIDTH = 80;
constexpr size_t LINE_LENGTH = NETWORK_GROUP_NAME_WIDTH + STREAM_NAME_WIDTH + UTILIZATION_WIDTH + NUMBER_WIDTH;
constexpr std::chrono::milliseconds EPSILON_TIME(500);

inline std::string truncate_str(const std::string &original_str, uint32_t max_length)
{
    static const std::string ELLIPSIS = "...  ";
    return (original_str.length() > max_length) ? original_str.substr(0, (max_length - ELLIPSIS.length())) + ELLIPSIS : original_str;
}

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
    return run_monitor();
#endif
}

void MonCommand::add_devices_info_header(std::ostream &buffer)
{
    buffer <<
        std::setw(DEVICE_ID_WIDTH) << std::left << "Device ID" <<
        std::setw(UTILIZATION_WIDTH) << std::left << "Utilization (%)" <<
        std::setw(STRING_WIDTH) << std::left << "Architecture" <<
        "\n" << std::left << std::string(LINE_LENGTH, '-') << "\n";
}

void MonCommand::add_devices_info_table(const ProtoMon &mon_message, std::ostream &buffer)
{
    auto data_line_len = NUMBER_WIDTH + NETWORK_GROUP_NAME_WIDTH + DEVICE_ID_WIDTH;
    auto rest_line_len = LINE_LENGTH - data_line_len;

    for (const auto &device_info : mon_message.device_infos()) {
        auto device_id = device_info.device_id();
        auto utilization = device_info.utilization();
        auto device_arch = device_info.device_arch();

        buffer << std::setprecision(1) << std::fixed <<
            std::setw(DEVICE_ID_WIDTH) << std::left << device_id <<
            std::setw(UTILIZATION_WIDTH) << std::left << utilization <<
            std::setw(STRING_WIDTH) << std::left << device_arch <<
            std::string(rest_line_len, ' ') << "\n";
    }
}

void MonCommand::add_networks_info_header(std::ostream &buffer)
{
    buffer <<
        std::setw(NETWORK_GROUP_NAME_WIDTH) << std::left << "Model" <<
        std::setw(UTILIZATION_WIDTH) << std::left << "Utilization (%) " <<
        std::setw(NUMBER_WIDTH) << std::left << "FPS" <<
        std::setw(NUMBER_WIDTH) << std::left << "PID" << 
        "\n" << std::left << std::string(LINE_LENGTH, '-') << "\n";
}

void MonCommand::add_networks_info_table(const ProtoMon &mon_message, std::ostream &buffer)
{
    const uint32_t NUMBER_OBJECTS_COUNT = 3;
    auto data_line_len = (NUMBER_WIDTH * NUMBER_OBJECTS_COUNT) + NETWORK_GROUP_NAME_WIDTH;
    auto rest_line_len = LINE_LENGTH - data_line_len;

    const std::string &pid = mon_message.pid();
    for (const auto &net_info : mon_message.networks_infos()) {
        auto &original_net_name = net_info.network_name();
        auto net_name = truncate_str(original_net_name, NETWORK_GROUP_NAME_WIDTH);
        auto fps = net_info.fps();
        auto utilization = net_info.utilization();

        buffer << std::setprecision(1) << std::fixed <<
            std::setw(STRING_WIDTH) << std::left << net_name <<
            std::setw(UTILIZATION_WIDTH) << std::left << utilization <<
            std::setw(NUMBER_WIDTH) << std::left << fps <<
            std::setw(NUMBER_WIDTH) << std::left << pid << std::string(rest_line_len, ' ') << "\n";
    }
}

void MonCommand::add_frames_header(std::ostream &buffer)
{
    buffer <<
        std::setw(STRING_WIDTH) << std::left << "Model" <<
        std::setw(STRING_WIDTH) << std::left << "Stream" <<
        std::setw(NUMBER_WIDTH) << std::left << "Direction" <<
        std::setw(3 * FRAME_VALUE_WIDTH - 2) << std::internal << "Frames Queue" <<
        "\n" <<
        std::setw(STRING_WIDTH) << std::left << "" <<
        std::setw(STRING_WIDTH) << std::left << "" <<
        std::setw(NUMBER_WIDTH) << std::left << "" <<
        std::setw(FRAME_VALUE_WIDTH) << "Avg" <<
        std::setw(FRAME_VALUE_WIDTH) << "Max" <<
        std::setw(FRAME_VALUE_WIDTH) << "Min" <<
        std::setw(FRAME_VALUE_WIDTH) << "Capacity" <<
        "\n" << std::left << std::string(LINE_LENGTH + NUMBER_WIDTH, '-') << "\n";
}

hailo_status MonCommand::print_frames_table(const ProtoMon &mon_message, std::ostream &buffer)
{
    for (const auto &net_info : mon_message.net_frames_infos()) {
        auto &original_net_name = net_info.network_name();
        auto net_name = truncate_str(original_net_name, NETWORK_GROUP_NAME_WIDTH);
        for (const auto &streams_frames : net_info.streams_frames_infos()) {
            auto &stream_name_original = streams_frames.stream_name();
            auto stream_name = truncate_str(stream_name_original, STREAM_NAME_WIDTH);
            auto stream_direction = (streams_frames.stream_direction() == PROTO__STREAM_DIRECTION__HOST_TO_DEVICE) ? "H2D" : "D2H";

            std::string max_frames, min_frames, queue_size;
            double avg_frames;
            if (SCHEDULER_MON_NAN_VAL == streams_frames.buffer_frames_size() || SCHEDULER_MON_NAN_VAL == streams_frames.pending_frames_count()) {
                avg_frames = -1;
                max_frames = "NaN";
                min_frames = "NaN";
                queue_size = "NaN";
            } else {
                avg_frames = streams_frames.avg_pending_frames_count();
                max_frames = std::to_string(streams_frames.max_pending_frames_count());
                min_frames = std::to_string(streams_frames.min_pending_frames_count());
                queue_size = std::to_string(streams_frames.buffer_frames_size());
            }

            std::string avg_frames_str;
            if (avg_frames == -1) {
                avg_frames_str = "NaN";
            } else {
                std::stringstream ss;
                ss << std::fixed << std::setprecision(2) << avg_frames;
                avg_frames_str = ss.str();
            }

            buffer <<
                std::setw(STRING_WIDTH) << std::left << net_name <<
                std::setw(STRING_WIDTH) << std::left << stream_name <<
                std::setw(NUMBER_WIDTH) << std::left << stream_direction <<
                std::setw(FRAME_VALUE_WIDTH) << std::left << avg_frames_str <<
                std::setw(FRAME_VALUE_WIDTH) << std::left << max_frames <<
                std::setw(FRAME_VALUE_WIDTH) << std::left << min_frames <<
                std::setw(FRAME_VALUE_WIDTH) << std::left << queue_size << "\n";
        }
    }
    return HAILO_SUCCESS;
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

hailo_status MonCommand::print_tables(const std::vector<ProtoMon> &mon_messages, uint32_t terminal_line_width)
{
    std::ostringstream buffer;
    buffer.str("");  // Clear previous content
    buffer.clear();  // Reset any error state

    buffer << FORMAT_RESET_TERMINAL_CURSOR_FIRST_LINE;

    add_devices_info_header(buffer);
    for (const auto &mon_message : mon_messages) {
        add_devices_info_table(mon_message, buffer);
    }

    buffer << std::string(terminal_line_width, ' ') << "\n";
    buffer << std::string(terminal_line_width, ' ') << "\n";

    add_networks_info_header(buffer);

    for (const auto &mon_message : mon_messages) {
        add_networks_info_table(mon_message, buffer);
    }

    buffer << std::string(terminal_line_width, ' ') << "\n";
    buffer << std::string(terminal_line_width, ' ') << "\n";

    add_frames_header(buffer);
    for (const auto &mon_message : mon_messages) {
        CHECK_SUCCESS(print_frames_table(mon_message, buffer));
    }

    std::cout << buffer.str() << std::flush;
    return HAILO_SUCCESS;
}

static volatile bool keep_running = true;
void signit_handler(int /*dummy*/)
{
    keep_running = false;
}

hailo_status MonCommand::run_monitor()
{
    // Note: There is no need to unregister to previous SIGINT handler since we finish running after it is called.
    signal(SIGINT, signit_handler);

    std::chrono::milliseconds time_interval = DEFAULT_SCHEDULER_MON_INTERVAL + EPSILON_TIME;
    TRY(const auto terminal_line_width, get_terminal_line_width());

    AlternativeTerminal alt_terminal;
    while (keep_running) {
        bool print_warning_msg = true; // Will change to false only if mon directory is valid and there are updated files in it.
        TRY(const auto mon_dir_valid, Filesystem::is_directory(SCHEDULER_MON_TMP_DIR));

        std::vector<ProtoMon> mon_messages;
        if (mon_dir_valid) {
            TRY(auto scheduler_mon_files_with_tmp, Filesystem::get_latest_files_in_dir_flat(SCHEDULER_MON_TMP_DIR, time_interval));

            // Filter out .tmp files - these files are created for temporary use and should not be considered
            std::vector<std::string> scheduler_mon_files;
            std::copy_if(scheduler_mon_files_with_tmp.begin(), scheduler_mon_files_with_tmp.end(), std::back_inserter(scheduler_mon_files),
                [](const std::string &file) {
                    return !Filesystem::has_suffix(file, ".tmp");
                });

            print_warning_msg = scheduler_mon_files.empty();

            mon_messages.reserve(scheduler_mon_files.size());
            for (const auto &mon_file : scheduler_mon_files) {
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
        }

        CHECK_SUCCESS(print_tables(mon_messages, terminal_line_width));
        if (print_warning_msg) {
            std::cout << FORMAT_GREEN_PRINT << "Monitor did not retrieve any files. This occurs when there is no application currently running.\n"
            << "If this is not the case, verify that environment variable '" << SCHEDULER_MON_ENV_VAR << "' is set to 1.\n" << FORMAT_NORMAL_PRINT;
        }

        std::this_thread::sleep_for(DEFAULT_SCHEDULER_MON_INTERVAL);
    }

    return HAILO_SUCCESS;
}
#endif

} /* namespace hailort */

