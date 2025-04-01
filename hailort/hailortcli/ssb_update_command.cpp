/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file ssb_update_command.cpp
 * @brief Update second stage boot on hailo device with flash
 **/

#include "ssb_update_command.hpp"
#include "common/file_utils.hpp"


SSBUpdateCommand::SSBUpdateCommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("ssb-update", "Second stage boot update command (only for flash based devices)")),
    m_second_stage_path()
{
    m_app->add_option("file_path", m_second_stage_path, "The path to the second stage boot binary")
        ->required()
        ->check(CLI::ExistingFile);
}

hailo_status SSBUpdateCommand::execute_on_device(Device &device)
{
    auto second_stage = read_binary_file(m_second_stage_path);
    if (!second_stage) {
        std::cerr << "Failed reading second stage boot file " << second_stage.status() << std::endl;
        return second_stage.status();
    }

    std::cout << "Updating second stage boot..." << std::endl;
    auto status = device.second_stage_update(second_stage->data(), static_cast<uint32_t>(second_stage->size()));
    if (HAILO_SUCCESS != status) {
        std::cerr << "Update second stage boot failed with error: " << status << std::endl;
        return status;
    }

    std::cout << "second stage boot has been updated successfully" << std::endl;

    return HAILO_SUCCESS;
}