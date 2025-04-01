/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_live_track.hpp
 * @brief Network live track
 **/

#ifndef _HAILO_HAILORTCLI_RUN2_NETWORK_LIVE_TRACK_HPP_
#define _HAILO_HAILORTCLI_RUN2_NETWORK_LIVE_TRACK_HPP_

#include "hailo/hailort.h"
#include "hailo/infer_model.hpp"
#include "hailo/network_group.hpp"

#include "common/latency_meter.hpp"

#include "live_stats.hpp"

#include <nlohmann/json.hpp>


class NetworkLiveTrack : public LiveStats::Track
{
public:
    NetworkLiveTrack(const std::string &name, std::shared_ptr<hailort::ConfiguredNetworkGroup> cng,
        std::shared_ptr<hailort::ConfiguredInferModel> configured_infer_model,
        hailort::LatencyMeterPtr overall_latency_meter, bool measure_fps, const std::string &hef_path);
    virtual ~NetworkLiveTrack() = default;
    virtual hailo_status start_impl() override;
    virtual uint32_t push_text_impl(std::stringstream &ss) override;
    virtual void push_json_impl(nlohmann::ordered_json &json) override;

    void progress();

    hailort::Expected<double> get_last_measured_fps();

private:
    double get_fps();

    static size_t max_ng_name;
    static std::mutex mutex;

    std::string m_name;
    std::atomic<uint32_t> m_count;
    std::chrono::time_point<std::chrono::steady_clock> m_last_get_time;
    std::shared_ptr<hailort::ConfiguredNetworkGroup> m_cng;
    std::shared_ptr<hailort::ConfiguredInferModel> m_configured_infer_model;
    hailort::LatencyMeterPtr m_overall_latency_meter;
    const bool m_measure_fps;
    const std::string &m_hef_path;

    double m_last_measured_fps;
};

#endif /* _HAILO_HAILORTCLI_RUN2_NETWORK_LIVE_TRACK_HPP_ */