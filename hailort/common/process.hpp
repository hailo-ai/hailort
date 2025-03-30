/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file process.hpp
 * @brief Create shell processes and retrieve output
 **/

#ifndef _OS_PROCESS_HPP_
#define _OS_PROCESS_HPP_

#include "hailo/hailort.h"
#include "hailo/platform.h"

#include "hailo/expected.hpp"

#include <string>

namespace hailort
{

class Process final {
public:
    // Note:
    // * This function will block!
    // * If the process' output size exceeds max_output_size, the output will be truncated to max_output_size
    // * We remove the trailing newline if it's the last char
    static Expected<std::pair<int32_t, std::string>> create_and_wait_for_output(const std::string &command_line, uint32_t max_output_size);
    Process() = delete;

private:
    #if defined(__GNUC__)
    class PopenWrapper final {
    public:
        static Expected<PopenWrapper> create(const std::string &command_line);
        ~PopenWrapper();
        PopenWrapper(const PopenWrapper &other) = delete;
        PopenWrapper &operator=(const PopenWrapper &other) = delete;
        PopenWrapper &operator=(PopenWrapper &&other) = delete;
        PopenWrapper(PopenWrapper &&other);
        
        Expected<std::string> read_stdout(uint32_t max_output_size);
        // This function is to be called only once!
        int32_t close();

    private:
        PopenWrapper(const std::string &command_line, hailo_status &status);

        const std::string m_command_line;
        FILE* m_pipe;
    };
    #endif
};

} /* namespace hailort */

#endif /* _OS_PROCESS_HPP_ */
