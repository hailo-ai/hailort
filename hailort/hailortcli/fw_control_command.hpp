/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file fw_control.hpp
 * @brief Several controls that can be sent to the firware
 **/

#ifndef _HAILO_FW_CONTROL_COMMAND_HPP_
#define _HAILO_FW_CONTROL_COMMAND_HPP_

#include "hailortcli.hpp"
#include "command.hpp"
#include "download_action_list_command.hpp"

class FwControlIdentifyCommand : public DeviceCommand {
public:
    explicit FwControlIdentifyCommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;

private:
    bool m_is_extended;
};

class FwControlResetCommand : public DeviceCommand {
public:
    explicit FwControlResetCommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;

private:
    hailo_reset_device_mode_t m_reset_mode;
};

class FwControlTestMemoriesCommand : public DeviceCommand {
public:
    explicit FwControlTestMemoriesCommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;
};

class FwControlDebugHaltContinueCommand : public DeviceCommand {
public:
    explicit FwControlDebugHaltContinueCommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;
};

class FwControlDebugCommand : public ContainerCommand {
public:
    explicit FwControlDebugCommand(CLI::App &parent_app);
};

class FwControlCommand : public ContainerCommand {
public:
    explicit FwControlCommand(CLI::App &parent_app);
};


#endif /* _HAILO_FW_CONTROL_COMMAND_HPP_ */
