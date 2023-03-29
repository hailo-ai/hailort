/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_live_track.hpp
 * @brief Network live track
 **/

#ifndef _HAILO_HAILORTCLI_RUN2_NETWORK_LIVE_TRACK_HPP_
#define _HAILO_HAILORTCLI_RUN2_NETWORK_LIVE_TRACK_HPP_

#include "hailo/hailort.h"
#include "hailo/network_group.hpp"

#include "common/latency_meter.hpp"

#include "live_printer.hpp"


class NetworkLiveTrack : public LivePrinter::Track
{
public:
    NetworkLiveTrack(const std::string &name, std::shared_ptr<hailort::ConfiguredNetworkGroup> cng, hailort::LatencyMeterPtr overall_latency_meter);
    virtual ~NetworkLiveTrack() = default;
    virtual hailo_status start() override;
    virtual uint32_t get_text(std::stringstream &ss) override;
    void progress();

private:
    std::string m_name;
    std::atomic<uint32_t> m_count;
    std::chrono::time_point<std::chrono::steady_clock> m_last_get_time;
    std::shared_ptr<hailort::ConfiguredNetworkGroup> m_cng;
    hailort::LatencyMeterPtr m_overall_latency_meter;
};

#endif /* _HAILO_HAILORTCLI_RUN2_NETWORK_LIVE_TRACK_HPP_ */