/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file ssb_update_command.hpp
 * @brief Update second stage boot on hailo device with flash
 **/

#ifndef _HAILORTCLI_SSB_UPDATE_COMMAND_HPP_
#define _HAILORTCLI_SSB_UPDATE_COMMAND_HPP_

#include "hailortcli.hpp"
#include "command.hpp"

#include "hailo/hailort.h"
#include "hailo/device.hpp"
#include "hailo/buffer.hpp"
#include "CLI/CLI.hpp"


class SSBUpdateCommand : public DeviceCommand {
public:
    explicit SSBUpdateCommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;

private:
    std::string m_second_stage_path;
};

#endif /* _HAILORTCLI_SSB_UPDATE_COMMAND_HPP_ */
