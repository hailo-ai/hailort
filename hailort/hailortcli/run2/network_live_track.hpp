/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_live_track.hpp
 * @brief Network live track
 **/

#include "live_printer.hpp"

#ifndef _HAILO_HAILORTCLI_RUN2_NETWORK_LIVE_TRACK_HPP_
#define _HAILO_HAILORTCLI_RUN2_NETWORK_LIVE_TRACK_HPP_

class NetworkLiveTrack : public LivePrinter::Track
{
public:
    NetworkLiveTrack(const std::string &name);
    virtual ~NetworkLiveTrack() = default;
    uint32_t get_text(std::stringstream &ss);
    void progress();

private:
    std::string m_name;
    std::atomic<uint32_t> m_count;
    std::chrono::time_point<std::chrono::steady_clock> m_last_get_time;
};

#endif /* _HAILO_HAILORTCLI_RUN2_NETWORK_LIVE_TRACK_HPP_ */