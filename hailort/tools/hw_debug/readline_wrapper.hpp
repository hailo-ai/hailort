/**
 * @file readline_wrapper.hpp
 * @brief Wrapper to the readline library, either use the library, or create simple implementation.
 */

#ifndef _HW_DEBUG_READLINE_WRAPPER_H_
#define _HW_DEBUG_READLINE_WRAPPER_H_

#include <string>
#include <vector>
#include <functional>

class ReadLineWrapper final {
public:
    ReadLineWrapper() = delete;

    static void init_library();
    static std::string readline(const std::string &prompt);
    static void add_history(const std::string &line);

    using AutoCompleter = std::function<std::vector<std::string>(const std::string &text)>;
    static void set_auto_completer(AutoCompleter completer);
    static void remove_auto_completer();
};

#endif /* _HW_DEBUG_READLINE_WRAPPER_H_ */