/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailo_monitor.hpp
 * @brief Hailo monitor that detects device type and dispatches to the appropriate monitor
 **/

#ifndef _HAILO_HAILO_MONITOR_HPP_
#define _HAILO_HAILO_MONITOR_HPP_

#include "hailo/hailort.h"
#include "hailo/device.hpp"

#include <vector>
#include <memory>

namespace hailort
{

class HailoMonitor
{
public:
    static hailo_status run(bool verbose);

private:
    static bool is_integrated();
};

} /* namespace hailort */

#endif /* _HAILO_HAILO_MONITOR_HPP_ */
