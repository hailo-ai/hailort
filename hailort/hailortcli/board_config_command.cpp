/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file board_config_command.cpp
 * @brief Board configuration command (fw static configuration).
 **/

#include "board_config_command.hpp"
#include "common/file_utils.hpp"

#include <fstream>

BoardConfigCommand::BoardConfigCommand(CLI::App &parent_app) :
    ContainerCommand(parent_app.add_subcommand("board-config", "Board configuration tool"))
{
    // This will make the board-config command to be hidden in the --help print in the command line.
    m_app->group("");

    add_subcommand<BoardConfigReadSubcommand>();
    add_subcommand<BoardConfigWriteSubcommand>();
}

BoardConfigReadSubcommand::BoardConfigReadSubcommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("read", "Read board configuration from device"))
{
    m_app->add_option("output_file", m_output_file_path, "File path to dump board configuration into.")
        ->required();
}

hailo_status BoardConfigReadSubcommand::execute_on_device(Device &device)
{
    auto status = validate_specific_device_is_given();
    CHECK_SUCCESS(status,
        "'board-config read' command should get a specific device-id.");

    auto buffer = device.read_board_config();
    CHECK_EXPECTED_AS_STATUS(buffer, "Failed reading board config from device");

    auto output_file = std::ofstream(m_output_file_path, std::ios::out | std::ios::binary);
    CHECK(output_file.is_open(), HAILO_OPEN_FILE_FAILURE, "Failed opening output file {} with errno: {}", m_output_file_path, errno);

    output_file.write(reinterpret_cast<char*>(buffer->data()), buffer->size());
    CHECK(output_file.good(), HAILO_FILE_OPERATION_FAILURE, "Failed writing board config into file {}.", m_output_file_path);

    return HAILO_SUCCESS;
}

BoardConfigWriteSubcommand::BoardConfigWriteSubcommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("write", "Write board configuration to device"))
{
    m_app->add_option("input_file", m_input_file_path, "Board config binary file path.")
        ->check(CLI::ExistingFile)
        ->required();
}

hailo_status BoardConfigWriteSubcommand::execute_on_device(Device &device)
{
    auto buffer = read_binary_file(m_input_file_path);
    CHECK_EXPECTED_AS_STATUS(buffer);

    hailo_status status = device.write_board_config(MemoryView(buffer.value()));
    CHECK_SUCCESS(status, "Failed writing board config to device.");

    return HAILO_SUCCESS;
}
