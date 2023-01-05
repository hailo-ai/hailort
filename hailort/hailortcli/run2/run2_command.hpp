/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file run2_command.hpp
 * @brief Run inference on hailo device
 **/

#ifndef _HAILO_HAILORTCLI_RUN2_RUN2_COMMAND_HPP_
#define _HAILO_HAILORTCLI_RUN2_RUN2_COMMAND_HPP_

#include "../command.hpp"

class Run2Command : public Command {
public:
    explicit Run2Command(CLI::App &parent_app);

    hailo_status execute() override;
private:
};

#endif /* _HAILO_HAILORTCLI_RUN2_RUN2_COMMAND_HPP_ */