/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file query_stats_utils.cpp
 * @brief Stats query utils module implementation
 **/

#include "hailo/hailort.h"
#include "common/logger_macros.hpp"
#include "common/process.hpp"
#include "common/filesystem.hpp"
#include "utils/query_stats_utils.hpp"
#include "utils/profiler/tracer_macros.hpp"
#include "common/env_vars.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <inttypes.h>
#include <regex>
#include <cstdint>

namespace hailort {

// Platform-specific macros for popen and pclose
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

#define MEM_INFO_PATH ("/proc/meminfo")
#define CPU_INFO_PATH ("/proc/stat")
#define PERFORMANCE_QUERY_SAMPLING_TIME_WINDOW (std::chrono::milliseconds(100))
#define EPSILON_TIME (std::chrono::milliseconds(1))
#define MAX_COMMAND_OUTPUT_LENGTH (4096)
#define HAILO_NOC_PERF_FILE_PATH "/etc/hailo_noc_perf.sh"
#define HAILO_NOC_MEASURE_OUTPUT_FILE_PATH "/tmp/noc_measure_output.txt"
#define HAILO_BIST_FAILURE_MASK_FILE_PATH "/sys/devices/soc0/mbist_status"
#define HAILO_ON_DIE_VOLTAGE_FILE_PATH "/sys/class/hwmon/hwmon0/in0_input"


Expected<float32_t> QueryStatsUtils::calculate_cpu_utilization()
{
    // First sample
    uint64_t user1, nice1, system1, idle1, iowait1, irq1, softirq1, steal1;
    auto status = parse_cpu_stats(user1, nice1, system1, idle1, iowait1, irq1, softirq1, steal1);
    CHECK_SUCCESS_AS_EXPECTED(status);

    std::this_thread::sleep_for(PERFORMANCE_QUERY_SAMPLING_TIME_WINDOW);

    // Second sample
    uint64_t user2, nice2, system2, idle2, iowait2, irq2, softirq2, steal2;
    status = parse_cpu_stats(user2, nice2, system2, idle2, iowait2, irq2, softirq2, steal2);
    CHECK_SUCCESS_AS_EXPECTED(status);

    // Calculate deltas
    uint64_t total1 = user1 + nice1 + system1 + idle1 + iowait1 + irq1 + softirq1 + steal1;
    uint64_t total2 = user2 + nice2 + system2 + idle2 + iowait2 + irq2 + softirq2 + steal2;
    uint64_t totalDelta = total2 - total1;

    uint64_t idleDelta = (idle2 + iowait2) - (idle1 + iowait1);

    // Calculate utilization percentage
    float32_t utilization = (static_cast<float32_t>(totalDelta - idleDelta) / static_cast<float32_t>(totalDelta)) * static_cast<float32_t>(100.0);
    return utilization;
}


// Function parses the first line of /proc/stat
hailo_status QueryStatsUtils::parse_cpu_stats(uint64_t &user, uint64_t &nice, uint64_t &system, uint64_t &idle,
    uint64_t &iowait, uint64_t &irq, uint64_t &softirq, uint64_t &steal)
{
    std::ifstream procStat(CPU_INFO_PATH);
    CHECK(procStat.is_open(), HAILO_OPEN_FILE_FAILURE, "Unable to open {}", CPU_INFO_PATH);

    std::string line;
    char cpu_label[16];

    getline(procStat, line); // Read the first line (starts with "cpu")
    int matches = sscanf(line.c_str(),
        "%s %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64,
        cpu_label, &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);

    procStat.close();
    constexpr int EXPECTED_MATCHES = 9; // 8 values + cpu label
    CHECK((EXPECTED_MATCHES == matches) && ("cpu" == std::string(cpu_label).substr(0, 3)), HAILO_INTERNAL_FAILURE,
          "Failed to parse CPU stats from {}", CPU_INFO_PATH);

    return HAILO_SUCCESS;
}

Expected<std::tuple<int64_t, int64_t>> QueryStatsUtils::calculate_ram_sizes()
{
    // function is based on Linux 'free' command
    int64_t total_ram = -1;
    int64_t used_ram = -1;
    TRY(const auto output, Process::create_and_wait_for_output("free -b", MAX_COMMAND_OUTPUT_LENGTH));

    std::istringstream stream(output.second);

    std::string label;
    long long total, used, freeMem, shared, buffCache, available;
    // Parse the output, searching for the line that starts with "Mem:"
    while (stream >> label) {
        if (label == "Mem:") {
            if (stream >> total >> used >> freeMem >> shared >> buffCache >> available) {
                total_ram = static_cast<int64_t>(total);
                used_ram = static_cast<int64_t>(used);
            }
            break;
        }
    }

    CHECK((-1 != total_ram) && (-1 != used_ram), HAILO_INTERNAL_FAILURE,
          "Failed to parse RAM stats from 'free' command. Total RAM: {}, Used RAM: {}", total_ram, used_ram);

    return std::make_tuple(total_ram, used_ram);
}

std::string QueryStatsUtils::get_sampling_time_window_as_string()
{
    std::ostringstream oss;
    oss.precision(1);  // Set precision to 1 decimal place
    oss << std::fixed << std::chrono::duration<double>(PERFORMANCE_QUERY_SAMPLING_TIME_WINDOW).count();
    return oss.str();
}

Expected<int32_t> QueryStatsUtils::get_dsp_utilization()
{
    std::string delay_str = get_sampling_time_window_as_string();

    const std::string dsp_utilization_command = "dsp-utilization -i 1 -b --delay " + delay_str;

    TRY(const auto output, Process::create_and_wait_for_output(dsp_utilization_command, MAX_COMMAND_OUTPUT_LENGTH));

    // Use regex to extract the percentage value (e.g., %15)
    std::regex percentage_regex(R"((\d+)%)");
    std::smatch match;

    CHECK(regex_search(output.second, match, percentage_regex), HAILO_INTERNAL_FAILURE,
          "No percentage found in output of '{}' command", dsp_utilization_command);
    return stoi(match[1]);
}

Expected<std::vector<ddr_noc_row_data_t>> QueryStatsUtils::read_ddr_noc_output_file(const std::string &filename)
{
    LOGGER__INFO("Opening ddr_noc file output in path: {}", filename);
    std::ifstream file(filename);
    std::vector<ddr_noc_row_data_t> data;

    CHECK(file.good(), HAILO_OPEN_FILE_FAILURE, "Error opening file {}", filename);

    std::string line;
    bool headerSkipped = false;

    while (std::getline(file, line)) {
        if (!headerSkipped) {
            headerSkipped = true; // Skip the header line
            continue;
        }

        std::istringstream ss(line);
        ddr_noc_row_data_t row;
        int index;
        std::string note;

        ss >> index >> row.time >> row.counter0 >> row.counter1 >> row.counter2;
        data.push_back(row);
    }

    file.close();
    return data;
}

int32_t QueryStatsUtils::calculate_ddr_noc_data_per_second(const std::vector<ddr_noc_row_data_t> &data, int ddr_noc_row_data_t::*member,
    const float32_t duration)
{
    double sum = 0.0;
    for (const auto &row : data) {
        sum += row.*member;
    }
    return static_cast<int32_t>(data.empty() ? 0.0 : sum / static_cast<double>(duration));
}

hailo_status QueryStatsUtils::execute_noc_command(const std::string &command)
{
    CHECK(Filesystem::does_file_exists(HAILO_NOC_PERF_FILE_PATH), HAILO_FILE_OPERATION_FAILURE,
          "File {} does not exist", HAILO_NOC_PERF_FILE_PATH);

    const std::string command_with_source = std::string(". ") + HAILO_NOC_PERF_FILE_PATH + " && " + command;
    LOGGER__INFO("Run the following DDR NOC command: {}", command_with_source);

    auto ret_val = system(command_with_source.c_str());
    CHECK((ret_val == 0), HAILO_INTERNAL_FAILURE, "Failed to execute DDR NOC command: {}", command_with_source);

    return HAILO_SUCCESS;
}

Expected<uint32_t> QueryStatsUtils::get_on_die_voltage()
{
    return read_single_data_from_file<uint32_t>(HAILO_ON_DIE_VOLTAGE_FILE_PATH, true);
}

Expected<uint32_t> QueryStatsUtils::get_bist_failure_mask()
{
    return read_single_data_from_file<uint32_t>(HAILO_BIST_FAILURE_MASK_FILE_PATH, false);
}

Expected<int32_t> QueryStatsUtils::get_ddr_noc_utilization()
{
    std::string delay_str = get_sampling_time_window_as_string();

    if (Filesystem::does_file_exists(HAILO_NOC_MEASURE_OUTPUT_FILE_PATH)) {
        std::remove(HAILO_NOC_MEASURE_OUTPUT_FILE_PATH);
    }

    auto status = execute_noc_command("noc_set_counter_total 0");
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = execute_noc_command("noc_measure_sleep " + delay_str + " 50 0 " + HAILO_NOC_MEASURE_OUTPUT_FILE_PATH);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto data = read_ddr_noc_output_file(HAILO_NOC_MEASURE_OUTPUT_FILE_PATH);
    CHECK_EXPECTED(data);

    CHECK(!data.value().empty(), HAILO_INTERNAL_FAILURE,
          "No data available to process to get ddr_noc_utilization from file: {}", HAILO_NOC_MEASURE_OUTPUT_FILE_PATH);

    int32_t total_transactions = calculate_ddr_noc_data_per_second(data.value(), &ddr_noc_row_data_t::counter0, std::stof(delay_str));

    return total_transactions;
}

Expected<float32_t> QueryStatsUtils::get_nnc_utilization(const std::string &id_info_str, const std::string &device_arch_str)
{
    TRACE(MonitorStartTrace, "");
    TRACE(AddDeviceTrace, id_info_str, device_arch_str);

    std::this_thread::sleep_for(PERFORMANCE_QUERY_SAMPLING_TIME_WINDOW + EPSILON_TIME);

    TRACE(DumpProfilerStateTrace);

    const uint32_t MAX_RETRIES = 5;
    uint32_t retry = 0;
    float32_t utilization = 0.0f;

    while (retry < MAX_RETRIES) {
        // Getting only files changed recently, to avoid reading old files
        TRY(auto nnc_utilization_file_paths, Filesystem::get_latest_files_in_dir_flat(
            NNC_UTILIZATION_TMP_DIR, PERFORMANCE_QUERY_SAMPLING_TIME_WINDOW));
        if (!nnc_utilization_file_paths.empty()) {
            auto utilization_exp = read_single_data_from_file<float32_t>(nnc_utilization_file_paths[0], true);
            if (HAILO_SUCCESS == utilization_exp.status()) {
                utilization = utilization_exp.release();
                break;
            }
        }

        LOGGER__WARNING("No data available to process to get nnc_utilization, retrying... (attempt {}/{})", retry + 1, MAX_RETRIES);
        retry++;
        std::this_thread::sleep_for(PERFORMANCE_QUERY_SAMPLING_TIME_WINDOW);
    }

    TRACE(MonitorEndTrace, "", id_info_str);

    CHECK((retry < MAX_RETRIES), HAILO_INTERNAL_FAILURE, "Failed to get nnc_utilization after {} retries", MAX_RETRIES);

    return utilization;
}

} /* namespace hailort */
