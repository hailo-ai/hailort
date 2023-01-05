/**
 * @file readline_wrapper.cpp
 * @brief Wrapper to the readline library, either use the library, or create simple implementation.
 */

#include "readline_wrapper.hpp"
#include <iostream>


#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#include <signal.h>

static void int_handler(int)
{
    printf("\n"); // Move to a new line
    rl_on_new_line(); // Regenerate the prompt on a newline
    rl_replace_line("", 0); // Clear the previous text
    rl_redisplay();
}

static ReadLineWrapper::AutoCompleter g_auto_completer = nullptr;

static char *name_generator(const char *text, int index)
{
    if (!g_auto_completer) {
        return nullptr;
    }

    auto results = g_auto_completer(std::string(text));
    if (static_cast<size_t>(index) >= results.size()) {
        return nullptr;
    }

    return strdup(results[index].c_str());
}

static char **name_completion(const char *text, int start, int)
{
    if (start > 0) {
        // We use autocomplete only for the first arg (command name).
        return nullptr;
    }

    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, name_generator);
}

void ReadLineWrapper::init_library()
{
    rl_attempted_completion_function = name_completion;
    signal(SIGINT, int_handler);
}

std::string ReadLineWrapper::readline(const std::string &prompt)
{
    auto line_raw = ::readline(prompt.c_str());
    if (line_raw == nullptr) {
        // Ctrl+D handle
        printf("\n");
        return "";
    }

    const std::string line(line_raw);
    free(line_raw);
    return line;
}

void ReadLineWrapper::add_history(const std::string &line)
{
    ::add_history(line.c_str());
}

void ReadLineWrapper::set_auto_completer(AutoCompleter completer)
{
    g_auto_completer = completer;
}

void ReadLineWrapper::remove_auto_completer()
{
    g_auto_completer = nullptr;
}

#else

void ReadLineWrapper::init_library()
{}

// Non readline implementation
std::string ReadLineWrapper::readline(const std::string &prompt)
{
    std::cout << prompt;
    std::string command;
    std::getline(std::cin, command);
    return command;
}

void ReadLineWrapper::add_history(const std::string &)
{
    // No history, just NOP.
}

void ReadLineWrapper::set_auto_completer(AutoCompleter)
{}

void ReadLineWrapper::remove_auto_completer()
{}

#endif