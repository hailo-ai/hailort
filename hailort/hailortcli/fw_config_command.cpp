/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file fw_config_command.cpp
 * @brief User Firmware configuration (persistent config) command.
 **/

#include "fw_config_command.hpp"

FwConfigReadSubcommand::FwConfigReadSubcommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("read", "Read firmware configuration from device"))
{
    m_app->add_option("--output-file", m_output_file, "File path to write user firmware configuration into.\n"
        "If not given the data will be printed to stdout.");
}

hailo_status FwConfigReadSubcommand::execute_on_device(Device &device)
{
    auto status = validate_specific_device_is_given();
    CHECK_SUCCESS(status,
        "'fw-config read' command should get a specific device-id.");

    auto user_config_buffer = device.read_user_config();
    CHECK_EXPECTED_AS_STATUS(user_config_buffer, "Failed reading user config from device");

    status = FwConfigJsonSerializer::deserialize_config(
        *reinterpret_cast<USER_CONFIG_header_t*>(user_config_buffer->data()),
        user_config_buffer->size(), m_output_file);
    CHECK_SUCCESS(status);
    
    return HAILO_SUCCESS;   
}

FwConfigWriteSubcommand::FwConfigWriteSubcommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("write", "Write firmware configuration to device"))
{
    m_app->add_option("input_file", m_input_file, "User firmware configuration file path.")
        ->check(CLI::ExistingFile)
        ->required();
}

hailo_status FwConfigWriteSubcommand::execute_on_device(Device &device)
{
    auto config_buffer = Buffer::create(FLASH_USER_CONFIG_SECTION_SIZE);
    CHECK_EXPECTED_AS_STATUS(config_buffer);

    auto config_size = FwConfigJsonSerializer::serialize_config(
        *reinterpret_cast<USER_CONFIG_header_t*>(config_buffer->data()), config_buffer->size(), m_input_file);
    CHECK_EXPECTED_AS_STATUS(config_size);
    
    // We only need to write config_size.value() bytes from config_buffer, so we "resize" the buffer
    CHECK(config_buffer->size() >= config_size.value(), HAILO_INTERNAL_FAILURE,
        "Unexpected config size {} (max_size={})", config_size.value(), config_buffer->size());
    auto resized_config_buffer = Buffer::create(config_buffer->data(), config_size.value());
    CHECK_EXPECTED_AS_STATUS(resized_config_buffer);

    hailo_status status = device.write_user_config(MemoryView(resized_config_buffer.value()));
    CHECK_SUCCESS(status, "Failed writing user firmware configuration to device");

    return HAILO_SUCCESS;
}

FwConfigSerializeSubcommand::FwConfigSerializeSubcommand(CLI::App &parent_app) :
    Command(parent_app.add_subcommand("serialize", "Serialize firmware configuration json to a binary file"))
{
    m_app->add_option("input_file", m_input_file, "File path to firmware configuration json")
        ->check(CLI::ExistingFile)
        ->required();
    m_app->add_option("output_file", m_output_file, "File path to write binary firmware configuration into")
        ->required();
}

hailo_status FwConfigSerializeSubcommand::execute()
{
    auto config_buffer = Buffer::create(FLASH_USER_CONFIG_SECTION_SIZE);
    CHECK_EXPECTED_AS_STATUS(config_buffer);

    USER_CONFIG_header_t *config_header = reinterpret_cast<USER_CONFIG_header_t*>(config_buffer->data());
    auto config_size = FwConfigJsonSerializer::serialize_config(*config_header, config_buffer->size(), m_input_file);
    CHECK_EXPECTED_AS_STATUS(config_size);

    std::ofstream ofs(m_output_file, std::ios::out | std::ios::binary);
    CHECK(ofs.good(), HAILO_OPEN_FILE_FAILURE, "Failed opening file: {}, with errno: {}", m_output_file, errno);

    ofs.write(reinterpret_cast<char*>(config_header), config_size.value());
    CHECK(ofs.good(), HAILO_FILE_OPERATION_FAILURE,
        "Failed writing binary firmware configuration to file: {}, with errno: {}", m_output_file, errno);

    return HAILO_SUCCESS;
}

FwConfigCommand::FwConfigCommand(CLI::App &parent_app) :
    ContainerCommand(parent_app.add_subcommand("fw-config", "User firmware configuration tool"))
{
    add_subcommand<FwConfigReadSubcommand>();
    add_subcommand<FwConfigWriteSubcommand>();
    add_subcommand<FwConfigSerializeSubcommand>();
}
