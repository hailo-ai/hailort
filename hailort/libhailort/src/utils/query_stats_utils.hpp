/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file query_stats_utils.hpp
 * @brief QueryStatsUtils is a class for querying the system for performance and health information.
 **/

#ifndef _HAILO_QUERY_STATS_UTILS_HPP_
#define _HAILO_QUERY_STATS_UTILS_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "common/utils.hpp"

#include <chrono>
#include <tuple>

namespace hailort {

typedef struct {
    double time;
    int counter0;
    int counter1;
    int counter2;
} ddr_noc_row_data_t;

// CPU stats sample from /proc/stat
struct CpuStatsSample
{
    uint64_t user = 0;
    uint64_t nice = 0;
    uint64_t system = 0;
    uint64_t idle = 0;
    uint64_t iowait = 0;
    uint64_t irq = 0;
    uint64_t softirq = 0;
    uint64_t steal = 0;

    uint64_t total() const {
        return user + nice + system + idle + iowait + irq + softirq + steal;
    }

    uint64_t idle_total() const {
        return idle + iowait;
    }
};

struct NncUtilSample
{
    uint64_t in_use = 0;
    uint64_t total = 0;
};

class QueryStatsUtils
{
public:
    static Expected<CpuStatsSample> get_cpu_stats();
    static float32_t get_cpu_utilization(const CpuStatsSample &prev_sample, const CpuStatsSample &current_sample);
    static Expected<std::tuple<int64_t, int64_t>> calculate_ram_sizes();
    static Expected<int32_t> get_dsp_utilization(std::chrono::milliseconds sampling_period);
    static Expected<int32_t> get_ddr_noc_utilization(std::chrono::milliseconds sampling_period);
    static Expected<uint32_t> get_on_die_voltage();
    static Expected<uint32_t> get_bist_failure_mask();
    static Expected<NncUtilSample> get_current_nnc_utilization();
    static float32_t get_nnc_utilization(const NncUtilSample &prev_sample, const NncUtilSample &current_sample);

private:
    static Expected<std::vector<ddr_noc_row_data_t>> read_ddr_noc_output_file(const std::string &filename);
    static int32_t calculate_ddr_noc_data_per_second(const std::vector<ddr_noc_row_data_t> &data, int ddr_noc_row_data_t::*member,
        const float32_t duration);
    static hailo_status execute_noc_command(const std::string &command);
    static std::string get_sampling_time_window_as_string(std::chrono::milliseconds sampling_period);

    template<typename T>
    static Expected<T> read_single_data_from_file(const std::string &file_path, bool read_as_text)
    {
        std::ifstream file(file_path, std::ios::in | std::ios::binary);
        CHECK(file.good(), HAILO_OPEN_FILE_FAILURE, "Error opening file {}", file_path);

        T value;
        if (read_as_text) {
            file >> value;
        } else {
            file.read(reinterpret_cast<char*>(&value), sizeof(T));
        }
        CHECK(file.good(), HAILO_FILE_OPERATION_FAILURE, "Failed reading file {}, errno: {}", file_path, errno);

        return value;
    }
};

class PerformanceStatsMeasurement final
{
public:
    static hailo_performance_stats_t measure(std::chrono::milliseconds sampling_period, const std::string &device_id,
        const std::string &device_arch);

    PerformanceStatsMeasurement() :
        m_performance_stats({-1, -1, -1, -1, -1, -1}),
        m_is_cpu_sample_valid(false),
        m_is_nnc_util_sample_valid(false)
    {}

private:
    void start_measurement(const std::string &device_id, const std::string &device_arch);
    std::chrono::milliseconds get_remaining_sleep_time(std::chrono::milliseconds sampling_period);
    hailo_performance_stats_t end_measurement();

    hailo_performance_stats_t m_performance_stats;
    std::chrono::steady_clock::time_point m_start_time;
    CpuStatsSample m_cpu_sample;
    bool m_is_cpu_sample_valid;
    NncUtilSample m_nnc_util_sample;
    bool m_is_nnc_util_sample_valid;
};

} /* namespace hailort */

#endif /* _HAILO_QUERY_STATS_UTILS_HPP_ */