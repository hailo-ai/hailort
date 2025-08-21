/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scan_command.hpp
 * @brief Scan hailo devices
 **/

#ifndef _HAILO_SCAN_COMMAND_HPP_
#define _HAILO_SCAN_COMMAND_HPP_

#include "hailortcli.hpp"
#include "command.hpp"
#include "hailo/hailort.h"
#include "hailo/device.hpp"
#include "CLI/CLI.hpp"


class ScanSubcommand final : public Command {
public:
    explicit ScanSubcommand(CLI::App &parent_app);
    hailo_status execute() override;

    static Expected<std::vector<std::string>> scan_ethernet(const std::string &interface_ip_addr,
        const std::string &interface_name);

private:
    // Scans any system device
    hailo_status scan();
};

#endif /* _HAILO_SCAN_COMMAND_HPP_ */