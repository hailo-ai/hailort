/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file os_utils.cpp
 * @brief Utilities for Windows methods
 **/

#include "common/os_utils.hpp"
#include "hailo/hailort.h"

#include <windows.h>
#include "spdlog/sinks/win_eventlog_sink.h"

namespace hailort
{

HailoRTOSLogger::HailoRTOSLogger()
{
    auto event_log_sink = std::make_shared<spdlog::sinks::win_eventlog_sink_mt>("hailort_service");
    m_hailort_os_logger = std::make_shared<spdlog::logger>("eventlog", event_log_sink);
    event_log_sink->set_pattern("%v");
    m_hailort_os_logger->set_level(spdlog::level::debug);
}

uint32_t OsUtils::get_curr_pid()
{
    return static_cast<uint32_t>(GetCurrentProcessId());
}

CursorAdjustment::CursorAdjustment()
{
    // Enables Vitual Terminal Processing - enables ANSI Escape Sequences on Windows
    // Source: https://stackoverflow.com/questions/52607960/how-can-i-enable-virtual-terminal-processing
    HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dword_mode = 0;
    GetConsoleMode(h_out, &dword_mode);
    m_previous_output_buffer_mode = dword_mode;
    dword_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(h_out, dword_mode);
}

CursorAdjustment::~CursorAdjustment()
{
        // Return to the original state
        HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleMode(h_out, m_previous_output_buffer_mode); // Return the output buffer mode to it's original mode
}
} /* namespace hailort */
