/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file logger_macros.hpp
 * @brief Declares logger used by hailort.
 **/

#ifndef _HAILORT_LOGGER_HPP_
#define _HAILORT_LOGGER_HPP_


#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "hailo/hailort.h"
#include "common/logger_macros.hpp"

namespace hailort
{

class HailoRTLogger {
public:
    static HailoRTLogger& get_instance()
    {
        static HailoRTLogger instance;
        return instance;
    }
    HailoRTLogger(HailoRTLogger const&) = delete;
    void operator=(HailoRTLogger const&) = delete;

    std::shared_ptr<spdlog::logger> logger();
    void set_levels(spdlog::level::level_enum console_level, spdlog::level::level_enum file_level,
        spdlog::level::level_enum flush_level);
    static std::shared_ptr<spdlog::sinks::sink> create_file_sink();
private:
    HailoRTLogger();

    std::shared_ptr<spdlog::sinks::sink> m_console_sink;
    std::shared_ptr<spdlog::sinks::sink> m_file_sink;
    std::shared_ptr<spdlog::logger> m_hailort_logger;
};

} /* namespace hailort */

#endif /* _HAILORT_LOGGER_HPP_ */
