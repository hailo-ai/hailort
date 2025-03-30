/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file fw_config_command.hpp
 * @brief User firmware configuration command.
 **/

#ifndef _HAILO_FW_CONFIG_COMMAND_HPP_
#define _HAILO_FW_CONFIG_COMMAND_HPP_

#include "hailortcli.hpp"
#include "command.hpp"

#include "fw_config_serializer.hpp"
#include "hailo/device.hpp"
#include "CLI/CLI.hpp"

#define FLASH_USER_CONFIG_SECTION_SIZE (0x008000)

class FwConfigReadSubcommand final : public DeviceCommand {
public:
    explicit FwConfigReadSubcommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;

private:
    std::string m_output_file;
};

class FwConfigWriteSubcommand final : public DeviceCommand {
public:
    explicit FwConfigWriteSubcommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;

private:
    std::string m_input_file;
};

class FwConfigSerializeSubcommand final : public Command {
public:
    explicit FwConfigSerializeSubcommand(CLI::App &parent_app);
    hailo_status execute() override;

private:
    std::string m_input_file;
    std::string m_output_file;
};

class FwConfigCommand final : public ContainerCommand {
public:
    explicit FwConfigCommand(CLI::App &parent_app);
};

#endif /* _HAILO_FW_CONFIG_COMMAND_HPP_ */
