/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
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
#include "utils/profiler/monitor_handler.hpp"
#include "common/env_vars.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <inttypes.h>
#include <regex>
#include <cstdint>

namespace hailort {

#define CPU_INFO_PATH ("/proc/stat")
#define MAX_COMMAND_OUTPUT_LENGTH (4096)
#define HAILO_NOC_PERF_FILE_PATH "/etc/hailo_noc_perf.sh"
#define HAILO_NOC_MEASURE_OUTPUT_FILE_PATH "/tmp/noc_measure_output.txt"
#define HAILO_BIST_FAILURE_MASK_FILE_PATH "/sys/devices/soc0/mbist_status"
#define HAILO_ON_DIE_VOLTAGE_FILE_PATH "/sys/class/hwmon/hwmon0/in0_input"
#define HAILO_NNC_VDMA_MONITOR_PATH "/sys/class/hailo1x_integrated/h1x/vdma_monitor"


float32_t QueryStatsUtils::get_cpu_utilization(const CpuStatsSample &prev_sample, const CpuStatsSample &current_sample)
{
    uint64_t total_delta = current_sample.total() - prev_sample.total();
    if (0 == total_delta) {
        return 0;
    }

    uint64_t idle_delta = current_sample.idle_total() - prev_sample.idle_total();

    // Calculate utilization percentage
    return (static_cast<float32_t>(total_delta - idle_delta) /
            static_cast<float32_t>(total_delta)) * 100.0f;
}

// Function parses the first line of /proc/stat
Expected<CpuStatsSample> QueryStatsUtils::get_cpu_stats()
{
    std::ifstream procStat(CPU_INFO_PATH);
    CHECK(procStat.is_open(), HAILO_OPEN_FILE_FAILURE, "Unable to open {}", CPU_INFO_PATH);

    std::string line;
    char cpu_label[16] = {0};
    CpuStatsSample sample;

    getline(procStat, line); // Read the first line (starts with "cpu")
    int matches = sscanf(line.c_str(),
        "%s %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64,
        cpu_label, &sample.user, &sample.nice, &sample.system, &sample.idle, &sample.iowait, &sample.irq, &sample.softirq,
        &sample.steal);

    procStat.close();
    constexpr int EXPECTED_MATCHES = 9; // 8 values + cpu label
    CHECK((EXPECTED_MATCHES == matches) && ("cpu" == std::string(cpu_label).substr(0, 3)), HAILO_INTERNAL_FAILURE,
          "Failed to parse CPU stats from {}", CPU_INFO_PATH);

    return sample;
}

Expected<std::tuple<int64_t, int64_t>> QueryStatsUtils::calculate_ram_sizes()
{
    int64_t total_ram = -1;
    int64_t used_ram = -1;
    TRY(const auto output, Process::create_and_wait_for_output("free", MAX_COMMAND_OUTPUT_LENGTH));

    std::istringstream stream(output.second);

    std::string label;
    long long total, used, freeMem, shared, buffCache, available;
    // Parse the output, searching for the line that starts with "Mem:"
    while (stream >> label) {
        if (label == "Mem:") {
            if (stream >> total >> used >> freeMem >> shared >> buffCache >> available) {
                total_ram = static_cast<int64_t>(total);
                used_ram = total_ram - static_cast<int64_t>(available);
            }
            break;
        }
    }

    CHECK((-1 != total_ram) && (-1 != used_ram), HAILO_INTERNAL_FAILURE,
          "Failed to parse RAM stats from 'free' command. Total RAM: {}, Used RAM: {}", total_ram, used_ram);

    return std::make_tuple(total_ram, used_ram);
}

std::string QueryStatsUtils::get_sampling_time_window_as_string(std::chrono::milliseconds sampling_period)
{
    std::ostringstream oss;
    oss.precision(1);  // Set precision to 1 decimal place
    oss << std::fixed << std::chrono::duration<double>(sampling_period).count();
    return oss.str();
}

Expected<int32_t> QueryStatsUtils::get_dsp_utilization(std::chrono::milliseconds sampling_period)
{
    std::string delay_str = get_sampling_time_window_as_string(sampling_period);
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

    const std::string command_with_source = std::string(". ") + HAILO_NOC_PERF_FILE_PATH + " && " + "cd /tmp && " + command;
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

Expected<int32_t> QueryStatsUtils::get_ddr_noc_utilization(std::chrono::milliseconds sampling_period)
{
    std::string delay_str = get_sampling_time_window_as_string(sampling_period);

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

Expected<NncUtilSample> QueryStatsUtils::get_current_nnc_utilization()
{
    std::ifstream file(HAILO_NNC_VDMA_MONITOR_PATH);
    CHECK(file.is_open(), HAILO_OPEN_FILE_FAILURE, "Unable to open {}", HAILO_NNC_VDMA_MONITOR_PATH);

    std::string header_line;
    std::getline(file, header_line); // Skip the "INUSE\tTOTAL" header

    uint64_t in_use = 0;
    uint64_t total = 0;
    file >> in_use >> total;
    CHECK(file.good() || file.eof(), HAILO_FILE_OPERATION_FAILURE,
          "Failed to parse vdma_monitor data from {}", HAILO_NNC_VDMA_MONITOR_PATH);

    return NncUtilSample{in_use, total};
}

float32_t QueryStatsUtils::get_nnc_utilization(const NncUtilSample &prev_sample, const NncUtilSample &current_sample)
{
    if ((prev_sample.total > current_sample.total) || (prev_sample.in_use > current_sample.in_use)) {
        LOGGER__ERROR("Invalid NNC util values, status = {}", HAILO_INTERNAL_FAILURE);
        return -1.0f;
    }

    if (current_sample.total == prev_sample.total) {
        return 0.0f;
    }

    uint64_t total_delta = current_sample.total - prev_sample.total;
    uint64_t in_use_delta = current_sample.in_use - prev_sample.in_use;
    return (static_cast<float32_t>(in_use_delta) / static_cast<float32_t>(total_delta)) * 100.0f;
}

void PerformanceStatsMeasurement::start_measurement(const std::string &device_id, const std::string &device_arch)
{
    TRACE(MonitorStartTrace, "");
    TRACE(AddDeviceTrace, device_id, device_arch);
    m_start_time = std::chrono::steady_clock::now();
    auto cpu_sample = QueryStatsUtils::get_cpu_stats();
    if (cpu_sample) {
        m_cpu_sample = cpu_sample.release();
        m_is_cpu_sample_valid = true;
    }

    auto nnc_util_sample = QueryStatsUtils::get_current_nnc_utilization();
    if (nnc_util_sample) {
        m_nnc_util_sample = nnc_util_sample.release();
        m_is_nnc_util_sample_valid = true;
    }
}

std::chrono::milliseconds PerformanceStatsMeasurement::get_remaining_sleep_time(std::chrono::milliseconds sampling_period)
{
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_start_time);
    if (elapsed < sampling_period) {
        return sampling_period - elapsed;
    }
    return std::chrono::milliseconds(0);
}

hailo_performance_stats_t PerformanceStatsMeasurement::end_measurement()
{
    if (m_is_cpu_sample_valid) {
        auto second_cpu_sample = QueryStatsUtils::get_cpu_stats();
        if (second_cpu_sample) {
            m_performance_stats.cpu_utilization = QueryStatsUtils::get_cpu_utilization(m_cpu_sample, second_cpu_sample.value());
        }
    }

    if (m_is_nnc_util_sample_valid) {
        auto second_nnc_util_sample = QueryStatsUtils::get_current_nnc_utilization();
        if (second_nnc_util_sample) {
            m_performance_stats.nnc_utilization = QueryStatsUtils::get_nnc_utilization(m_nnc_util_sample, second_nnc_util_sample.value());
        }
    }

    auto ram_sizes = QueryStatsUtils::calculate_ram_sizes();
    if (ram_sizes) {
        m_performance_stats.ram_size_total = std::get<0>(ram_sizes.value());
        m_performance_stats.ram_size_used = std::get<1>(ram_sizes.value());
    }

    // TODO HRT-16545: Enable when this function will run faster
    // auto ddr_noc_utilization = QueryStatsUtils::get_ddr_noc_utilization(sampling_period);
    // if (HAILO_SUCCESS == ddr_noc_utilization.status()) {
    //     performance_stats.ddr_noc_total_transactions = ddr_noc_utilization.release();
    // }

    TRACE(DumpProfilerStateTrace);

    return m_performance_stats;
}

hailo_performance_stats_t PerformanceStatsMeasurement::measure(std::chrono::milliseconds sampling_period,
    const std::string &device_id, const std::string &device_arch)
{
    // TODO: Print errors (only once so the log won't be spammed) in case of an error (HRT-20218s)
    PerformanceStatsMeasurement measurement;
    measurement.start_measurement(device_id, device_arch);

    auto remaining_sleep = measurement.get_remaining_sleep_time(sampling_period);
    if (remaining_sleep.count() > 0) {
        std::this_thread::sleep_for(remaining_sleep);
    }

    return measurement.end_measurement();
}

} /* namespace hailort */
