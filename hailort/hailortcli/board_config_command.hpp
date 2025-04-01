/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file board_config_command.hpp
 * @brief Firmware configuration command.
 **/

#ifndef _HAILO_BOARD_CONFIG_COMMAND_HPP_
#define _HAILO_BOARD_CONFIG_COMMAND_HPP_

#include "hailortcli.hpp"
#include "command.hpp"

#include "hailo/hailort.h"
#include "hailo/device.hpp"
#include "CLI/CLI.hpp"

class BoardConfigReadSubcommand final : public DeviceCommand {
public:
    explicit BoardConfigReadSubcommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;

private:
    std::string m_output_file_path;
};

class BoardConfigWriteSubcommand final : public DeviceCommand {
public:
    explicit BoardConfigWriteSubcommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;

private:
    std::string m_input_file_path;
};

class BoardConfigCommand final : public ContainerCommand {
public:
    explicit BoardConfigCommand(CLI::App &parent_app);
};

#endif /* _HAILO_BOARD_CONFIG_COMMAND_HPP_ */