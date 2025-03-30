/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scan_command.cpp
 * @brief Scan hailo devices
 **/
#include "scan_command.hpp"
#include "hailortcli.hpp"
#include "common/socket.hpp"

#include <iostream>


ScanSubcommand::ScanSubcommand(CLI::App &parent_app) :
    Command(parent_app.add_subcommand("scan", "Shows all available devices"))
{
    // Ethernet options
    auto *eth_options_group = m_app->add_option_group("Ethernet Device Options");
    auto *interface_ip_option = eth_options_group->add_option("--interface-ip", m_interface_ip_addr,
        "Interface IP address to scan")
        ->default_val("")
        ->check(CLI::ValidIPV4);

    eth_options_group->add_option("--interface", m_interface_name, "Interface name to scan")
        ->default_val("")
        ->excludes(interface_ip_option);
}

hailo_status ScanSubcommand::execute()
{
    const bool request_for_eth_scan = !m_interface_name.empty() || !m_interface_ip_addr.empty();

    if (!request_for_eth_scan) {
        return scan();
    }
    else {
        TRY(const auto res, scan_ethernet(m_interface_ip_addr, m_interface_name));
        return HAILO_SUCCESS;
    }
}

hailo_status ScanSubcommand::scan()
{
    TRY(const auto device_ids, Device::scan());
    if (device_ids.size() == 0) {
        std::cout << "Hailo devices not found" << std::endl;
    }
    else {
        std::cout << "Hailo Devices:" << std::endl;
        for (const auto &device_id : device_ids) {
            std::cout << "[-] Device: " << device_id << std::endl;
        }
    }

    return HAILO_SUCCESS;
}

Expected<std::vector<std::string>> ScanSubcommand::scan_ethernet(const std::string &interface_ip_addr,
    const std::string &interface_name)
{
    const std::chrono::seconds timeout(3);
    std::vector<hailo_eth_device_info_t> device_infos;

    if (!interface_ip_addr.empty()) {
        auto result = Device::scan_eth_by_host_address(interface_ip_addr, timeout);
        if (!result) {
            std::cerr << "Failed scanning ethernet device from host address (" << result.status() << ")" << std::endl;
            return make_unexpected(result.status());
        }
        device_infos = result.release();
    } else {
        auto result = Device::scan_eth(interface_name, timeout);
        if (!result) {
            std::cerr << "Failed scanning ethernet device from interface name (" << result.status() << ")" << std::endl;
            return make_unexpected(result.status());
        }
        device_infos = result.release();
    }

    std::cout << "Hailo Ethernet Devices:" << std::endl;
    std::vector<std::string> ip_addresses;
    ip_addresses.reserve(device_infos.size());
    char textual_ip_address[INET_ADDRSTRLEN] = {};
    for (size_t i = 0; i < device_infos.size(); i++) {
        auto status = Socket::ntop(AF_INET, &(device_infos[i].device_address.sin_addr), textual_ip_address,
            INET_ADDRSTRLEN);
        if (status != HAILO_SUCCESS) {
            std::cerr << "Could not convert ip address to textual format (inet_ntop has failed)" << std::endl;
            continue;
        }

        std::cout << "[-] Board IP: " << textual_ip_address << std::endl;
        ip_addresses.emplace_back(textual_ip_address);
    }
    return ip_addresses;
}
