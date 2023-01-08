/**
 * @file shell.cpp
 * @brief Generic shell - contains commands and sub-shells. The shell implements
 *        a parse-execute commands loop.
 */
#include "shell.hpp"
#include "readline_wrapper.hpp"
#include "spdlog/fmt/fmt.h"

#include <cassert>
#include <tuple>

static std::vector<std::string> split_string(std::string s, const std::string &delimiter = " ")
{
    std::vector<std::string> parts;
    auto pos = std::string::npos;
    while ((pos = s.find(delimiter)) != std::string::npos) {
        parts.push_back(s.substr(0, pos));
        s.erase(0, pos + delimiter.size());
    }
    parts.push_back(s);
    return parts;
}

ShellCommand::ShellCommand(const std::string &name, const std::string &short_name, const std::string &help) :
    m_name(name),
    m_short_name(short_name),
    m_help(help)
{}

Shell::Shell(const std::string &prompt) :
    m_prompt(prompt),
    m_commands(),
    m_should_quit(false)
{
    add_command(std::make_unique<Quit>(*this));
    add_command(std::make_unique<Help>(*this));
}

void Shell::add_command(std::unique_ptr<ShellCommand> shell_command)
{
    assert(nullptr == get_command_by_name(shell_command->name()));
    assert(nullptr == get_command_by_name(shell_command->short_name()));

    m_commands.emplace_back(std::move(shell_command));
}

std::shared_ptr<Shell> Shell::add_subshell(const std::string &name, const std::string &short_name)
{
    auto subshell_cmd = std::make_unique<StartSubshellCommand>(name, short_name,
        fmt::format("Start {} subshell", name));
    auto shell = subshell_cmd->get_shell();
    add_command(std::move(subshell_cmd));
    return shell;
}

void Shell::run_forever()
{
    ReadLineWrapper::set_auto_completer([this](const std::string &text) {
        return autocomplete(text);
    });

    std::cout << get_help() << std::endl;
    while (!m_should_quit) {
        std::string name;
        std::vector<std::string> args;
        std::tie(name, args) = ask_user_command();

        auto cmd = get_command_by_name(name);
        if (cmd == nullptr) {
            std::cout << fmt::format("Command {} not found...", name) << std::endl;
            continue;
        }

        try {
            auto cmd_result = cmd->execute(args);
            cmd_result.print(std::cout);
        } catch (const std::runtime_error &exc) {
            std::cerr << fmt::format("Error: {}", exc.what()) << std::endl;
        }
    }

    ReadLineWrapper::remove_auto_completer();

    // Disable quit for next run
    m_should_quit = false;
}

std::vector<std::string> Shell::autocomplete(const std::string &text)
{
    std::vector<std::string> names;
    for (const auto &cmd : m_commands) {
        if (text.empty() || (cmd->name().rfind(text, 0) == 0)) {
            names.emplace_back(cmd->name());
        }

        if (text.empty() || (cmd->short_name().rfind(text, 0) == 0)) {
            names.emplace_back(cmd->short_name());
        }
    }

    return names;
}

std::pair<std::string, std::vector<std::string>> Shell::ask_user_command()
{
    while (true) {
        auto line = ReadLineWrapper::readline(m_prompt);
        auto parts = split_and_trim_line(line);
        if (parts.empty()) {
            continue;
        }

        ReadLineWrapper::add_history(line);
        const auto name = parts[0];
        const std::vector<std::string> args(parts.begin() + 1, parts.end());
        return std::make_pair(name, args);
    }
}

std::vector<std::string> Shell::split_and_trim_line(const std::string &line)
{
    auto parts = split_string(line, " ");

    // remove spaces 
    for (auto &part : parts) {
        part.erase(std::remove_if(part.begin(), part.end(), [](char c) {
            return std::isspace(c);
        }), part.end());
    }

    // Remove empty commands
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](const std::string &s) {
        return s.empty();
    }), parts.end());

    return parts;
}

std::string Shell::get_help() const
{
    std::string result;
    for (const auto &cmd : m_commands) {
        auto full_name = fmt::format("{}({})", cmd->name(), cmd->short_name());
        result += fmt::format("{:<30}{}\n", full_name, cmd->help());
    }
    return result;
}

ShellCommand *Shell::get_command_by_name(const std::string &name)
{
    for (const auto &cmd : m_commands) {
        if ((name == cmd->name()) || (name == cmd->short_name())) {
            return cmd.get();
        }
    }
    return nullptr;
}

Shell::Help::Help(Shell &shell) :
    ShellCommand("help", "h", "Show help on all commands"),
    m_shell(shell)
{}

ShellResult Shell::Help::execute(const std::vector<std::string> &)
{
    return m_shell.get_help();
}

Shell::Quit::Quit(Shell &shell) :
    ShellCommand("quit", "q", "Quit current shell"),
    m_shell(shell)
{}

ShellResult Shell::Quit::execute(const std::vector<std::string> &)
{
    m_shell.m_should_quit = true;
    return ShellResult("");
}


StartSubshellCommand::StartSubshellCommand(const std::string &name, const std::string &short_name,
    const std::string &help) :
    ShellCommand(name, short_name, help),
    m_shell(std::make_shared<Shell>(fmt::format("({})> ", name)))
{}

ShellResult StartSubshellCommand::execute(const std::vector<std::string> &)
{
    m_shell->run_forever();
    return ShellResult("");
}

std::shared_ptr<Shell> StartSubshellCommand::get_shell()
{
    return m_shell;
}
