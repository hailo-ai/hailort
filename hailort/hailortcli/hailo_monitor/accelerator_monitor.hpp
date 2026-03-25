/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file accelerator_monitor.hpp
 * @brief Accelerator device stats monitor for H10 devices
 **/

#ifndef _HAILO_ACCELERATOR_MONITOR_HPP_
#define _HAILO_ACCELERATOR_MONITOR_HPP_

#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "hailo/device.hpp"

#include <vector>
#include <memory>
#include <ostream>

namespace hailort
{

class AcceleratorMonitor
{
public:
    static hailo_status run();

private:
    static std::vector<std::unique_ptr<Device>> scan_h10_devices();

    struct MonitorEntry {
        std::string device_id;
        std::string architecture;
        // Performance
        float32_t nnc_utilization;
        float32_t cpu_utilization;
        float32_t ram_utilization;
        int64_t ram_total;
        int64_t ram_used;
        // Health
        float32_t on_die_temperature;
        int32_t on_die_voltage;
    };

    static hailo_status print_table(const std::vector<MonitorEntry> &entries);
    static void add_table_header(std::ostream &buffer);
    static void add_table_row(const MonitorEntry &entry, std::ostream &buffer);
};

} /* namespace hailort */

#endif /* _HAILO_ACCELERATOR_MONITOR_HPP_ */
