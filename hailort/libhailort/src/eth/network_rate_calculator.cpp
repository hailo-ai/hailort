/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_rate_calculator.cpp
 * @brief Network rate calculator.
 *
 * DEPRECATED: ethernet/UDP firmware control is no longer supported. The
 * NetworkUdpRateCalculator class is retained because it is part of the
 * exported HAILORTAPI surface, but every method now returns
 * HAILO_NOT_SUPPORTED. It will be removed in a future release.
 **/


#include "hailo/hailort.h"
#include "hailo/network_rate_calculator.hpp"


namespace hailort
{

NetworkUdpRateCalculator::NetworkUdpRateCalculator(std::map<std::string, uint32_t> &&input_edge_shapes,
    std::map<std::string, uint32_t> &&output_edge_shapes) :
    m_input_edge_shapes(std::move(input_edge_shapes)),
    m_output_edge_shapes(std::move(output_edge_shapes)) {}

Expected<StreamInfoVector> NetworkUdpRateCalculator::get_streams_from_hef(Hef* hef, const std::string &network_group_name)
{
    (void)hef;
    (void)network_group_name;
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

Expected<NetworkUdpRateCalculator> NetworkUdpRateCalculator::create(Hef* hef, const std::string &network_group_name)
{
    (void)hef;
    (void)network_group_name;
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

Expected<NetworkUdpRateCalculator> NetworkUdpRateCalculator::create(ConfiguredNetworkGroup &net_group)
{
    (void)net_group;
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

Expected<std::map<std::string, uint32_t>> NetworkUdpRateCalculator::calculate_inputs_bandwith(uint32_t fps,
    uint32_t max_supported_bandwidth)
{
    (void)fps;
    (void)max_supported_bandwidth;
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

Expected<std::map<uint16_t, uint32_t>> NetworkUdpRateCalculator::get_udp_ports_rates_dict(
    std::vector<std::reference_wrapper<InputStream>> &udp_input_streams, uint32_t fps, uint32_t max_supported_bandwidth)
{
    (void)udp_input_streams;
    (void)fps;
    (void)max_supported_bandwidth;
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

hailo_status NetworkUdpRateCalculator::set_rate_limit(const std::string &ip, uint16_t port, uint32_t rate_bytes_per_sec)
{
    (void)ip;
    (void)port;
    (void)rate_bytes_per_sec;
    return HAILO_NOT_SUPPORTED;
}

hailo_status NetworkUdpRateCalculator::reset_rate_limit(const std::string &ip, uint16_t port)
{
    (void)ip;
    (void)port;
    return HAILO_NOT_SUPPORTED;
}

Expected<std::string> NetworkUdpRateCalculator::get_interface_name(const std::string &ip)
{
    (void)ip;
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

} /* namespace hailort */
