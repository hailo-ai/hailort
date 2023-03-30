/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_live_track.cpp
 * @brief Network live track
 **/

#include "network_live_track.hpp"
#include "../infer_stats_printer.hpp"

#include <spdlog/fmt/fmt.h>
#include <sstream>

NetworkLiveTrack::NetworkLiveTrack(const std::string &name, std::shared_ptr<ConfiguredNetworkGroup> cng, LatencyMeterPtr overall_latency_meter) :
    m_name(name), m_count(0), m_last_get_time(), m_cng(cng), m_overall_latency_meter(overall_latency_meter)
{
}

hailo_status NetworkLiveTrack::start()
{
    m_last_get_time = std::chrono::steady_clock::now();
    m_count = 0;
    m_started = true;

    return HAILO_SUCCESS;
}

uint32_t NetworkLiveTrack::get_text(std::stringstream &ss)
{
    if (!m_started) {
        return 0;
    }

    auto elapsed_time = std::chrono::steady_clock::now() - m_last_get_time;
    auto count = m_count.load();

    auto fps = count / std::chrono::duration<double>(elapsed_time).count();
    ss << fmt::format("{}:\n\t|  fps: {:.2f}", m_name, fps);

    auto hw_latency_measurement = m_cng->get_latency_measurement();
    if (hw_latency_measurement) {
        ss << fmt::format("  |  hw latency: {:.2f} ms", InferResultsFormatUtils::latency_result_to_ms(hw_latency_measurement->avg_hw_latency));
    }
    else if (HAILO_NOT_AVAILABLE != hw_latency_measurement.status()) { // HAILO_NOT_AVAILABLE is a valid error, we ignore it
        ss << fmt::format("  |  hw latency: failed with status={}", hw_latency_measurement.status());
    }

    if (m_overall_latency_meter) {
        auto overall_latency_measurement = m_overall_latency_meter->get_latency(true);
        if (overall_latency_measurement) {
            ss << fmt::format("  |  overall latency: {:.2f} ms", InferResultsFormatUtils::latency_result_to_ms(*overall_latency_measurement));
        }
        else if (HAILO_NOT_AVAILABLE != overall_latency_measurement.status()) { // HAILO_NOT_AVAILABLE is a valid error, we ignore it
            ss << fmt::format("  |  overall latency: failed with status={}", overall_latency_measurement.status());
        }
    }
    ss << "\n";

    return 2;
}

void NetworkLiveTrack::progress()
{
    if (!m_started) {
        return;
    }

    m_count++;
}