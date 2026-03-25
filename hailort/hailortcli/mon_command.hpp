/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file mon_command.hpp
 * @brief Monitor command - dispatches to the appropriate monitor implementation
 **/

#ifndef _HAILO_MON_COMMAND_HPP_
#define _HAILO_MON_COMMAND_HPP_

#include "hailo/hailort.h"

#include "hailortcli.hpp"
#include "command.hpp"

#include "CLI/CLI.hpp"

namespace hailort
{

class MonCommand : public Command
{
public:
    explicit MonCommand(CLI::App &parent_app);

    virtual hailo_status execute() override;

private:
    bool m_verbose;
};

} /* namespace hailort */

#endif /* _HAILO_MON_COMMAND_HPP_ */
