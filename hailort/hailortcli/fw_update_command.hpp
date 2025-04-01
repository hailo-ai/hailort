/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file fw_update_command.hpp
 * @brief Update fw on hailo device with flash
 **/

#ifndef _HAILO_FW_UPDATE_COMMAND_COMMAND_HPP_
#define _HAILO_FW_UPDATE_COMMAND_COMMAND_HPP_

#include "hailortcli.hpp"
#include "command.hpp"

#include "hailo/hailort.h"
#include "hailo/device.hpp"
#include "hailo/buffer.hpp"
#include "CLI/CLI.hpp"


class FwUpdateCommand : public DeviceCommand {
public:
    explicit FwUpdateCommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;

private:
    std::string m_firmware_path;
    bool m_skip_reset;
};

#endif /* _HAILO_FW_UPDATE_COMMAND_COMMAND_HPP_ */
