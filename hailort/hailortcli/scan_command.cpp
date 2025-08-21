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
{}

hailo_status ScanSubcommand::execute()
{
    return scan();
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
