/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pcie_driver_sysfs.hpp
 * @brief Parse pcie driver sysfs
 **/

#include "os/hailort_driver.hpp"

namespace hailort
{

Expected<std::vector<std::string>> list_sysfs_pcie_devices();
Expected<HailoRTDriver::DeviceInfo> query_device_info(const std::string &device_name);

} /* namespace hailort */
