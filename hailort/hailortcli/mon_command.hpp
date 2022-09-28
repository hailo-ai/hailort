/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file mon_command.hpp
 * @brief Monitor of networks - Presents information about the running networks
 **/

#ifndef _HAILO_MON_COMMAND_HPP_
#define _HAILO_MON_COMMAND_HPP_

#include "hailo/hailort.h"
#include "hailortcli.hpp"
#include "command.hpp"
#include "scheduler_mon.hpp"

#include "CLI/CLI.hpp"

namespace hailort
{

class MonCommand : public Command
{
public:
    explicit MonCommand(CLI::App &parent_app);

    virtual hailo_status execute() override;

private:
    hailo_status print_table();
    size_t print_networks_info_header();
    size_t print_frames_header();
    size_t print_networks_info_table(const ProtoMon &mon_message);
    size_t print_frames_table(const ProtoMon &mon_message);
};

} /* namespace hailort */

#endif /* _HAILO_MON_COMMAND_HPP_ */

