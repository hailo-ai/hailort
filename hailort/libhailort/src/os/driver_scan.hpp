/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file driver_scan.hpp
 * @brief Get list and parse pcie driver info
 **/

#include "os/hailort_driver.hpp"

namespace hailort
{

Expected<std::vector<std::string>> list_devices();
#ifndef __QNX__
Expected<HailoRTDriver::DeviceInfo> query_device_info(const std::string &device_name);
#else // __QNX__
Expected<HailoRTDriver::DeviceInfo> query_device_info(const std::string &device_name, uint32_t index);
#endif // __QNX__

} /* namespace hailort */
