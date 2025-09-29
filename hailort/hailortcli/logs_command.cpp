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
    Command(parent_app.add_subcommand("logs", "Prints the device logs")),
    m_should_follow(false)
{
    add_device_options(m_app, m_device_params);
    m_app->add_option("log_type", m_log_type, "Logs type")
        ->transform(HailoCheckedTransformer<hailo_log_type_t>({
            { "runtime", HAILO_LOG_TYPE__RUNTIME },
            { "system_control", HAILO_LOG_TYPE__SYSTEM_CONTROL },
            { "nnc", HAILO_LOG_TYPE__NNC }
        }))
        ->required()
        ->default_val("runtime");

    m_app->add_flag("-f,--follow", m_should_follow, "Follow log output (like tail -f).")
            ->default_val(false);
}

hailo_status LogsCommand::read_log(Device &device)
{
    TRY(auto max_log_size, device.get_max_logs_size(m_log_type));

    TRY(auto log_buffer, Buffer::create(max_log_size));
    auto log_mem_view = MemoryView(log_buffer);

    TRY(auto log_size, device.fetch_logs(log_mem_view, m_log_type));

    std::cout.write(reinterpret_cast<const char*>(log_buffer.data()), log_size);
    CHECK(!std::cout.fail(), HAILO_INTERNAL_FAILURE, "Failed to write logs to stdout.");

    return HAILO_SUCCESS;
}

hailo_status LogsCommand::execute()
{
    CHECK_SUCCESS(validate_specific_device_is_given(m_device_params));
    TRY(auto devices, create_devices(m_device_params));
    CHECK(1 == devices.size(), HAILO_INTERNAL_FAILURE, "Multiple devices created");
    return execute_on_device(*devices[0]);
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
