/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file fw_logger_command.hpp
 * @brief Write fw log to output file
 **/

#ifndef _HAILO_FW_LOGGER_COMMAND_COMMAND_HPP_
#define _HAILO_FW_LOGGER_COMMAND_COMMAND_HPP_

#include "hailortcli.hpp"
#include "command.hpp"

#include "hailo/hailort.h"
#include "hailo/device.hpp"
#include "hailo/buffer.hpp"
#include "CLI/CLI.hpp"


class FwLoggerCommand : public DeviceCommand {
public:
    explicit FwLoggerCommand(CLI::App &parent_app);

protected:
    virtual void pre_execute() override;
    virtual hailo_status execute_on_device(Device &device) override;

private:
    std::string m_output_file;
    bool m_should_overwrite;
    bool m_stdout;
    bool m_continuos;

    hailo_status write_logs(Device &device, std::ostream *os, hailo_cpu_id_t cpu_id);
};

#endif /* _HAILO_FW_LOGGER_COMMAND_COMMAND_HPP_ */
