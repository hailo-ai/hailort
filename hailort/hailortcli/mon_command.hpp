/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "utils/profiler/monitor_handler.hpp"
#include "common/runtime_statistics_internal.hpp"

#include "CLI/CLI.hpp"

namespace hailort
{

class MonCommand : public Command
{
public:
    explicit MonCommand(CLI::App &parent_app);

    virtual hailo_status execute() override;

private:
    hailo_status run_monitor();
    hailo_status print_tables(const std::vector<ProtoMon> &mon_messages, uint32_t terminal_line_width);
    void add_devices_info_header(std::ostream &buffer);
    void add_networks_info_header(std::ostream &buffer);
    void add_frames_header(std::ostream &buffer);
    void add_devices_info_table(const ProtoMon &mon_message, std::ostream &buffer);
    void add_networks_info_table(const ProtoMon &mon_message, std::ostream &buffer);
    hailo_status print_frames_table(const ProtoMon &mon_message, std::ostream &buffer);
    hailo_status run_in_alternative_terminal();
};

} /* namespace hailort */

#endif /* _HAILO_MON_COMMAND_HPP_ */

