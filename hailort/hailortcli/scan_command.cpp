/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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
    Command(parent_app.add_subcommand("scan", "Shows all available devices")),
    m_device_type(Device::Type::PCIE)
{
    const HailoCheckedTransformer<Device::Type> device_type_transformer({
            { "pcie", Device::Type::PCIE },
            { "eth", Device::Type::ETH },
        });
    auto *device_type_option = m_app->add_option("-d,--device-type,--target", m_device_type,
        "Device type to use.")
        ->transform(device_type_transformer)
        ->default_val("pcie");

    // Ethernet options
    auto *eth_options_group = m_app->add_option_group("Ethernet Device Options");
    auto *interface_ip_option = eth_options_group->add_option("--interface-ip", m_interface_ip_addr,
        "Interface IP address to scan")
        ->default_val("")
        ->check(CLI::ValidIPV4);

    auto *interface_name_option = eth_options_group->add_option("--interface", m_interface_name, "Interface name to scan")
        ->default_val("")
        ->excludes(interface_ip_option);

    m_app->parse_complete_callback([this, device_type_option, interface_ip_option, interface_name_option]() {
        bool eth_options_given = !interface_ip_option->empty() || !interface_name_option->empty();

        // The user didn't put target, we can figure it ourself
        if (device_type_option->empty()) {
            if (eth_options_given) {
                // User gave IP, target is eth
                m_device_type = Device::Type::ETH;
            }
            else {
                // Default is pcie
                m_device_type = Device::Type::PCIE;
            }
        }

        if (!eth_options_given && (m_device_type == Device::Type::ETH)) {
            throw CLI::ParseError("Ethernet options not set", CLI::ExitCodes::InvalidError);
        }

        if (eth_options_given && (m_device_type != Device::Type::ETH)) {
            throw CLI::ParseError("Ethernet options set on non eth device", CLI::ExitCodes::InvalidError);
        }
    });
}

hailo_status ScanSubcommand::execute()
{
    switch (m_device_type)
    {
    case Device::Type::PCIE:
        return scan_pcie();
    case Device::Type::ETH:
        return scan_ethernet(m_interface_ip_addr, m_interface_name).status();
    default:
        std::cerr << "Unkown target" << std::endl;
        return HAILO_INVALID_ARGUMENT;
    }
}

hailo_status ScanSubcommand::scan_pcie()
{
    auto scan_result = Device::scan_pcie();
    CHECK_SUCCESS(scan_result.status(), "Error scan failed status = {}", scan_result.status());

    if (scan_result->size() == 0) {
        std::cout << "Hailo PCIe devices not found" << std::endl;
    }
    else {
        std::cout << "Hailo PCIe Devices:" << std::endl;
        for (const auto& device_info : scan_result.value()) {
            auto device_info_str = Device::pcie_device_info_to_string(device_info);
            CHECK_EXPECTED_AS_STATUS(device_info_str);
            std::cout << "[-] Device BDF: " << device_info_str.value() << std::endl;
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
