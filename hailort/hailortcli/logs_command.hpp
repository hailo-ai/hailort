/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file logs_command.hpp
 * @brief Prints the device logs to stdout in a loop.
 **/

#ifndef _HAILO_LOGS_COMMAND_HPP_
#define _HAILO_LOGS_COMMAND_HPP_

#include "hailortcli.hpp"
#include "command.hpp"


class LogsCommand : public DeviceCommand {
public:
    explicit LogsCommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;
    virtual void pre_execute() override;

private:
    hailo_status read_log(Device &device);

    hailo_log_type_t m_log_type;
    bool m_should_follow;
};

#endif /* _HAILO_LOGS_COMMAND_HPP_ */
