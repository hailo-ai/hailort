/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file memory_requirements_command.hpp
 * @brief Command that prints memory requirements for running models
 **/

#ifndef _HAILO_memory_requirements_command_HPP_
#define _HAILO_memory_requirements_command_HPP_

#include "command.hpp"

class MemoryRequirementsCommand : public Command {
public:
    explicit MemoryRequirementsCommand(CLI::App &parent_app);

    virtual hailo_status execute() override;
};

#endif /* _HAILO_memory_requirements_command_HPP_ */
