/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

size_t NetworkLiveTrack::max_ng_name = 0;
std::mutex NetworkLiveTrack::mutex;

NetworkLiveTrack::NetworkLiveTrack(const std::string &name, std::shared_ptr<ConfiguredNetworkGroup> cng,
    std::shared_ptr<ConfiguredInferModel> configured_infer_model, LatencyMeterPtr overall_latency_meter,
    bool measure_fps, const std::string &hef_path) :
    m_name(name),
    m_count(0),
    m_last_get_time(),
    m_cng(cng),
    m_configured_infer_model(configured_infer_model),
    m_overall_latency_meter(overall_latency_meter),
    m_measure_fps(measure_fps),
    m_hef_path(hef_path),
    m_last_measured_fps(0)
{
    std::lock_guard<std::mutex> lock(mutex);
    max_ng_name = std::max(m_name.size(), max_ng_name);
}

hailo_status NetworkLiveTrack::start_impl()
{
    m_last_get_time = std::chrono::steady_clock::now();
    m_count = 0;

    return HAILO_SUCCESS;
}

double NetworkLiveTrack::get_fps()
{
    auto elapsed_time = std::chrono::steady_clock::now() - m_last_get_time;
    auto count = m_count.load();
    auto fps = count / std::chrono::duration<double>(elapsed_time).count();
    m_last_measured_fps = fps;
    return fps;
}

Expected<double> NetworkLiveTrack::get_last_measured_fps()
{
    return Expected<double>(m_last_measured_fps);
}

uint32_t NetworkLiveTrack::push_text_impl(std::stringstream &ss)
{
    ss << fmt::format("{}:", m_name);
    ss << std::string(max_ng_name - m_name.size(), ' ');

    bool first = true;
    auto get_separator = [&first] () {
        auto res =  first ? " " : " | ";
        first = false;
        return res;
    };

    if (m_measure_fps) {
        auto fps = get_fps();
        ss << fmt::format("{}fps: {:.2f}", get_separator(), fps);
    }

    if (m_cng) {
        auto hw_latency_measurement = m_cng->get_latency_measurement();
        if (hw_latency_measurement) {
            ss << fmt::format("{}hw latency: {:.2f} ms", get_separator(), InferStatsPrinter::latency_result_to_ms(hw_latency_measurement->avg_hw_latency));
        } else if (HAILO_NOT_AVAILABLE != hw_latency_measurement.status()) { // HAILO_NOT_AVAILABLE is a valid error, we ignore it
            ss << fmt::format("{}hw latency: NaN (err)", get_separator());
        }
    }
    else {
        auto hw_latency_measurement = m_configured_infer_model->get_hw_latency_measurement();
        if (hw_latency_measurement) {
            ss << fmt::format("{}hw latency: {:.2f} ms", get_separator(), InferStatsPrinter::latency_result_to_ms(hw_latency_measurement->avg_hw_latency));
        }
        else if (HAILO_NOT_AVAILABLE != hw_latency_measurement.status()) { // HAILO_NOT_AVAILABLE is a valid error, we ignore it
            ss << fmt::format("{}hw latency: NaN (err)", get_separator());
        }
    }

    if (m_overall_latency_meter) {
        auto overall_latency_measurement = m_overall_latency_meter->get_latency(false);
        if (overall_latency_measurement) {
            ss << fmt::format("{}overall latency: {:.2f} ms", get_separator(), InferStatsPrinter::latency_result_to_ms(*overall_latency_measurement));
        }
        else if (HAILO_NOT_AVAILABLE != overall_latency_measurement.status()) { // HAILO_NOT_AVAILABLE is a valid error, we ignore it
            ss << fmt::format("{}overall latency: NaN (err)", get_separator());
        }
    }
    ss << "\n";

    return 1;
}

void NetworkLiveTrack::push_json_impl(nlohmann::ordered_json &json)
{
    nlohmann::ordered_json network_group_json;
    network_group_json["name"] = m_name;
    network_group_json["full_hef_path"] = m_hef_path;

    // TODO: HRT-8695 Support stats display per network
    // auto networks_info = m_cng->get_network_infos();
    // if (networks_info){
    //     network_group_json["networks"] = nlohmann::ordered_json::array();
    //     for (const auto &network_info : networks_info.value()){
    //         network_group_json["networks"].emplace_back(nlohmann::json::object({ {"name", network_info.name} }));
    //     }
    // }

    if (m_measure_fps) {
        auto fps = get_fps();
        network_group_json["FPS"] = std::to_string(fps);
    }

    if (m_cng) {
        auto hw_latency_measurement = m_cng->get_latency_measurement();
        if (hw_latency_measurement){
            network_group_json["hw_latency"] = InferStatsPrinter::latency_result_to_ms(hw_latency_measurement->avg_hw_latency);
        }
    }
    else {
        auto hw_latency_measurement = m_configured_infer_model->get_hw_latency_measurement();
        if (hw_latency_measurement){
            network_group_json["hw_latency"] = InferStatsPrinter::latency_result_to_ms(hw_latency_measurement->avg_hw_latency);
        }
    }


    if (m_overall_latency_meter){
        auto overall_latency_measurement = m_overall_latency_meter->get_latency(false);
        if (overall_latency_measurement){
            network_group_json["overall_latency"] = InferStatsPrinter::latency_result_to_ms(*overall_latency_measurement);
        }
    }
    json["network_groups"].emplace_back(network_group_json);
}

void NetworkLiveTrack::progress()
{
    if (!m_started) {
        return;
    }

    m_count++;
}