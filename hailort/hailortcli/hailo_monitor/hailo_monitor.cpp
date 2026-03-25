/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailo_monitor.cpp
 * @brief Hailo monitor that detects device type and dispatches to the appropriate monitor
 **/

#include "hailo_monitor.hpp"
#include "integrated_monitor.hpp"
#include "accelerator_monitor.hpp"

namespace hailort
{

bool HailoMonitor::is_integrated()
{
    auto device = Device::create();
    if (HAILO_SUCCESS != device.status()) {
        return false;
    }

    auto type = device.value()->get_type();
    return (Device::Type::INTEGRATED == type);
}

hailo_status HailoMonitor::run(bool verbose)
{
    if (is_integrated()) {
        return IntegratedMonitor::run(verbose);
    }

    return AcceleratorMonitor::run();
}

} /* namespace hailort */
