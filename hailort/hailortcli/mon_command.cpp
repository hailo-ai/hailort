/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file mon_command.cpp
 * @brief Monitor command - dispatches to the appropriate monitor implementation
 **/

#include "mon_command.hpp"
#include "hailo_monitor/hailo_monitor.hpp"

#include "common/env_vars.hpp"

namespace hailort
{

MonCommand::MonCommand(CLI::App &parent_app) :
    Command(parent_app.add_subcommand("monitor", "Monitor device stats. " \
     "On H15, presents information about the running networks " \
     "(requires environment variable '" + std::string(SCHEDULER_MON_ENV_VAR) + "' set to 1 in the application process). " \
     "On H10, presents performance and health stats.")),
    m_verbose(false)
{
    m_app->add_flag("-v,--verbose", m_verbose, "Show detailed frame queue information (H15 only)");
}

hailo_status MonCommand::execute()
{
#ifdef _WIN32
    LOGGER__ERROR("hailortcli `monitor` command is not supported on Windows");
    return HAILO_NOT_IMPLEMENTED;
#else
    return HailoMonitor::run(m_verbose);
#endif
}

} /* namespace hailort */
