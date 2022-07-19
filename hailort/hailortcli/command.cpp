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
    if ((DeviceType::PCIE == m_device_params.device_type ) &&
        ("*" == m_device_params.pcie_params.pcie_bdf)) {
        return execute_on_all_pcie_devices();
    }
    auto device = create_device(m_device_params);
    if (!device) {
        return device.status();
    }

    return execute_on_device(*device.value());
}

hailo_status DeviceCommand::execute_on_all_pcie_devices()
{
    auto status = HAILO_SUCCESS; // Best effort
    auto all_devices_infos = Device::scan_pcie();
    if (!all_devices_infos) {
        return all_devices_infos.status();
    }
    for (auto &dev_info : all_devices_infos.value()) {
        auto device = Device::create_pcie(dev_info);
        if (!device) {
            return device.status();
        }
        std::cout << "Executing on device: " << device.value()->get_dev_id() << std::endl;

        auto execute_status = execute_on_device(*device.value());
        if (HAILO_SUCCESS != execute_status) {
            std::cerr << "Failed to execute on device: " << device.value()->get_dev_id() << ". status= " << execute_status << std::endl;
            status = execute_status;
        }
    }
    return status;
}
