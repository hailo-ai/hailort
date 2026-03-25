/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pcie_utils.hpp
 * @brief Declaration of PCIe utils
 **/

 #ifndef _HAILO_PCIE_UTILS_HPP_
 #define _HAILO_PCIE_UTILS_HPP_
 
 #include "hailo/hailort.h"
 #include "hailo/expected.hpp"
 
 #include <vector>
 #include <string>
 
 
 namespace hailort
 {

class PcieUtils {
public:
    static Expected<std::vector<hailo_pcie_device_info_t>> scan();
    static Expected<hailo_pcie_device_info_t> parse_pcie_device_info(const std::string &device_info_str);
    static Expected<std::string> pcie_device_info_to_string(const hailo_pcie_device_info_t &device_info);
    static bool are_pcie_device_infos_equal(const hailo_pcie_device_info_t &first, const hailo_pcie_device_info_t &second);
    static std::string remove_pci_prefix(const std::string &device_info_str);
};
 
 } /* namespace hailort */
 
 #endif /* _HAILO_USB_UTILS_HPP_ */
 