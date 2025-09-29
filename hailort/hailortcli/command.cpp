/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
    Command(app),
    m_show_stdout(true)
{
    add_device_options(m_app, m_device_params);
}

void DeviceCommand::pre_execute()
{
    // Do nothing by default
}

hailo_status DeviceCommand::execute()
{
    pre_execute();

    TRY(auto devices, create_devices(m_device_params));
    return execute_on_devices(devices);
}

hailo_status DeviceCommand::execute_on_devices(std::vector<std::unique_ptr<Device>> &devices)
{
    auto status = HAILO_SUCCESS; // Best effort
    for (auto &device : devices) {
        if (m_show_stdout) {
            std::cout << "Executing on device: " << device->get_dev_id() << std::endl;
        }
        auto execute_status = execute_on_device(*device);
        if (HAILO_SUCCESS != execute_status) {
            std::cerr << "Failed to execute on device: " << device->get_dev_id() << ". status= " << execute_status << std::endl;
            status = execute_status;
        }
    }
    return status;
}

hailo_status DeviceCommand::validate_specific_device_is_given()
{
    return ::validate_specific_device_is_given(m_device_params);
}
