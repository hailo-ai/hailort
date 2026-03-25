/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pcie_utils.cpp
 * @brief Implementation of PCIe utils
 **/

 #include "hailo/hailort.h"
 #include "device_common/pcie_utils.hpp"
 #include "common/utils.hpp"
 #include "vdma/driver/hailort_driver.hpp"

 #include <sstream>
 #include <iomanip>

 namespace hailort
 {
 
static const std::string PCI_PREFIX = "pci/";

Expected<std::vector<hailo_pcie_device_info_t>> PcieUtils::scan()
{
    TRY(auto scan_results, HailoRTDriver::scan_devices());

    std::vector<hailo_pcie_device_info_t> out_results;
    out_results.reserve(scan_results.size());
    for (const auto &scan_result : scan_results) {
        TRY(auto device_info, parse_pcie_device_info(scan_result.device_id), "Invalid PCIe device info string");
        out_results.emplace_back(device_info);
    }

    return out_results;
}

Expected<hailo_pcie_device_info_t> PcieUtils::parse_pcie_device_info(const std::string &device_info_str)
{
    static const std::vector<std::string> FORMATS_WITH_DOMAIN = {PCI_PREFIX + "%04x:%02x:%02x.%d", "%04x:%02x:%02x.%d"};
    static constexpr auto ELEMENTS_COUNT_WITH_DOMAIN = 4;

    static const std::vector<std::string> FORMATS_WITHOUT_DOMAIN = {PCI_PREFIX + "%02x:%02x.%d", "%02x:%02x.%d"};
    static constexpr auto ELEMENTS_COUNT_WITHOUT_DOMAIN = 3;

    hailo_pcie_device_info_t device_info{};
    for (const auto &format : FORMATS_WITH_DOMAIN) {
        int scanf_res = sscanf(device_info_str.c_str(), format.c_str(), &device_info.domain, &device_info.bus, &device_info.device, &device_info.func);
        if (ELEMENTS_COUNT_WITH_DOMAIN == scanf_res) {
            return device_info;
        }
    }

    for (const auto &format : FORMATS_WITHOUT_DOMAIN) {
        int scanf_res = sscanf(device_info_str.c_str(), format.c_str(), &device_info.bus, &device_info.device, &device_info.func);
        if (ELEMENTS_COUNT_WITHOUT_DOMAIN == scanf_res) {
            device_info.domain = HAILO_PCIE_ANY_DOMAIN;
            return device_info;
        }
    }

    return make_unexpected(HAILO_INVALID_ARGUMENT);
}

Expected<std::string> PcieUtils::pcie_device_info_to_string(const hailo_pcie_device_info_t &device_info)
{
    // same format as in lspci - [<domain>].<bus>.<device>.<func> 
    // domain (0 to ffff) bus (0 to ff), device (0 to 1f) and function (0 to 7).

    std::ostringstream ss;
    if (HAILO_PCIE_ANY_DOMAIN == device_info.domain) {
        ss << PCI_PREFIX << std::setfill('0') << std::hex << std::setw(2) << static_cast<uint32_t>(device_info.bus)
            << ":" << std::setw(2) << static_cast<uint32_t>(device_info.device)
            << "." << static_cast<uint32_t>(device_info.func);
    } else {
        ss << PCI_PREFIX << std::setfill('0') << std::hex << std::setw(4) << static_cast<uint32_t>(device_info.domain)
            << ":" << std::setw(2) << static_cast<uint32_t>(device_info.bus)
            << ":" << std::setw(2) << static_cast<uint32_t>(device_info.device)
            << "." << static_cast<uint32_t>(device_info.func);
    }
    return ss.str();
}

bool PcieUtils::are_pcie_device_infos_equal(const hailo_pcie_device_info_t &first, const hailo_pcie_device_info_t &second)
{
    const bool bdf_equal = (first.bus == second.bus) && (first.device == second.device) && (first.func == second.func);
    const bool domain_equal = (HAILO_PCIE_ANY_DOMAIN == first.domain) || (HAILO_PCIE_ANY_DOMAIN == second.domain) ||
        (first.domain == second.domain);
    return bdf_equal && domain_equal;
}

std::string PcieUtils::remove_pci_prefix(const std::string &device_info_str)
{
    if (device_info_str.compare(0, PCI_PREFIX.size(), PCI_PREFIX) == 0) {
        return device_info_str.substr(PCI_PREFIX.size());
    }
    return device_info_str;
}
 
 } /* namespace hailort */