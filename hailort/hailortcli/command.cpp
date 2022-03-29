/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file command.cpp
 * @brief Base classes for hailortcli commands.
 **/


#include "command.hpp"

Command::Command(CLI::App *app) :
    m_app(app)
{
}

ContainerCommand::ContainerCommand(CLI::App *app) :
    Command(app)
{
    m_app->require_subcommand(1);
}

hailo_status ContainerCommand::execute()
{
    for (auto &command : m_subcommands) {
        if (command->parsed()) {
            return command->execute();
        }
    }

    LOGGER__ERROR("No subommand found..");
    return HAILO_NOT_FOUND;
}

DeviceCommand::DeviceCommand(CLI::App *app) :
    Command(app)
{
    add_device_options(m_app, m_device_params);
}

hailo_status DeviceCommand::execute()
{
    auto device = create_device(m_device_params);
    if (!device) {
        return device.status();
    }

    return execute_on_device(*device.value());
}

PcieDeviceCommand::PcieDeviceCommand(CLI::App *app) :
    Command(app)
{
    auto group = app->add_option_group("PCIE Device Options");

    // PCIe options
    group->add_option("-s,--bdf", m_pcie_device_params.pcie_bdf,
        "Device id ([<domain>]:<bus>:<device>.<func>, same as in lspci command)")
        ->default_val("");
}

hailo_status PcieDeviceCommand::execute()
{
    auto device = create_pcie_device(m_pcie_device_params);
    if (!device) {
        return device.status();
    }

    return execute_on_device(*device.value());
}
