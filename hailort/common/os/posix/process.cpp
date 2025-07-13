/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file process.cpp
 * @brief Process wrapper for Linux
 **/

#include "common/process.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"
#include "hailo/buffer.hpp"

namespace hailort
{

Expected<std::pair<int32_t, std::string>> Process::create_and_wait_for_output(const std::string &command_line, uint32_t max_output_size)
{
    TRY(auto popen, PopenWrapper::create(command_line));
    TRY(const auto output, popen.read_stdout(max_output_size));
    const auto process_exit_code = popen.close();
    return std::make_pair(process_exit_code, output);
}

Expected<Process::PopenWrapper> Process::PopenWrapper::create(const std::string &command_line)
{
    hailo_status status = HAILO_UNINITIALIZED;
    PopenWrapper popen(command_line, status);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return popen;
}

Process::PopenWrapper::PopenWrapper(const std::string &command_line, hailo_status &status) :
    m_command_line(command_line)
{
    static const char* READONLY_MODE = "r";
    m_pipe = popen(command_line.c_str(), READONLY_MODE);
    if (nullptr == m_pipe) {
        LOGGER__ERROR("popen(\"{}\") failed with errno={}", command_line.c_str(), errno);
        status = HAILO_INTERNAL_FAILURE;
    } else {
        status = HAILO_SUCCESS;
    }
}

Process::PopenWrapper::~PopenWrapper()
{
    if (nullptr != m_pipe) {
        (void) close();
    }
}

Process::PopenWrapper::PopenWrapper(PopenWrapper &&other) :
    m_pipe(std::exchange(other.m_pipe, nullptr))
{}

Expected<std::string> Process::PopenWrapper::read_stdout(uint32_t max_output_size)
{
    assert (nullptr != m_pipe);

    // We zero out the buffer so that output won't contain junk from the heap
    TRY(auto output, Buffer::create(max_output_size, 0));
    
    const auto num_read = fread(reinterpret_cast<char*>(output.data()), sizeof(uint8_t), output.size(), m_pipe);
    if (!feof(m_pipe)) {
        if (output.size() == num_read) {
            // Truncate output
            LOGGER__WARNING("Truncating output of command \"{}\" to {} chars long! "
                "The max_output_size needs to be bigger!", m_command_line, max_output_size);
            return output.to_string();
        } else {
            LOGGER__ERROR("fread failed with ferror={}", ferror(m_pipe));
            return make_unexpected(HAILO_FILE_OPERATION_FAILURE);
        }
    }

    // We remove the trailing newline we get from fread
    const auto output_as_str = output.to_string();
    if (output_as_str[output_as_str.length() - 1] == '\n') {
        return output_as_str.substr(0, num_read - 1);
    }
    return output_as_str.substr(0, num_read);
}

int32_t Process::PopenWrapper::close()
{
    assert (nullptr != m_pipe);
    const auto return_code = pclose(m_pipe);
    m_pipe = nullptr; // We only close the handle once
    return return_code;
}

} /* namespace hailort */
