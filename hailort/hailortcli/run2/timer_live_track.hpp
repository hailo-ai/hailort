/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file timer_live_track.hpp
 * @brief Timer live track
 **/

#include "live_stats.hpp"

#ifndef _HAILO_HAILORTCLI_RUN2_TIMER_LIVE_TRACK_HPP_
#define _HAILO_HAILORTCLI_RUN2_TIMER_LIVE_TRACK_HPP_

class TimerLiveTrack : public LiveStats::Track
{
public:
    TimerLiveTrack(std::chrono::milliseconds duration);
    virtual ~TimerLiveTrack() = default;
    virtual hailo_status start_impl() override;
    virtual void measure() override;
    virtual std::string get_text_impl() const override;
    virtual void push_json_impl(nlohmann::ordered_json &json) override;
    virtual hailort::Expected<double> get_last_measured_fps() override;

private:
    static constexpr uint32_t MAX_PROGRESS_BAR_WIDTH = 20u;
    std::chrono::milliseconds m_duration;
    std::chrono::time_point<std::chrono::steady_clock> m_start_time;
    std::chrono::seconds m_eta;
    uint32_t m_elapsed_percentage;
    uint32_t m_progress_bar_width;
};

#endif /* _HAILO_HAILORTCLI_RUN2_TIMER_LIVE_TRACK_HPP_ */
