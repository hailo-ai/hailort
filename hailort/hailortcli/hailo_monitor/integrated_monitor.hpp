/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file integrated_monitor.hpp
 * @brief Integrated device network monitor for H15 devices
 **/

#ifndef _HAILO_INTEGRATED_MONITOR_HPP_
#define _HAILO_INTEGRATED_MONITOR_HPP_

#include "hailo/hailort.h"

#include "utils/profiler/monitor_handler.hpp"
#include "common/runtime_statistics_internal.hpp"

#include <vector>
#include <ostream>

namespace hailort
{

class IntegratedMonitor
{
public:
    static hailo_status run(bool verbose);

private:
    static hailo_status print_tables(const std::vector<ProtoMon> &mon_messages, bool verbose);
    static void add_devices_info_header(std::ostream &buffer);
    static void add_networks_info_header(std::ostream &buffer);
    static void add_frames_header(std::ostream &buffer);
    static void add_devices_info_table(const ProtoMon &mon_message, std::ostream &buffer);
    static void add_networks_info_table(const ProtoMon &mon_message, std::ostream &buffer);
    static hailo_status print_frames_table(const ProtoMon &mon_message, std::ostream &buffer);
};

} /* namespace hailort */

#endif /* _HAILO_INTEGRATED_MONITOR_HPP_ */
