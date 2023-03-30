/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file fw_logger_command.cpp
 * @brief Write fw log to output file
 **/

#include "fw_logger_command.hpp"
#include "common/file_utils.hpp"

#define AMOUNT_OF_BYTES_TO_READ 256

FwLoggerCommand::FwLoggerCommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("fw-logger", "Download fw logs to a file")),
    m_should_overwrite(false)
{
    m_app->add_option("output_file", m_output_file, "File path to write binary firmware log into")
        ->required();
    m_app->add_flag("--overwrite", m_should_overwrite, "Should overwrite the file or not");
}

hailo_status write_logs_to_file(Device &device, std::ofstream &ofs, hailo_cpu_id_t cpu_id){
    auto still_has_logs = true;
    static const auto buffer_size = AMOUNT_OF_BYTES_TO_READ;
    
    auto expected_buffer = Buffer::create(buffer_size);
    CHECK_EXPECTED_AS_STATUS(expected_buffer);
    Buffer buffer = expected_buffer.release();

    while(still_has_logs) {
        MemoryView response_view(buffer);
        auto response_size_expected = device.read_log(response_view, cpu_id);
        CHECK_EXPECTED_AS_STATUS(response_size_expected);

        auto response_size = response_size_expected.release();
        if (response_size == 0) {
            still_has_logs = false;
        }
        else {
            ofs.write((char *)buffer.data(), response_size);
            CHECK(ofs.good(), HAILO_FILE_OPERATION_FAILURE,
                "Failed writing firmware logger to output file, with errno: {}", errno);
        }
    }
    return HAILO_SUCCESS;
}

hailo_status FwLoggerCommand::execute_on_device(Device &device)
{
    auto status = validate_specific_device_is_given();
    CHECK_SUCCESS(status,
        "'fw-logger' command should get a specific device-id");
        
    auto ofs_flags = std::ios::out | std::ios::binary;

    if (!m_should_overwrite){
        ofs_flags |= std::ios::app;
    }

    std::ofstream ofs(m_output_file, ofs_flags);
    CHECK(ofs.good(), HAILO_OPEN_FILE_FAILURE, "Failed opening file: {}, with errno: {}", m_output_file, errno);

    if (Device::Type::ETH == device.get_type()) {
        LOGGER__ERROR("Read FW log is not supported over Eth device");
        return HAILO_INVALID_OPERATION;
    }
    
    if (Device::Type::INTEGRATED != device.get_type()) {
        status = write_logs_to_file(device, ofs, HAILO_CPU_ID_0);
        if (status != HAILO_SUCCESS){
            return status;
        }
    }

    status = write_logs_to_file(device, ofs, HAILO_CPU_ID_1);
    if (status != HAILO_SUCCESS){
        return status;
    }

    return HAILO_SUCCESS;
}

