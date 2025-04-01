/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file query_stats_utils.cpp
 * @brief QueryStatsUtils is a class for querying the system for performance and health information.
 **/

#ifndef _HAILO_QUERY_STATS_UTILS_HPP_
#define _HAILO_QUERY_STATS_UTILS_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "common/utils.hpp"

#include <tuple>

namespace hailort {

typedef struct {
    double time;
    int counter0;
    int counter1;
    int counter2;
} ddr_noc_row_data_t;

class QueryStatsUtils
{
public:
    static Expected<float32_t> calculate_cpu_utilization();
    static Expected<std::tuple<int64_t, int64_t>> calculate_ram_sizes();
    static Expected<int32_t> get_dsp_utilization();
    static Expected<float32_t> get_nnc_utilization(const std::string &id_info_str, const std::string &device_arch_str);
    static Expected<int32_t> get_ddr_noc_utilization();

private:
    static hailo_status parse_cpu_stats(uint64_t &user, uint64_t &nice, uint64_t &system, uint64_t &idle,
        uint64_t &iowait, uint64_t &irq, uint64_t &softirq, uint64_t &steal);
    static  Expected<std::vector<ddr_noc_row_data_t>> read_ddr_noc_output_file(const std::string &filename);
    static  int32_t calculate_ddr_noc_data_per_second(const std::vector<ddr_noc_row_data_t> &data, int ddr_noc_row_data_t::*member,
        const float32_t duration);
    static hailo_status execute_noc_command(const std::string &command);
    static Expected<std::pair<int32_t, std::string>> run_command(const std::string &cmd);
    static Expected<std::istringstream> read_nnc_utilization_file();
    static std::string get_sampling_time_window_as_string();
};

} /* namespace hailort */

#endif /* _HAILO_QUERY_STATS_UTILS_HPP_ */