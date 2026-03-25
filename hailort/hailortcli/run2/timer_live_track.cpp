/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file timer_live_track.cpp
 * @brief Timer live track
 **/

#include "timer_live_track.hpp"
#include "../common.hpp"
#include <spdlog/fmt/fmt.h>

TimerLiveTrack::TimerLiveTrack(std::chrono::milliseconds duration) :
        LiveStats::Track(), m_duration(duration), m_start_time()
{
}

hailo_status TimerLiveTrack::start_impl()
{
    m_start_time = std::chrono::steady_clock::now();
    return HAILO_SUCCESS;
}

void TimerLiveTrack::measure()
{
    auto elapsed_time = std::chrono::steady_clock::now() - m_start_time;
    m_eta = std::chrono::seconds(std::max<int32_t>(0, static_cast<int32_t>(std::round(std::chrono::duration<double>(m_duration - elapsed_time).count()))));
    m_elapsed_percentage = std::min<uint32_t>(100, static_cast<uint32_t>(std::round(std::chrono::duration<double>(100 * elapsed_time / m_duration).count())));
    m_progress_bar_width = std::max<uint32_t>(1, std::min<uint32_t>(MAX_PROGRESS_BAR_WIDTH,
        static_cast<uint32_t>(std::round(std::chrono::duration<double>(MAX_PROGRESS_BAR_WIDTH * elapsed_time / m_duration).count()))));
}

std::string TimerLiveTrack::get_text_impl() const
{
    return fmt::format("[{:=>{}}{:{}}] {:>3}% {}\n", '>', m_progress_bar_width, "",
        MAX_PROGRESS_BAR_WIDTH - m_progress_bar_width, m_elapsed_percentage, CliCommon::duration_to_string(m_eta));
}

void TimerLiveTrack::push_json_impl(nlohmann::ordered_json &json)
{
    json["time_to_run"] = fmt::format("{:.2f} seconds", std::round(std::chrono::duration<double>(m_duration).count()));
}

Expected<double> TimerLiveTrack::get_last_measured_fps()
{
    return make_unexpected(HAILO_NOT_AVAILABLE);
}
