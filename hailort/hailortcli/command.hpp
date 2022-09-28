/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file command.hpp
 * @brief Base classes for hailortcli commands.
 **/

#ifndef _HAILO_COMMAND_HPP_
#define _HAILO_COMMAND_HPP_

#include "hailortcli.hpp"
#include "CLI/CLI.hpp"


class Command {
public:
    explicit Command(CLI::App *app);
    virtual ~Command() = default;

    virtual hailo_status execute() = 0;

    bool parsed() const
    {
        return m_app->parsed();
    }

    void set_footer(const std::string &new_footer)
    {
        m_app->footer(new_footer);
    }

protected:
    CLI::App *m_app;
};

// Command that only contains list of subcommand
class ContainerCommand : public Command {
public:
    explicit ContainerCommand(CLI::App *app);
    virtual hailo_status execute() override final;

protected:

    template<typename CommandType>
    CommandType &add_subcommand(bool hidden = false)
    {
        // Unnamed "option groups" hide subcommands/options from the help message
        // (see https://github.com/CLIUtils/CLI11/blob/main/README.md)
        auto *parent = hidden ? m_app->add_option_group("") : m_app;
        auto command = std::make_shared<CommandType>(*parent);
        m_subcommands.push_back(command);
        return *command;
    }

private:
    std::vector<std::shared_ptr<Command>> m_subcommands;
};

class DeviceCommand : public Command {
public:
    explicit DeviceCommand(CLI::App *app);
    virtual hailo_status execute() override final;

protected:
    virtual hailo_status execute_on_device(Device &device) = 0;
    hailo_status execute_on_devices(std::vector<std::unique_ptr<Device>> &devices);

private:
    hailo_device_params m_device_params;
};

#endif /* _HAILO_COMMAND_HPP_ */