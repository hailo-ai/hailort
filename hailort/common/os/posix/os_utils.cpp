/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file os_utils.cpp
 * @brief Utilities for Posix methods
 **/

#include "hailo/hailort.h"

#include "common/os_utils.hpp"

#include "spdlog/sinks/syslog_sink.h"


namespace hailort
{

HailoRTOSLogger::HailoRTOSLogger()
{
    m_hailort_os_logger = spdlog::syslog_logger_mt("syslog", "hailort_service", LOG_PID);
    m_hailort_os_logger->set_pattern("%v");
    m_hailort_os_logger->set_level(spdlog::level::debug);
}

uint32_t OsUtils::get_curr_pid()
{
    return getpid();
}

CursorAdjustment::CursorAdjustment(){}
CursorAdjustment::~CursorAdjustment(){}

} /* namespace hailort */
