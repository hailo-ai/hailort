/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file timer_live_track.hpp
 * @brief Timer live track
 **/

#include "live_printer.hpp"

#ifndef _HAILO_HAILORTCLI_RUN2_TIMER_LIVE_TRACK_HPP_
#define _HAILO_HAILORTCLI_RUN2_TIMER_LIVE_TRACK_HPP_

class TimerLiveTrack : public LivePrinter::Track
{
public:
    TimerLiveTrack(std::chrono::milliseconds duration);
    virtual ~TimerLiveTrack() = default;
    virtual uint32_t get_text(std::stringstream &ss) override;

private:
    std::chrono::milliseconds m_duration;
    std::chrono::time_point<std::chrono::steady_clock> m_start;
};

#endif /* _HAILO_HAILORTCLI_RUN2_TIMER_LIVE_TRACK_HPP_ */