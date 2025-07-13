/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file logs_command.cpp
 * @brief Prints logs to stdout in a loop.
 **/

#include "logs_command.hpp"

LogsCommand::LogsCommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("logs", "Prints the device logs")),
    m_should_follow(false)
{
    m_app->add_option("log_type", m_log_type, "Logs type")
        ->transform(HailoCheckedTransformer<hailo_log_type_t>({
            { "runtime", HAILO_LOG_TYPE__RUNTIME },
            { "system_control", HAILO_LOG_TYPE__SYSTEM_CONTROL },
            { "nnc", HAILO_LOG_TYPE__NNC }
        }))
        ->required()
        ->default_val("runtime");

    m_app->add_flag("-f,--follow", m_should_follow, "Follow log output (like tail -f). Not supported for 'nnc' log type")
            ->default_val(false);

    
    m_app->parse_complete_callback([this]() {
        PARSE_CHECK(!m_should_follow || (m_log_type != HAILO_LOG_TYPE__NNC),
            "'follow' option is not supported for 'nnc' log type");
    });
}

hailo_status LogsCommand::read_log(Device &device)
{
    auto status = validate_specific_device_is_given();
    CHECK_SUCCESS(status);

    TRY(auto max_log_size, device.get_max_logs_size(m_log_type));

    TRY(auto log_buffer, Buffer::create(max_log_size));
    auto log_mem_view = MemoryView(log_buffer);

    TRY(auto log_size, device.fetch_logs(log_mem_view, m_log_type));

    std::cout.write(reinterpret_cast<const char*>(log_buffer.data()), log_size);
    CHECK(!std::cout.fail(), HAILO_INTERNAL_FAILURE, "Failed to write logs to stdout.");

    return HAILO_SUCCESS;
}

hailo_status LogsCommand::execute_on_device(Device &device)
{
    if (!m_should_follow) {
        return read_log(device);
    }

    static constexpr std::chrono::milliseconds SLEEP_TIME_BETWEEN_FETCH_LOGS {2000};
    while (true) {
        auto status = read_log(device);
        CHECK_SUCCESS(status);
        std::this_thread::sleep_for(SLEEP_TIME_BETWEEN_FETCH_LOGS);
    }

    return HAILO_SUCCESS;
}

void LogsCommand::pre_execute()
{
    // We want only the logger data to be written to stdout
    DeviceCommand::m_show_stdout = false;
}
