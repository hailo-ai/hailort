/**
 * @file shell.hpp
 * @brief Generic shell - contains commands and sub-shells. The shell implements
 *        a parse-execute commands loop.
 */

#ifndef _HW_DEBUG_SHELL_H_
#define _HW_DEBUG_SHELL_H_

#include <string>
#include <iostream>
#include <sstream>
#include <vector>
#include <memory>

// Result returned from each command. Currently wrapper to the output string.
class ShellResult final {
public:
    ShellResult(const std::string &str) :
        m_str(str)
    {}

    ShellResult(const std::vector<ShellResult> &results)
    {
        std::stringstream out;
        for (const auto &result : results) {
            result.print(out);
        }
        m_str = out.str();
    }

    void print(std::ostream &out) const
    {
        out << m_str;
    }

private:
    std::string m_str;
};

// Base abstract class for some shell command.
class ShellCommand {
public:
    virtual ~ShellCommand() = default;

    ShellCommand(const std::string &name, const std::string &short_name,
        const std::string &help);
    
    std::string name() const { return m_name; }
    std::string short_name() const { return m_short_name; }
    std::string help() const { return m_help; }

    virtual ShellResult execute(const std::vector<std::string> &args) = 0;
private:
    const std::string m_name;
    const std::string m_short_name;
    const std::string m_help;
};

class Shell final {
public:
    explicit Shell(const std::string &prompt);

    Shell(const Shell &other) = delete;
    Shell &operator=(const Shell &other) = delete;

    void add_command(std::unique_ptr<ShellCommand> shell_command);
    std::shared_ptr<Shell> add_subshell(const std::string &name, const std::string &short_name);
    void run_forever();
    std::vector<std::string> autocomplete(const std::string &text);

private:

    class Help : public ShellCommand {
    public:
        Help(Shell &shell);
        ShellResult execute(const std::vector<std::string> &args) override;
    private:
        Shell &m_shell;
    };

    class Quit : public ShellCommand {
    public:
        Quit(Shell &shell);
        ShellResult execute(const std::vector<std::string> &args) override;
    private:
        Shell &m_shell;
    };

    // pair of command name and its arguments.
    std::pair<std::string, std::vector<std::string>> ask_user_command();
    static std::vector<std::string> split_and_trim_line(const std::string &line);

    std::string get_help() const;
    // Gets a command or nullptr if it doesn't exists.
    ShellCommand *get_command_by_name(const std::string &name);

    const std::string m_prompt;
    std::vector<std::unique_ptr<ShellCommand>> m_commands;
    bool m_should_quit;
};


// This command starts a new subshell
class StartSubshellCommand : public ShellCommand {
public:
    StartSubshellCommand(const std::string &name, const std::string &short_name,
        const std::string &help);
    ShellResult execute(const std::vector<std::string> &) override;

    std::shared_ptr<Shell> get_shell();
private:
    std::shared_ptr<Shell> m_shell;
};

#endif /* _HW_DEBUG_SHELL_H_ */