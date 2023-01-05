/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file timer_live_track.cpp
 * @brief Timer live track
 **/

#include "timer_live_track.hpp"
#include "../common.hpp"
#include <spdlog/fmt/fmt.h>
#include <sstream>

TimerLiveTrack::TimerLiveTrack(std::chrono::milliseconds duration) :
        m_duration(duration), m_start(std::chrono::steady_clock::now())
{
}

uint32_t TimerLiveTrack::get_text(std::stringstream &ss)
{
    static const uint32_t MAX_PROGRESS_BAR_WIDTH = 20;
    auto elapsed_time = std::chrono::steady_clock::now() - m_start;
    auto eta = std::chrono::seconds(std::max<int32_t>(0, static_cast<int32_t>(std::round(std::chrono::duration<double>(m_duration - elapsed_time).count())))); // std::chrono::round is from C++17
    auto elapsed_percentage = std::min<uint32_t>(100, static_cast<uint32_t>(std::round(std::chrono::duration<double>(100 * elapsed_time / m_duration).count())));
    auto progress_bar_width = std::max<uint32_t>(1, std::min<uint32_t>(MAX_PROGRESS_BAR_WIDTH,
        static_cast<uint32_t>(std::round(std::chrono::duration<double>(MAX_PROGRESS_BAR_WIDTH * elapsed_time / m_duration).count()))));

    ss << fmt::format("[{:=>{}}{:{}}] {:>3}% {}\n", '>', progress_bar_width, "", MAX_PROGRESS_BAR_WIDTH - progress_bar_width, elapsed_percentage, CliCommon::duration_to_string(eta));
    return 1;
}