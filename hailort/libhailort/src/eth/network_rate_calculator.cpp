/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_rate_calculator.cpp
 * @brief: Network rate calculator
 **/


#include "hailo/hailort.h"
#include "hailo/network_rate_calculator.hpp"

#include "common/utils.hpp"

#include "eth/eth_stream.hpp"

#include <numeric>
#include <algorithm>


namespace hailort
{

Expected<StreamInfoVector> NetworkUdpRateCalculator::get_streams_from_hef(Hef* hef, const std::string &network_group_name)
{
    assert(nullptr != hef);

    auto all_streams_infos = hef->get_all_stream_infos(network_group_name);
    CHECK_EXPECTED(all_streams_infos);

    // We expect to have two or more streams (atleast one for input and one for output)
    if (all_streams_infos->size() < 2) {
        return make_unexpected(HAILO_INVALID_HEF);
    }

    return all_streams_infos;
}

NetworkUdpRateCalculator::NetworkUdpRateCalculator(std::map<std::string, uint32_t> &&input_edge_shapes,
    std::map<std::string, uint32_t> &&output_edge_shapes) :
    m_input_edge_shapes(std::move(input_edge_shapes)),
    m_output_edge_shapes(std::move(output_edge_shapes)) {}

Expected<NetworkUdpRateCalculator> NetworkUdpRateCalculator::create(Hef* hef, const std::string &network_group_name)
{
    if (hef == nullptr) {
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
    const auto stream_infos = get_streams_from_hef(hef, network_group_name);
    if (!stream_infos) {
        return make_unexpected(stream_infos.status());
    }

    // Working with HEF for rate_calcs assums that all streams are udp streams
    std::map<std::string, uint32_t> input_udp_edge_shapes;
    std::map<std::string, uint32_t> output_udp_edge_shapes;
    for (auto &info : stream_infos.value()) {
        if (HAILO_H2D_STREAM == info.direction) {
            input_udp_edge_shapes.insert(std::make_pair(info.name, info.hw_frame_size));
        } else if (HAILO_D2H_STREAM == info.direction) {
            output_udp_edge_shapes.insert(std::make_pair(info.name, info.hw_frame_size));
        } else {
            LOGGER__ERROR("Invalid stream direction for stream {}.", info.name);
            return make_unexpected(HAILO_INTERNAL_FAILURE);
        }
    }

    return NetworkUdpRateCalculator(std::move(input_udp_edge_shapes), std::move(output_udp_edge_shapes));
}

Expected<NetworkUdpRateCalculator> NetworkUdpRateCalculator::create(ConfiguredNetworkGroup &net_group)
{
    auto udp_input_streams = net_group.get_input_streams_by_interface(HAILO_STREAM_INTERFACE_ETH);
    CHECK_AS_EXPECTED(!udp_input_streams.empty(), HAILO_INVALID_OPERATION,
        "There are no udp input streams in this network_group.");
    auto udp_output_streams = net_group.get_output_streams_by_interface(HAILO_STREAM_INTERFACE_ETH);

    std::map<std::string, uint32_t> input_udp_edge_shapes;
    for (const auto &stream : udp_input_streams) {
        input_udp_edge_shapes.insert(std::make_pair(stream.get().name(),
            stream.get().get_info().hw_frame_size));
    }
    std::map<std::string, uint32_t> output_udp_edge_shapes;
    for (const auto &stream : udp_output_streams) {
        output_udp_edge_shapes.insert(std::make_pair(stream.get().name(),
            stream.get().get_info().hw_frame_size));
    }

    return NetworkUdpRateCalculator(std::move(input_udp_edge_shapes), std::move(output_udp_edge_shapes));
}

Expected<std::map<std::string, uint32_t>> NetworkUdpRateCalculator::calculate_inputs_bandwith(uint32_t fps,
    uint32_t max_supported_bandwidth)
{
    if (1 > fps) {
        fps = 1;
        LOGGER__WARNING("FPS for rate calculations cannot be smaller than 1. calculating rate_limiter with fps=1.");
    }

    std::map<std::string, uint32_t> input_rates;
    std::transform(m_input_edge_shapes.begin(), m_input_edge_shapes.end(), std::inserter(input_rates, input_rates.end()),
        [fps](auto &input_edge_pair) { return std::make_pair(input_edge_pair.first, (fps * input_edge_pair.second)); });

    std::map<std::string, uint32_t> output_rates = {};
    std::transform(m_output_edge_shapes.begin(), m_output_edge_shapes.end(), std::inserter(output_rates, output_rates.end()),
        [fps](auto &output_edge_pair) { return std::make_pair(output_edge_pair.first, (fps * output_edge_pair.second)); });

    uint32_t total_input_rate = std::accumulate(input_rates.begin(), input_rates.end(), 0,
        [](int value, const auto &p) { return value + p.second; });
    uint32_t total_output_rate = std::accumulate(output_rates.begin(), output_rates.end(), 0,
        [](int value, const auto &p) { return value + p.second; });

    if ((total_input_rate > max_supported_bandwidth) || (total_output_rate > max_supported_bandwidth)) {
        LOGGER__WARNING("Requested rate (input: {} Bps, output: {} Bps) is high and might be unstable. Setting rate to {}.",
            total_input_rate, total_output_rate, max_supported_bandwidth);
        if (total_output_rate > total_input_rate) {
            // Output is bigger than max rate. Adjusting input rate accordingly
            auto input_output_ratio = (total_input_rate / total_output_rate);
            LOGGER__WARNING("Output Bps ({}) is bigger than input Bps ({}) output (ratio is: {})", total_output_rate,
                total_input_rate, input_output_ratio);
            max_supported_bandwidth *= input_output_ratio;
        }
        auto total_inputs_rate_to_max_supported_ratio = (static_cast<float64_t>(max_supported_bandwidth) / total_input_rate);
        for (auto &rate_pair : input_rates) {
            auto rate = rate_pair.second * total_inputs_rate_to_max_supported_ratio;
            rate_pair.second = static_cast<uint32_t>(rate);
        }
    }

    return input_rates;
}

Expected<std::map<uint16_t, uint32_t>> NetworkUdpRateCalculator::get_udp_ports_rates_dict(
    std::vector<std::reference_wrapper<InputStream>> &udp_input_streams, uint32_t fps, uint32_t max_supported_bandwidth)
{
    auto rates_per_name = calculate_inputs_bandwith(fps, max_supported_bandwidth);
    CHECK_EXPECTED(rates_per_name);

    std::map<uint16_t, uint32_t> results = {};
    for (const auto &input_stream : udp_input_streams) {
        uint16_t remote_port = 0;
        remote_port = reinterpret_cast<EthernetInputStream*>(&(input_stream.get()))->get_remote_port();
        results.insert(std::make_pair(remote_port,
            rates_per_name->at(input_stream.get().name())));
    }

    return results;
}

} /* namespace hailort */
