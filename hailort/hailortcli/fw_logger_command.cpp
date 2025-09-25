/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
    m_should_overwrite(false),
    m_stdout(false),
    m_continuos(false)
{
    m_app->add_option("output_file", m_output_file, "File path to write binary firmware log into")
        ->required();
    m_app->add_flag("--overwrite", m_should_overwrite, "Should overwrite the file or not");
    m_app->add_flag("--stdout", m_stdout, "Write the output to stdout instead of a file");
    m_app->add_flag("--continuos", m_continuos, "Write to file/stdout, until the process is killed");
}

hailo_status FwLoggerCommand::write_logs(Device &device, std::ostream *os, hailo_cpu_id_t cpu_id)
{
    auto still_has_logs = true;
    static const auto buffer_size = AMOUNT_OF_BYTES_TO_READ;

    TRY(auto buffer, Buffer::create(buffer_size));

    while (still_has_logs || m_continuos) {
        MemoryView response_view(buffer);
        TRY(const auto response_size, device.read_log(response_view, cpu_id));
        if (response_size == 0) {
            still_has_logs = false;
        } else {
            os->write((char *)buffer.data(), response_size);
            CHECK(os->good(), HAILO_FILE_OPERATION_FAILURE,
                "Failed writing firmware logger to output file, with errno: {}", errno);
            os->flush();
        }
    }
    return HAILO_SUCCESS;
}

void FwLoggerCommand::pre_execute()
{
    if (m_stdout) {
        // We want only the binary data from the logger to be written to stdout
        DeviceCommand::m_show_stdout = false;
    }
}

hailo_status FwLoggerCommand::execute_on_device(Device &device)
{
    auto status = validate_specific_device_is_given();
    CHECK_SUCCESS(status,
        "'fw-logger' command should get a specific device-id");

    // Initialization dependency
    std::ofstream ofs;
    std::ostream *os = nullptr;
    if (m_stdout) {
        os = &std::cout;
    } else {
        auto ofs_flags = std::ios::out | std::ios::binary;
        if (!m_should_overwrite){
            ofs_flags |= std::ios::app;
        }
        ofs.open(m_output_file, ofs_flags);
        CHECK(ofs.good(), HAILO_OPEN_FILE_FAILURE, "Failed opening file: {}, with errno: {}", m_output_file, errno);
        os = &ofs;
    }

    if (Device::Type::ETH == device.get_type()) {
        LOGGER__ERROR("Read FW log is not supported over Eth device");
        return HAILO_INVALID_OPERATION;
    }

    if (Device::Type::INTEGRATED != device.get_type()) {
        status = write_logs(device, os, HAILO_CPU_ID_0);
        if (status != HAILO_SUCCESS){
            return status;
        }
    }

    status = write_logs(device, os, HAILO_CPU_ID_1);
    if (status != HAILO_SUCCESS){
        return status;
    }

    return HAILO_SUCCESS;
}

