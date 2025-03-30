/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file fw_update_command.cpp
 * @brief Update fw on hailo device with flash
 **/

#include "fw_update_command.hpp"
#include "common/file_utils.hpp"

FwUpdateCommand::FwUpdateCommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("fw-update", "Firmware update tool (only for flash based devices)")),
    m_firmware_path(),
    m_skip_reset(false)
{
    m_app->add_option("firmware", m_firmware_path, "The path to the firmware binary")
        ->required()
        ->check(CLI::ExistingFile);
    m_app->add_flag("--skip-reset", m_skip_reset, "Don't reset after update");
}

hailo_status FwUpdateCommand::execute_on_device(Device &device)
{
    auto firmware = read_binary_file(m_firmware_path);
    if (!firmware) {
        std::cerr << "Failed reading firmware file " << firmware.status() << std::endl;
        return firmware.status();
    }

    std::cout << "Updating firmware" << std::endl;
    const bool should_reset = !m_skip_reset;
    auto status = device.firmware_update(MemoryView(firmware->data(), static_cast<uint32_t>(firmware->size())), should_reset);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Update firmware failed, error code " << status << std::endl;
        return status;
    }

    std::cout << "Firmware has been updated" << std::endl;

    return HAILO_SUCCESS;
}

